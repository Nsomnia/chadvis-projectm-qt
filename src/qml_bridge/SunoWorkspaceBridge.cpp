// SunoWorkspaceBridge.cpp — the QML face of the local creation workspace.
//
// See SunoWorkspaceBridge.hpp for what this surface deliberately does not expose, and
// SunoWorkspace.hpp for why the class underneath shrank. Two rules govern this file:
//
//   * Nothing here invents a producer. Every signal this bridge emits is triggered by
//     a `SunoWorkspace` signal or by a call it made, and every refusal is returned
//     rather than logged and dropped.
//   * Nothing here converts a render percent into a fraction. -1 means "no
//     denominator" and is forwarded as -1.

#include "SunoWorkspaceBridge.hpp"

#include "core/Logger.hpp"

#include <QMetaType>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qml_bridge {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Refusal shapes
// ─────────────────────────────────────────────────────────────────────────────

/// `{ok, kind, subject, message}` — the shape of a `SunoWorkspace::WorkspaceRefusal`
/// after it crosses into QML.
///
/// `kind` is `workspaceRefusalKindName(...)`, never an ordinal, so a QML `switch` and
/// a log line name the same thing. `subject` is the offender as its owner spells it
/// (`"render queue"`, or `JobError::field`), so no consumer has to invent a second
/// name for the same subject.
QVariantMap refusalToQml(const vc::suno::WorkspaceRefusal& refusal) {
    QVariantMap map;
    map["ok"] = false;
    map["kind"] =
            QString::fromStdString(std::string(vc::suno::workspaceRefusalKindName(refusal.kind)));
    map["subject"] = QString::fromStdString(refusal.subject);
    map["message"] = QString::fromStdString(refusal.message);
    return map;
}

/// `{ok: true}` — the whole map, because a caller that reads `kind` on a success gets
/// an empty string rather than an undefined key.
QVariantMap acceptedToQml() {
    QVariantMap map;
    map["ok"] = true;
    return map;
}

// ─────────────────────────────────────────────────────────────────────────────
// RenderJob translation
// ─────────────────────────────────────────────────────────────────────────────

/// A key this file could not read. `subject` is the QML map key verbatim, so the
/// message names what the caller actually wrote.
///
/// Deliberately **not** a `WorkspaceRefusalKind`: those describe what the workspace
/// decided, and for a map this file refused to read the workspace was never asked.
/// Reporting it as a workspace refusal would put a translation problem in the same
/// bucket as a missing queue.
struct FieldRefusal {
    std::string subject;
    std::string message;

    [[nodiscard]] std::string describe() const { return subject + ": " + message; }
};

/// `{ok: false, kind: "invalid-job-fields", subject, message}` — the same four-key
/// shape as a workspace refusal so a QML caller reads one thing, with a `kind` that
/// cannot be confused with one of `workspaceRefusalKindName`'s four tokens.
QVariantMap fieldRefusalToQml(const FieldRefusal& refusal) {
    QVariantMap map;
    map["ok"] = false;
    map["kind"] = QStringLiteral("invalid-job-fields");
    map["subject"] = QString::fromStdString(refusal.subject);
    map["message"] = QString::fromStdString(refusal.message);
    return map;
}

/// Every key `jobFromMap` understands: `RenderJob`'s own field names, verbatim.
///
/// The list exists so an unknown key is **refused** rather than ignored. A typo'd
/// `videocodec` that was silently dropped would render with `RenderJob`'s default and
/// produce a receipt describing a render nobody asked for — the same argument
/// `RenderJob::validate()` makes about an unknown vocabulary token, which is why that
/// rule survives a hop through QML.
inline constexpr std::array<std::string_view, 27> kJobKeys{"id",
                                                           "label",
                                                           "createdUtc",
                                                           "audioPath",
                                                           "audioSha256",
                                                           "durationSeconds",
                                                           "expectedFrames",
                                                           "outputPath",
                                                           "width",
                                                           "height",
                                                           "fps",
                                                           "videoCodec",
                                                           "pixelFormat",
                                                           "container",
                                                           "crf",
                                                           "encoderPreset",
                                                           "twoPass",
                                                           "hardwareAccel",
                                                           "gopSize",
                                                           "audioCodec",
                                                           "audioSampleRate",
                                                           "audioChannels",
                                                           "audioBitrateKbps",
                                                           "scenes",
                                                           "karaokeMode",
                                                           "karaokeAssPath",
                                                           "projectmVersion"};

