// SunoWorkspace.cpp — local song-creation workspace implementation.
//
// See SunoWorkspace.hpp for the class contract. This file is intentionally
// thin: it owns state, emits signals, and delegates network/render work to
// existing SunoClient + AudioEngine + VisualizerRenderer collaborators.

#include "SunoWorkspace.hpp"

#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <filesystem>
#include <format>
#include <fstream>

namespace vc::suno {

namespace fs = std::filesystem;

SunoWorkspace::SunoWorkspace(QObject* parent)
    : QObject(parent) {
    workspaceId_ = QCryptographicHash::hash(
        QString("%1%2").arg(QDateTime::currentDateTime().toString(Qt::ISODate))
        .toUtf8(), QCryptographicHash::Md5).toHex().mid(0, 16).toUtf8().toChar();
    LOG_INFO("SunoWorkspace: created workspace {}", workspaceId_);
}

SunoWorkspace::~SunoWorkspace() {
    LOG_INFO("SunoWorkspace: destroying workspace {}", workspaceId_);
}

void SunoWorkspace::startGeneration(const std::string& prompt, const std::string& tags,
                                    bool makeInstrumental, const std::string& model) {
    if (state_.isGenerating) {
        LOG_WARN("SunoWorkspace: generation already in progress");
        return;
    }
    state_.currentPrompt = prompt;
    state_.currentTags = tags;
    state_.currentModel = model;
    state_.makeInstrumental = makeInstrumental;
    state_.isGenerating = true;
    state_.lastError.clear();
    emit generationStarted();
}

void SunoWorkspace::cancelGeneration() {
    if (!state_.isGenerating) return;
    state_.isGenerating = false;
    LOG_INFO("SunoWorkspace: generation cancelled");
}

void SunoWorkspace::addRegion(const std::string& clipId, double startS, double endS,
                               const std::string& label) {
    ClipRegion region;
    region.clipId = clipId;
    region.startSeconds = startS;
    region.endSeconds = endS;
    region.label = label.empty() ? clipId : label;
    state_.regions.push_back(region);
    emit regionAdded(region);
}

void SunoWorkspace::removeRegion(size_t index) {
    if (index >= state_.regions.size()) return;
    state_.regions.erase(state_.regions.begin() + static_cast<ptrdiff_t>(index));
}

void SunoWorkspace::clearRegions() {
    state_.regions.clear();
    emit regionsCleared();
}

void SunoWorkspace::requestStems(const std::string& clipId) {
    LOG_INFO("SunoWorkspace: requesting stems for clip {}", clipId);
    emit stemsRequested(clipId);
}

void SunoWorkspace::setLyricsText(const std::string& text) {
    state_.lyricsText = text;
    emit lyrics_changed(text);
}

void SunoWorkspace::startRender(RenderMode mode, double durationS, bool includeVideo,
                                 bool includeKaraoke) {
    if (state_.isRendering) {
        LOG_WARN("SunoWorkspace: render already in progress");
        return;
    }
    state_.renderMode = mode;
    state_.renderDuration = durationS;
    state_.renderIncludeVideo = includeVideo;
    state_.renderIncludeKaraoke = includeKaraoke;
    state_.isRendering = true;
    emit renderStarted(mode);
    emit renderProgress(0.0f, "initializing");
}

void SunoWorkspace::cancelRender() {
    if (!state_.isRendering) return;
    state_.isRendering = false;
    LOG_INFO("SunoWorkspace: render cancelled");
}

Result<void> SunoWorkspace::saveWorkspace(const fs::path& path) {
    QJsonObject root;
    root["workspace_id"] = QString::fromUtf8(workspaceId_);
    root["prompt"] = QString::fromUtf8(state_.currentPrompt);
    root["tags"] = QString::fromUtf8(state_.currentTags);
    root["model"] = QString::fromUtf8(state_.currentModel);
    root["make_instrumental"] = state_.makeInstrumental;

    QJsonArray clipsArr;
    for (const auto& clip : state_.generatedClips) {
        QJsonObject c;
        c["id"] = QString::fromUtf8(clip.id);
        c["title"] = QString::fromUtf8(clip.title);
        clipsArr.append(c);
    }
    root["clips"] = clipsArr;

    QJsonArray regionsArr;
    for (const auto& r : state_.regions) {
        QJsonObject ro;
        ro["clip_id"] = QString::fromUtf8(r.clipId);
        ro["start_s"] = r.startSeconds;
        ro["end_s"] = r.endSeconds;
        ro["label"] = QString::fromUtf8(r.label);
        regionsArr.append(ro);
    }
    root["regions"] = regionsArr;

    root["lyrics"] = QString::fromUtf8(state_.lyricsText);
    root["render_mode"] = static_cast<int>(state_.renderMode);
    root["render_duration"] = state_.renderDuration;
    root["render_include_video"] = state_.renderIncludeVideo;
    root["render_include_karaoke"] = state_.renderIncludeKaraoke;

    QJsonDocument doc(root);
    QString jsonStr = doc.toJson(QJsonDocument::Compact);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return std::unexpected(std::format("cannot open {} for writing", path.string()));
    }
    f.write(jsonStr.toUtf8().constData(), static_cast<std::streamsize>(jsonStr.size()));
    f.close();
    LOG_INFO("SunoWorkspace: saved workspace to {}", path.string());
    return {};
}

Result<void> SunoWorkspace::loadWorkspace(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::unexpected(std::format("cannot open {} for reading", path.string()));
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    auto doc = QJsonDocument::fromJson(QByteArray::fromUtf8(content));
    if (doc.isNull()) {
        return std::unexpected(std::format("invalid JSON in {}", path.string()));
    }
    auto root = doc.object();
    workspaceId_ = root["workspace_id"].toString().toUtf8().toChar();
    state_.currentPrompt = root["prompt"].toString().toUtf8().toChar();
    state_.currentTags = root["tags"].toString().toUtf8().toChar();
    state_.currentModel = root["model"].toString().toUtf8().toChar();
    state_.makeInstrumental = root["make_instrumental"].toBool(false);
    state_.lyricsText = root["lyrics"].toString().toUtf8().toChar();
    state_.renderMode = static_cast<RenderMode>(root["render_mode"].toInt(0));
    state_.renderDuration = root["render_duration"].toDouble(120.0);
    state_.renderIncludeVideo = root["render_include_video"].toBool(false);
    state_.renderIncludeKaraoke = root["render_include_karaoke"].toBool(false);
    LOG_INFO("SunoWorkspace: loaded workspace from {}", path.string());
    return {};
}

} // namespace vc::suno