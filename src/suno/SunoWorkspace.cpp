// SunoWorkspace.cpp — local creation state, and the render submission half.
//
// See SunoWorkspace.hpp for why this class is smaller than it was and what each
// surviving member's real producer is. Two rules govern the file:
//
//   * Nothing here invents a frame, a tick, a percentage, or a result. Every
//     render signal is a translation of a `RenderQueue` decision.
//   * Every refusal is a return value. Nothing is broadcast to "whoever is
//     listening", because that pattern has already produced one phantom-failure
//     bug elsewhere in this tree.

#include "SunoWorkspace.hpp"

#include "core/Logger.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <format>
#include <fstream>
#include <utility>

namespace vc::suno {

// ─────────────────────────────────────────────────────────────────────────────
// Refusals
// ─────────────────────────────────────────────────────────────────────────────

std::string_view workspaceRefusalKindName(const WorkspaceRefusalKind kind) noexcept {
    switch (kind) {
        case WorkspaceRefusalKind::NoQueueAttached:
            return "no-queue-attached";
        case WorkspaceRefusalKind::JobRejected:
            return "job-rejected";
        case WorkspaceRefusalKind::RenderAlreadyActive:
            return "render-already-active";
        case WorkspaceRefusalKind::GenerationUnavailable:
            return "generation-unavailable";
    }
    return "unknown";
}

std::string WorkspaceRefusal::describe() const {
    return std::format("{} ({}): {}", workspaceRefusalKindName(kind), subject, message);
}

namespace {

WorkspaceRefusal makeRefusal(const WorkspaceRefusalKind kind, std::string subject,
                             std::string message) {
    return WorkspaceRefusal{kind, std::move(subject), std::move(message)};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

SunoWorkspace::SunoWorkspace(QObject* parent) : QObject(parent) {
    // Per-session identity, derived from the wall clock because it is a display
    // label for a workspace file and nothing depends on it being unpredictable.
    workspaceId_ = QCryptographicHash::hash(
                           QString("%1%2")
                                   .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                                   .toUtf8(),
                           QCryptographicHash::Md5)
                           .toHex()
                           .mid(0, 16)
                           .toStdString();
    LOG_INFO("SunoWorkspace: created workspace {}", workspaceId_);
}

SunoWorkspace::~SunoWorkspace() {
    // A live render outliving this object would deliver its terminal state to a
    // destroyed receiver, and the queue's `jobStateChanged` is a direct connection
    // by default — so the id is cleared here and the queue is left to settle the
    // job into its own batch array, which is where its receipt and note live
    // anyway. Nothing is cancelled: cancelling here would report an outcome the
    // queue has not decided.
    if (activeRenderId_.has_value()) {
        LOG_WARN("SunoWorkspace: destroyed with job {} still in flight; the queue keeps "
                 "the verdict",
                 *activeRenderId_);
    }
    LOG_INFO("SunoWorkspace: destroying workspace {}", workspaceId_);
}

void SunoWorkspace::setQueue(RenderQueue* queue) noexcept {
    if (queue_ == queue) return;
    // Re-pointing while a render is live would silently stop reporting it, so the
    // refusal is the same one `startRender` would give and the old queue is left
    // to finish what it started.
    if (activeRenderId_.has_value()) {
        LOG_WARN("SunoWorkspace: refusing to re-point the render queue while job {} is "
                 "in flight",
                 *activeRenderId_);
        return;
    }
    // Explicitly disconnect rather than relying on `UniqueConnection`: the queue is
    // a singleton in practice, so A→B→A would otherwise leave one connection to A
    // and then add a second, and every verdict would be reported twice.
    if (queue_ != nullptr) {
        QObject::disconnect(queue_, &RenderQueue::jobStateChanged, this,
                            &SunoWorkspace::onQueueStateChanged);
    }
    queue_ = queue;
    if (queue_ != nullptr) {
        QObject::connect(queue_, &RenderQueue::jobStateChanged, this,
                         &SunoWorkspace::onQueueStateChanged);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Generation: recorded, then refused
// ─────────────────────────────────────────────────────────────────────────────

std::expected<void, WorkspaceRefusal> SunoWorkspace::startGeneration(const std::string& prompt,
                                                                     const std::string& tags,
                                                                     const bool makeInstrumental,
                                                                     const std::string& model) {
    // The authored text is real local state and `saveWorkspace` persists it, so
    // the user does not lose their prompt to a refusal. Nothing after this line
    // pretends to act on it.
    state_.currentPrompt = prompt;
    state_.currentTags = tags;
    state_.makeInstrumental = makeInstrumental;
    if (!model.empty()) state_.currentModel = model;

    const WorkspaceRefusal refusal = makeRefusal(
            WorkspaceRefusalKind::GenerationUnavailable, "POST /api/gen",
            "generation is not wired in this client: SunoClient has no generation method, "
            "and whether one is ever added is a human decision because the captcha "
            "provider behind /api/c/check is uncaptured. Author suno.com in a browser "
            "and import the finished clips from the library instead");
    state_.lastError = refusal.describe();
    LOG_INFO("SunoWorkspace: generation refused, prompt recorded ({} chars): {}", prompt.size(),
             state_.lastError);
    return std::unexpected(refusal);
}

// ─────────────────────────────────────────────────────────────────────────────
// Regions
// ─────────────────────────────────────────────────────────────────────────────

void SunoWorkspace::addRegion(const std::string& clipId, double startS, const double endS,
                              const std::string& label) {
    if (!(startS < endS)) {
        // Silently accepting an empty or inverted region adds a row to a mashup
        // timeline that no amount of rendering can use.
        LOG_WARN("SunoWorkspace: refusing region on {} with start {} >= end {}", clipId, startS,
                 endS);
        return;
    }
    ClipRegion region;
    region.clipId = clipId;
    region.startSeconds = startS;
    region.endSeconds = endS;
    region.label = label.empty() ? clipId : label;
    state_.regions.push_back(region);
    emit regionAdded(region);
}

void SunoWorkspace::removeRegion(const size_t index) {
    if (index >= state_.regions.size()) return;
    state_.regions.erase(state_.regions.begin() + static_cast<ptrdiff_t>(index));
    emit regionRemoved(index);
}

void SunoWorkspace::clearRegions() {
    if (state_.regions.empty()) return;
    state_.regions.clear();
    emit regionsCleared();
}

std::span<const ClipRegion> SunoWorkspace::regions() const {
    return std::span<const ClipRegion>(state_.regions);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lyrics
// ─────────────────────────────────────────────────────────────────────────────

void SunoWorkspace::setLyricsText(const std::string& text) {
    if (state_.lyricsText == text) return;
    state_.lyricsText = text;
    emit lyrics_changed(text);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

std::expected<void, WorkspaceRefusal> SunoWorkspace::startRender(RenderJob job) {
    if (queue_ == nullptr) {
        const WorkspaceRefusal refusal = makeRefusal(
                WorkspaceRefusalKind::NoQueueAttached, "render queue",
                "no RenderQueue is attached, so a render has nowhere to be scheduled. "
                "Wire one in with setQueue(); a workspace with no queue accepts the job "
                "and then reports a stall with no cause");
        state_.lastError = refusal.describe();
        LOG_WARN("SunoWorkspace: render refused: {}", state_.lastError);
        return std::unexpected(refusal);
    }
    if (activeRenderId_.has_value()) {
        const WorkspaceRefusal refusal =
                makeRefusal(WorkspaceRefusalKind::RenderAlreadyActive, *activeRenderId_,
                            "this workspace submits one render at a time; wait for it to settle or "
                            "cancel it");
        state_.lastError = refusal.describe();
        LOG_WARN("SunoWorkspace: render refused: {}", state_.lastError);
        return std::unexpected(refusal);
    }

    // A job that could never be admitted must not announce a start. `renderStarted`
    // carries the stronger invariant — when it fires, a render IS live — which is
    // what makes it safe for a UI to spin on and is the whole difference from the
    // `renderProgress(0.0f, "initializing")` that never followed anything.
    // `RenderJob::validate()` is pure and `enqueue` calls it anyway, so this is a
    // second look at the same authority rather than a second opinion.
    if (auto invalid = job.validate(); !invalid) {
        const WorkspaceRefusal refusal = makeRefusal(
                WorkspaceRefusalKind::JobRejected, invalid.error().field, invalid.error().message);
        state_.lastError = refusal.describe();
        LOG_WARN("SunoWorkspace: render refused before it was live: {}", state_.lastError);
        return std::unexpected(refusal);
    }

    // Everything below happens BEFORE `enqueue`, because `enqueue` pumps: the job
    // is handed out, and may even be settled, before `enqueue` returns. A flag set
    // after that call would miss its own verdict.
    activeRenderId_ = job.id;
    activeRenderOutput_ = job.outputPath;
    activeRenderExpectedFrames_ = job.expectedFrames;
    lastRenderPercent_ = -1;
    state_.lastError.clear();
    emit renderStarted(job.id);

    auto admitted = queue_->enqueue(std::move(job));
    if (admitted) return {};

    // Reachable only for state the queue owns and `validate()` cannot see — today
    // exactly one thing, a duplicate id. The render *was* live and is now not, so
    // the verdict is reported rather than dropped: nothing else will report it, and
    // dropping it is what strands a flag as `Rendering`.
    settleFailed(admitted.error().describe());
    return std::unexpected(makeRefusal(WorkspaceRefusalKind::JobRejected, admitted.error().field,
                                       admitted.error().message));
}

bool SunoWorkspace::cancelRender() {
    if (!activeRenderId_.has_value()) return false;
    // No state change here. The queue settles a queued job immediately and leaves
    // a running one to the worker, and in both cases `onQueueStateChanged` is what
    // clears the id. Inventing a terminal state here would make `isRendering()`
    // claim something the queue has not decided.
    return queue_ != nullptr && queue_->cancel(*activeRenderId_);
}

void SunoWorkspace::onQueueStateChanged(const QString& jobId, const int state, const int percent) {
    if (!activeRenderId_.has_value()) return;
    const std::string id = *activeRenderId_;
    if (jobId.toStdString() != id) return; // a foreign job, not ours

    const auto outcome = static_cast<RenderState>(state);
    // `renderStateName`, not the enum: there is no `std::formatter<RenderState>`,
    // and the state has to be spelled for a human reading a failure.
    const std::string outcomeName(renderStateName(outcome));
    publishProgress(percent, outcomeName);
    if (!isTerminal(outcome)) return;

    if (outcome == RenderState::Completed) {
        activeRenderId_.reset();
        state_.lastError.clear();
        emit renderCompleted(activeRenderOutput_);
        return;
    }

    // Every terminal state except `Completed` is a failure **here**, and the reason
    // is `RenderJob`'s own `leavesPartialOutput()`: it is true for
    // CompletedPartial, FailedRetryable, FailedPermanent *and* Cancelled, because a
    // cancelled render had already been muxing frames when it stopped. A 40-second
    // file must never read as a completed 3-minute one — that is the exact
    // "plausible-looking short file" failure this repo has already shipped once.
    activeRenderOutput_.clear();
    const std::string note = queuedNote(id);
    settleFailed(note.empty() ? std::format("render {} ended as {} with no recorded reason", id,
                                            outcomeName)
                              : std::format("render {} ended as {}: {}", id, outcomeName, note));
}

void SunoWorkspace::publishProgress(const int percent, const std::string& stage) {
    // An unknown denominator is -1, and -1 is passed straight through: clamping it
    // to 0 would claim the render is 0% done rather than that it cannot be measured.
    //
    // The one correction is for `RenderQueue::enqueue`, which hardcodes a literal
    // `0` at the admission emit (RenderQueue.cpp:195) instead of asking
    // `RenderResult::progressPercent()`, which returns -1 for a job with no
    // `expectedFrames`. Every other report for such a job is -1, so accepting that
    // one 0 would leave a permanent "0%" on a render that cannot be measured.
    int reported = percent;
    if (activeRenderExpectedFrames_ == 0 && reported >= 0) reported = -1;

    if (reported >= 0) {
        lastRenderPercent_ = std::max(lastRenderPercent_, reported);
    } else {
        lastRenderPercent_ = reported;
    }
    emit renderProgress(lastRenderPercent_, stage);
}

void SunoWorkspace::settleFailed(std::string reason) {
    activeRenderId_.reset();
    state_.lastError = reason;
    LOG_WARN("SunoWorkspace: render failed: {}", reason);
    emit renderFailed(reason);
}

std::string SunoWorkspace::queuedNote(const std::string& jobId) const {
    if (queue_ == nullptr) return {};
    // `RenderQueue::job()` is erased by the settle that led here, so the batch
    // array is the only surviving record — and the note is the entire value of a
    // failure report, so a refusal with an empty message is a defect, not a style.
    for (const RenderResult& result : queue_->results()) {
        if (result.jobId == jobId) return result.note;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Persistence
// ─────────────────────────────────────────────────────────────────────────────

std::expected<void, std::string> SunoWorkspace::saveWorkspace(const fs::path& path) {
    QJsonObject root;
    root["workspace_id"] = QString::fromUtf8(workspaceId_);
    root["prompt"] = QString::fromUtf8(state_.currentPrompt);
    root["tags"] = QString::fromUtf8(state_.currentTags);
    root["model"] = QString::fromUtf8(state_.currentModel);
    root["make_instrumental"] = state_.makeInstrumental;
    root["lyrics"] = QString::fromUtf8(state_.lyricsText);

    QJsonArray regionsArr;
    for (const ClipRegion& region : state_.regions) {
        QJsonObject entry;
        entry["clip_id"] = QString::fromUtf8(region.clipId);
        entry["start_s"] = region.startSeconds;
        entry["end_s"] = region.endSeconds;
        entry["label"] = QString::fromUtf8(region.label);
        regionsArr.append(entry);
    }
    root["regions"] = regionsArr;

    // `version` is written so a future reader can tell this file from one written
    // by the previous 12-signal shape, which persisted render parameters that
    // `RenderJob` now owns. Those keys are read-and-ignored on load rather than
    // rejected: refusing to open an old workspace would lose the user's regions.
    root["version"] = 2;

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(std::format("cannot open {} for writing", path.string()));
    }
    out.write(bytes.constData(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        // Without this a short write reports success, and the failure surfaces
        // later as "my regions vanished" with nothing pointing here.
        return std::unexpected(std::format("failed while writing {}", path.string()));
    }
    out.close();
    LOG_INFO("SunoWorkspace: saved workspace {} ({} regions) to {}", workspaceId_,
             state_.regions.size(), path.string());
    return {};
}

std::expected<void, std::string> SunoWorkspace::loadWorkspace(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // Distinct from the parse failure below so "nothing was ever saved" can
        // never be reported as "the file you have is broken".
        return std::unexpected(std::format("cannot open {} for reading", path.string()));
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(content));
    if (doc.isNull() || !doc.isObject()) {
        return std::unexpected(std::format("invalid workspace JSON in {}", path.string()));
    }

    const QJsonObject root = doc.object();
    // `toString()` on an absent key yields a null string, which would blank a
    // field the file simply does not carry. `defaultValue` keeps an older or
    // hand-edited file from wiping state on a partial round trip.
    const auto text = [&root](const char* key, const std::string& fallback) {
        const QJsonValue value = root.value(QLatin1String(key));
        return value.isString() ? value.toString().toStdString() : fallback;
    };

    WorkspaceState loaded;
    loaded.currentPrompt = text("prompt", state_.currentPrompt);
    loaded.currentTags = text("tags", state_.currentTags);
    loaded.currentModel = text("model", state_.currentModel);
    loaded.lyricsText = text("lyrics", state_.lyricsText);
    loaded.makeInstrumental = root.value("make_instrumental").toBool(state_.makeInstrumental);

    loaded.regions.clear();
    for (const QJsonValue& value : root.value("regions").toArray()) {
        const QJsonObject entry = value.toObject();
        ClipRegion region;
        region.clipId = entry.value("clip_id").toString().toStdString();
        region.startSeconds = entry.value("start_s").toDouble();
        region.endSeconds = entry.value("end_s").toDouble();
        region.label = entry.value("label").toString().toStdString();
        if (region.clipId.empty() || !(region.startSeconds < region.endSeconds)) {
            LOG_WARN("SunoWorkspace: dropping an unusable region from {}", path.string());
            continue;
        }
        loaded.regions.push_back(std::move(region));
    }

    state_ = std::move(loaded);
    // An id is regenerated per session, so a loaded file keeps this session's id
    // rather than adopting a stale one — otherwise two live workspaces could claim
    // the same file.
    LOG_INFO("SunoWorkspace: loaded {} regions from {}", state_.regions.size(), path.string());
    return {};
}

} // namespace vc::suno