/// The three fields of `RenderScene`, with the same refuse-don't-ignore rule.
inline constexpr std::array<std::string_view, 3> kSceneKeys{"presetName", "durationSeconds",
                                                            "crossfadeSeconds"};

/// 2^53 rather than `UINT64_MAX`: a QML number arrives as a `double`, and 2^53 is the
/// largest value that survives that hop exactly. At 60 fps it is 414 days of video,
/// so the cap is a documented limit rather than a practical one — and a documented
/// limit beats a silently rounded frame count, which would make the progress bar lie
/// about its own denominator.
constexpr double kExactDouble = 9007199254740992.0;

QVariantMap::const_iterator find(const QVariantMap& fields, const std::string_view name) {
    return fields.constFind(QLatin1String(name.data(), static_cast<qsizetype>(name.size())));
}

/// Present-but-wrong-type is refused, never coerced.
///
/// A `crf` of `"high"` must not become the string `high` and then fail some unrelated
/// numeric check three layers down with a message naming the wrong thing. The reported
/// `typeName()` is what makes the difference between "crf is a number" and "you wrote
/// `crf` as a string" visible to whoever has to fix the call site.
[[nodiscard]] std::optional<FieldRefusal> wrongType(const std::string_view name,
                                                    const QString& actual, const char* wanted) {
    return FieldRefusal{std::string(name), std::format("must be {}, got {}", wanted,
                                                       actual.isEmpty() ? std::string("nothing")
                                                                        : actual.toStdString())};
}

[[nodiscard]] std::optional<FieldRefusal>
readString(const QVariantMap& fields, const std::string_view name, std::string& into) {
    const auto it = find(fields, name);
    if (it == fields.constEnd()) return std::nullopt;
    // `typeId()` rather than a convenience predicate: Qt 6 has no `QVariant::isString`,
    // and an exact type check is what makes a number refused instead of stringified.
    if (it->typeId() != QMetaType::QString) return wrongType(name, it->typeName(), "a string");
    into = it->toString().toStdString();
    return std::nullopt;
}

[[nodiscard]] std::optional<FieldRefusal>
readPath(const QVariantMap& fields, const std::string_view name, vc::fs::path& into) {
    const auto it = find(fields, name);
    if (it == fields.constEnd()) return std::nullopt;
    if (it->typeId() != QMetaType::QString) return wrongType(name, it->typeName(), "a string");
    // `toStdString`, not `toStdWString`: every other bridge in this directory builds
    // an `fs::path` that way (AudioBridge.cpp:51, RecordingBridge.cpp:51), and a
    // `std::wstring` path on POSIX is a conversion the filesystem never asked for.
    into = vc::fs::path(it->toString().toStdString());
    return std::nullopt;
}

[[nodiscard]] std::optional<FieldRefusal> readDouble(const QVariantMap& fields,
                                                     const std::string_view name, double& into,
                                                     const double low, const double high) {
    const auto it = find(fields, name);
    if (it == fields.constEnd()) return std::nullopt;
    bool numeric = false;
    const double value = it->toDouble(&numeric);
    if (!numeric) return wrongType(name, it->typeName(), "a number");
    if (!std::isfinite(value) || value < low || value > high)
        return FieldRefusal{std::string(name),
                            std::format("must be a finite number in [{}, {}]", low, high)};
    into = value;
    return std::nullopt;
}

[[nodiscard]] std::optional<FieldRefusal> readU32(const QVariantMap& fields,
                                                  const std::string_view name, vc::u32& into) {
    const auto it = find(fields, name);
    if (it == fields.constEnd()) return std::nullopt;
    bool numeric = false;
    const double value = it->toDouble(&numeric);
    if (!numeric) return wrongType(name, it->typeName(), "a number");
    const double ceiling = static_cast<double>(std::numeric_limits<vc::u32>::max());
    if (!std::isfinite(value) || std::trunc(value) != value || value < 0.0 || value > ceiling)
        return FieldRefusal{std::string(name), std::format("must be a whole number in [0, {}]",
                                                           std::numeric_limits<vc::u32>::max())};
    into = static_cast<vc::u32>(value);
    return std::nullopt;
}

[[nodiscard]] std::optional<FieldRefusal> readFrames(const QVariantMap& fields,
                                                     const std::string_view name, vc::u64& into) {
    const auto it = find(fields, name);
    if (it == fields.constEnd()) return std::nullopt;
    bool numeric = false;
    const double value = it->toDouble(&numeric);
    if (!numeric) return wrongType(name, it->typeName(), "a number");
    if (!std::isfinite(value) || std::trunc(value) != value || value < 0.0 || value > kExactDouble)
        return FieldRefusal{std::string(name),
                            std::format("must be a whole number in [0, {}] -- QML has no 64-bit "
                                        "integer, so this surface cannot carry more",
                                        static_cast<unsigned long long>(kExactDouble))};
    into = static_cast<vc::u64>(value);
    return std::nullopt;
}

[[nodiscard]] std::optional<FieldRefusal> readBool(const QVariantMap& fields,
                                                   const std::string_view name, bool& into) {
    const auto it = find(fields, name);
    if (it == fields.constEnd()) return std::nullopt;
    // Exact type rather than `toBool()`: `QVariant("false").toBool()` is `true`, so a
    // stringly-typed caller would silently get the opposite of what it wrote.
    if (it->typeId() != QMetaType::Bool) return wrongType(name, it->typeName(), "a boolean");
    into = it->toBool();
    return std::nullopt;
}

[[nodiscard]] std::optional<FieldRefusal> readScenes(const QVariantMap& fields,
                                                     std::vector<vc::RenderScene>& into) {
    const auto it = find(fields, "scenes");
    if (it == fields.constEnd()) return std::nullopt;
    if (it->typeId() != QMetaType::QVariantList)
        return wrongType("scenes", it->typeName(), "a list of objects");

    std::vector<vc::RenderScene> scenes;
    int index = 0;
    for (const QVariant& value : it->toList()) {
        if (value.typeId() != QMetaType::QVariantMap)
            return FieldRefusal{std::format("scenes[{}]", index), "must be an object"};
        const QVariantMap source = value.toMap();
        for (auto key = source.constBegin(); key != source.constEnd(); ++key)
            if (std::ranges::find(kSceneKeys, key.key().toStdString()) == kSceneKeys.end())
                return FieldRefusal{std::format("scenes[{}].{}", index, key.key().toStdString()),
                                    "not a RenderScene field"};

        vc::RenderScene scene;
        // `presetName` and `durationSeconds` are read as required rather than
        // defaulted: leaving them empty would produce a scene that
        // `RenderJob::validate()` rejects with a message about a field the caller
        // never named, which is a worse diagnosis than "you left it out here".
        const auto preset = readString(source, "presetName", scene.presetName);
        if (preset)
            return FieldRefusal{std::format("scenes[{}].presetName", index), preset->message};
        const auto duration = readDouble(source, "durationSeconds", scene.durationSeconds, 0.0,
                                         std::numeric_limits<double>::max());
        if (duration)
            return FieldRefusal{std::format("scenes[{}].durationSeconds", index),
                                duration->message};
        const auto crossfade = readDouble(source, "crossfadeSeconds", scene.crossfadeSeconds, 0.0,
                                          std::numeric_limits<double>::max());
        if (crossfade)
            return FieldRefusal{std::format("scenes[{}].crossfadeSeconds", index),
                                crossfade->message};
        scenes.push_back(std::move(scene));
        ++index;
    }
    into = std::move(scenes);
    return std::nullopt;
}

/// Translate a QML map into a `RenderJob`, or explain the first key it could not read.
///
/// **This is a view of the job, not a second schema.** The distinction is the whole
/// reason it can exist at all:
///
///   * `RenderMode` plus `renderDuration` / `renderIncludeVideo` / `renderIncludeKaraoke`
///     were a second schema that could not *express* what `RenderJob` describes, so the
///     two drifted. This map is keyed by `RenderJob`'s own field names, is a strict
///     superset of what the old four arguments could say, and refuses what it does not
///     recognise.
///   * Every judgement about whether a job is renderable stays with
///     `RenderJob::validate()`, which `SunoWorkspace::startRender` calls itself. This
///     function can fail to *deliver* a value; it cannot hold an opinion about one.
///
/// `validate()` re-checks every field except `createdUtc`, `twoPass`, `gopSize` and
/// `expectedFrames` — so those four are range-checked here, which is the only place in
/// this file where a wrong read could survive to a rendered file rather than a refusal.
[[nodiscard]] std::expected<vc::RenderJob, FieldRefusal> jobFromMap(const QVariantMap& fields) {
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        if (std::ranges::find(kJobKeys, it.key().toStdString()) == kJobKeys.end())
            return std::unexpected(
                    FieldRefusal{it.key().toStdString(),
                                 "not a RenderJob field; a silently ignored key would render with "
                                 "RenderJob's default instead of the value you wrote"});

    vc::RenderJob job;
    std::optional<FieldRefusal> refused;

    // Each reader is total and independent: it either leaves the field at
    // `RenderJob`'s own default or records why it could not read it. Unstated fields
    // therefore stay defaults, which is why a caller that omits `fps` gets the
    // documented 60 and not a surprise.
    const auto use = [&refused](const std::optional<FieldRefusal>& reason) -> bool {
        if (!reason.has_value()) return true;
        refused = *reason;
        return false;
    };

    if (!use(readString(fields, "id", job.id))) return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "label", job.label))) return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "createdUtc", job.createdUtc)))
        return std::unexpected(std::move(*refused));
    if (!use(readPath(fields, "audioPath", job.audioPath)))
        return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "audioSha256", job.audioSha256)))
        return std::unexpected(std::move(*refused));
    if (!use(readDouble(fields, "durationSeconds", job.durationSeconds, 0.0,
                        std::numeric_limits<double>::max())))
        return std::unexpected(std::move(*refused));
    if (!use(readFrames(fields, "expectedFrames", job.expectedFrames)))
        return std::unexpected(std::move(*refused));
    if (!use(readPath(fields, "outputPath", job.outputPath)))
        return std::unexpected(std::move(*refused));
    if (!use(readU32(fields, "width", job.width))) return std::unexpected(std::move(*refused));
    if (!use(readU32(fields, "height", job.height))) return std::unexpected(std::move(*refused));
    if (!use(readU32(fields, "fps", job.fps))) return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "videoCodec", job.videoCodec)))
        return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "pixelFormat", job.pixelFormat)))
        return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "container", job.container)))
        return std::unexpected(std::move(*refused));
    if (!use(readU32(fields, "crf", job.crf))) return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "encoderPreset", job.encoderPreset)))
        return std::unexpected(std::move(*refused));
    if (!use(readBool(fields, "twoPass", job.twoPass))) return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "hardwareAccel", job.hardwareAccel)))
        return std::unexpected(std::move(*refused));
    if (!use(readU32(fields, "gopSize", job.gopSize))) return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "audioCodec", job.audioCodec)))
        return std::unexpected(std::move(*refused));
    if (!use(readU32(fields, "audioSampleRate", job.audioSampleRate)))
        return std::unexpected(std::move(*refused));
    if (!use(readU32(fields, "audioChannels", job.audioChannels)))
        return std::unexpected(std::move(*refused));
    if (!use(readU32(fields, "audioBitrateKbps", job.audioBitrateKbps)))
        return std::unexpected(std::move(*refused));
    if (!use(readScenes(fields, job.scenes))) return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "karaokeMode", job.karaokeMode)))
        return std::unexpected(std::move(*refused));
    if (!use(readPath(fields, "karaokeAssPath", job.karaokeAssPath)))
        return std::unexpected(std::move(*refused));
    if (!use(readString(fields, "projectmVersion", job.projectmVersion)))
        return std::unexpected(std::move(*refused));

    return job;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

SunoWorkspaceBridge::SunoWorkspaceBridge(QObject* parent) : QObject(parent) {
    // Owned by this bridge, and the reason it can never hold a render queue: nothing
    // hands one in, and `registerBridges` has no argument to pass one with. `setQueue`
    // is the seam, and taking a `RenderQueue` here (rather than constructing one) is
    // the larger change `BridgeRegistration.cpp` defers — it needs a queue in the
    // registration signature, not a convenience inside this constructor.
    workspace_ = new vc::suno::SunoWorkspace(this);
    lastErrorCache_ = QString::fromStdString(workspace_->state().lastError);

    connect(workspace_, &vc::suno::SunoWorkspace::regionAdded, this,
            &SunoWorkspaceBridge::onRegionAdded);
    connect(workspace_, &vc::suno::SunoWorkspace::regionRemoved, this,
            &SunoWorkspaceBridge::onRegionRemoved);
    connect(workspace_, &vc::suno::SunoWorkspace::regionsCleared, this,
            &SunoWorkspaceBridge::onRegionsCleared);
    connect(workspace_, &vc::suno::SunoWorkspace::lyrics_changed, this,
            &SunoWorkspaceBridge::onLyricsChanged);
    connect(workspace_, &vc::suno::SunoWorkspace::renderStarted, this,
            &SunoWorkspaceBridge::onRenderStarted);
    connect(workspace_, &vc::suno::SunoWorkspace::renderProgress, this,
            &SunoWorkspaceBridge::onRenderProgress);
    connect(workspace_, &vc::suno::SunoWorkspace::renderCompleted, this,
            &SunoWorkspaceBridge::onRenderCompleted);
    connect(workspace_, &vc::suno::SunoWorkspace::renderFailed, this,
            &SunoWorkspaceBridge::onRenderFailed);

    LOG_INFO("SunoWorkspaceBridge: ready (render queue attached: {})",
             renderQueueAttached() ? "yes" : "no");
}

// ─────────────────────────────────────────────────────────────────────────────
// Regions
// ─────────────────────────────────────────────────────────────────────────────

bool SunoWorkspaceBridge::addRegion(const QString& clipId, const double startS, const double endS,
                                    const QString& label) {
    const std::size_t before = workspace_->regions().size();
    workspace_->addRegion(clipId.toStdString(), startS, endS, label.toStdString());
    // Derived from the class's own state rather than from a second bounds check, so
    // this can only agree with it: an inverted range is refused there, so the count
    // does not move and the caller is told its row was not added.
    return workspace_->regions().size() != before;
}

bool SunoWorkspaceBridge::removeRegion(const int index) {
    // `SunoWorkspace::removeRegion` takes a `size_t` and ignores an out-of-range
    // index, so a negative QML int would become an enormous unsigned value and still
    // be ignored. Checking here is what makes the return value mean "your edit
    // landed" rather than "nothing threw".
    if (index < 0) return false;
    const auto position = static_cast<std::size_t>(index);
    if (position >= workspace_->regions().size()) return false;
    workspace_->removeRegion(position);
    return true;
}

bool SunoWorkspaceBridge::clearRegions() {
    const std::size_t before = workspace_->regions().size();
    workspace_->clearRegions();
    return workspace_->regions().size() != before;
}

QVariantList SunoWorkspaceBridge::regions() const {
    QVariantList list;
    const std::span<const vc::suno::ClipRegion> current = workspace_->regions();
    list.reserve(static_cast<qsizetype>(current.size()));
    for (const vc::suno::ClipRegion& region : current) {
        QVariantMap entry;
        // Snake case, matching the workspace JSON sidecar's keys, so a region loaded
        // from disk and a region added from QML are the same shape.
        entry["clip_id"] = QString::fromStdString(region.clipId);
        entry["start_s"] = region.startSeconds;
        entry["end_s"] = region.endSeconds;
        entry["label"] = QString::fromStdString(region.label);
        list.append(entry);
    }
    return list;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lyrics
// ─────────────────────────────────────────────────────────────────────────────

void SunoWorkspaceBridge::setLyricsText(const QString& text) {
    workspace_->setLyricsText(text.toStdString());
}

QString SunoWorkspaceBridge::lyricsText() const {
    // A `string_view`, so it is built from the bytes rather than through
    // `QString::fromStdString`, which takes a `std::string` and would copy the whole
    // lyric sheet to hand back something QML only reads.
    const std::string_view text = workspace_->lyricsText();
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Generation: refused
// ─────────────────────────────────────────────────────────────────────────────

QVariantMap SunoWorkspaceBridge::startGeneration(const QString& prompt, const QString& tags,
                                                 const bool instrumental, const QString& model) {
    // An empty `model` keeps the previous one, which is the class's documented
    // behaviour rather than a default this bridge chooses.
    const auto refused = workspace_->startGeneration(prompt.toStdString(), tags.toStdString(),
                                                     instrumental, model.toStdString());
    publishLastError();
    if (!refused) return refusalToQml(refused.error());
    // Unreachable in this build, and it is still handled: the whole point of a
    // return value is that the "ok" branch cannot be mistaken for the refusal branch
    // if the class ever grows a producer.
    return acceptedToQml();
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

QVariantMap SunoWorkspaceBridge::startRender(const QVariantMap& jobFields) {
    auto job = jobFromMap(jobFields);
    if (!job) {
        LOG_WARN("SunoWorkspaceBridge: render fields refused: {}", job.error().describe());
        // `lastError` is untouched here on purpose: the workspace was never asked, so
        // attributing a translation problem to it would be a false report.
        return fieldRefusalToQml(job.error());
    }
    const auto admitted = workspace_->startRender(std::move(*job));
    // `RenderQueue::enqueue` pumps, so a job can be started AND settled before
    // `startRender` returns -- every signal below may already have fired by the time
    // this line runs. `renderStateChanged` is emitted from `onRenderStarted` and again
    // from the settle, so a QML binding sees both transitions in order.
    publishLastError();
    if (!admitted) return refusalToQml(admitted.error());
    return acceptedToQml();
}

bool SunoWorkspaceBridge::cancelRender() { return workspace_->cancelRender(); }

bool SunoWorkspaceBridge::renderQueueAttached() const { return workspace_->queue() != nullptr; }

bool SunoWorkspaceBridge::renderActive() const { return workspace_->isRendering(); }

QString SunoWorkspaceBridge::renderJobId() const {
    const std::optional<std::string_view> id = workspace_->activeRenderJobId();
    if (!id.has_value()) return {};
    return QString::fromStdString(std::string(*id));
}

int SunoWorkspaceBridge::renderProgressPercent() const {
    // Read through, never clamped: a workspace with no live render holds -1, and that
    // is "unknown", not "0%".
    return workspace_->renderProgressPercent();
}

QString SunoWorkspaceBridge::lastError() const {
    return QString::fromStdString(workspace_->state().lastError);
}

void SunoWorkspaceBridge::publishLastError() {
    const QString current = lastError();
    if (current == lastErrorCache_) return;
    lastErrorCache_ = current;
    emit lastErrorChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Persistence
// ─────────────────────────────────────────────────────────────────────────────

QVariantMap SunoWorkspaceBridge::saveWorkspace(const QString& path) {
    // A save changes nothing a QML surface is showing, so it emits nothing: the only
    // honest announce here is the one the caller already has, the return value.
    const auto saved = workspace_->saveWorkspace(vc::fs::path(path.toStdString()));
    if (!saved) {
        LOG_ERROR("SunoWorkspaceBridge: save failed: {}", saved.error());
        QVariantMap map;
        map["ok"] = false;
        map["message"] = QString::fromStdString(saved.error());
        return map;
    }
    return acceptedToQml();
}

QVariantMap SunoWorkspaceBridge::loadWorkspace(const QString& path) {
    const auto loaded = workspace_->loadWorkspace(vc::fs::path(path.toStdString()));
    if (!loaded) {
        // Absent and corrupt are distinct errors on purpose, so "nothing was ever
        // saved" can never be reported as "the file you have is broken".
        LOG_ERROR("SunoWorkspaceBridge: load failed: {}", loaded.error());
        QVariantMap map;
        map["ok"] = false;
        map["message"] = QString::fromStdString(loaded.error());
        return map;
    }
    // A load replaces regions and lyrics wholesale and the class emits nothing for it
    // -- `regionAdded` fires per region on an *add*, and a restore is not that. These
    // two notifies are what make a restored workspace actually repaint.
    emit regionsChanged();
    emit lyricsChanged();
    return acceptedToQml();
}

// ─────────────────────────────────────────────────────────────────────────────
// Translation slots
// ─────────────────────────────────────────────────────────────────────────────

void SunoWorkspaceBridge::onRegionAdded(const vc::suno::ClipRegion& region) {
    emit regionsChanged();
}

void SunoWorkspaceBridge::onRegionRemoved(const std::size_t index) {
    // Connected now, and it was not before: the previous version forwarded
    // `regionAdded` and `regionsCleared` but not `regionRemoved`, so a QML list bound
    // to `regions` would have kept showing a region that was gone.
    emit regionsChanged();
}

void SunoWorkspaceBridge::onRegionsCleared() { emit regionsChanged(); }

void SunoWorkspaceBridge::onLyricsChanged(const std::string& text) { emit lyricsChanged(); }

void SunoWorkspaceBridge::onRenderStarted(const std::string& jobId) {
    LOG_INFO("SunoWorkspaceBridge: render {} started", jobId);
    emit renderStateChanged();
}

void SunoWorkspaceBridge::onRenderProgress(const int percent, const std::string& stage) {
    // Forwarded in the queue's own unit, -1 included. The workspace has already
    // applied the two corrections that matter (never backwards, and the literal 0 that
    // `RenderQueue::enqueue` hardcodes at admission for a job with no denominator), so
    // there is nothing to add here and nothing to clamp.
    emit renderProgressChanged(percent, QString::fromStdString(stage));
}

void SunoWorkspaceBridge::onRenderCompleted(const vc::fs::path& outputPath) {
    LOG_INFO("SunoWorkspaceBridge: render completed: {}", outputPath.string());
    // State first: the class has already cleared the job id by the time this slot runs,
    // so a binding that reads `renderActive` from `renderCompleted` sees the truth.
    emit renderStateChanged();
    publishLastError(); // the class clears lastError on completion
    emit renderCompleted(QString::fromStdString(outputPath.string()));
}

void SunoWorkspaceBridge::onRenderFailed(const std::string& error) {
    LOG_ERROR("SunoWorkspaceBridge: render failed: {}", error);
    emit renderStateChanged();
    publishLastError();
    emit renderFailed(QString::fromStdString(error));
}

} // namespace qml_bridge