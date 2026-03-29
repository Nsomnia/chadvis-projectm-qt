#include "SunoBridge.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/SunoModels.hpp"
#include <QQmlEngine>

namespace qml_bridge {

vc::suno::SunoController* SunoBridge::s_controller = nullptr;
SunoBridge* SunoBridge::s_instance = nullptr;

SunoBridge::SunoBridge(QObject* parent)
    : QObject(parent)
{
}

SunoBridge* SunoBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    if (!s_instance) {
        s_instance = new SunoBridge(qmlEngine);
    }
    return s_instance;
}

void SunoBridge::setSunoController(vc::suno::SunoController* controller)
{
    s_controller = controller;
}

void SunoBridge::connectSignals()
{
	if (s_controller && s_instance) {
		connect(s_controller, &vc::suno::SunoController::libraryUpdated,
			s_instance, [s = s_instance](const std::vector<vc::suno::SunoClip>& clips) {
				s->onLibraryUpdated(clips);
			}, Qt::QueuedConnection);
		connect(s_controller, &vc::suno::SunoController::clipUpdated,
			s_instance, [s = s_instance](const std::string& clipId) {
				s->onClipUpdated(clipId);
			}, Qt::QueuedConnection);
		connect(s_controller, &vc::suno::SunoController::statusMessage,
			s_instance, [s = s_instance](const std::string& msg) {
				s->onStatusMessage(msg);
			}, Qt::QueuedConnection);
	}
}

bool SunoBridge::isAuthenticated() const
{
    return s_controller && s_controller->client() &&
           s_controller->client()->isAuthenticated();
}

bool SunoBridge::isSyncing() const
{
    return isSyncing_;
}

QVariantList SunoBridge::clips() const
{
    if (!s_controller) return {};

    QVariantList result;
    for (const auto& clip : s_controller->clips()) {
        result.append(clipToVariant(clip));
    }
    return result;
}

int SunoBridge::totalClips() const
{
    return s_controller ? static_cast<int>(s_controller->clips().size()) : 0;
}

QString SunoBridge::statusMessage() const
{
    return statusMessage_;
}

QString SunoBridge::searchQuery() const
{
    return searchQuery_;
}

QVariantList SunoBridge::searchResults() const
{
    return searchResults_;
}

void SunoBridge::setSearchQuery(const QString& query)
{
    if (searchQuery_ != query) {
        searchQuery_ = query;
        updateSearchResults();
        emit searchQueryChanged();
    }
}

void SunoBridge::authenticate()
{
	if (s_controller) {
		s_controller->requestAuthentication();
	}
}

void SunoBridge::signOut()
{
    emit authChanged();
}

void SunoBridge::refreshLibrary(int page)
{
    if (s_controller) {
        isSyncing_ = true;
        emit syncingChanged();
        s_controller->refreshLibrary(page);
    }
}

void SunoBridge::syncDatabase()
{
    if (s_controller) {
        isSyncing_ = true;
        emit syncingChanged();
        s_controller->syncDatabase();
    }
}

void SunoBridge::downloadAndPlay(const QString& clipId)
{
    if (!s_controller) return;

    for (const auto& clip : s_controller->clips()) {
        if (clip.id == clipId.toStdString()) {
            s_controller->downloadAndPlay(clip);
            break;
        }
    }
}

QVariantMap SunoBridge::getClip(const QString& clipId) const
{
    if (!s_controller) return {};

    for (const auto& clip : s_controller->clips()) {
        if (clip.id == clipId.toStdString()) {
            return clipToVariant(clip);
        }
    }
    return {};
}

bool SunoBridge::hasLyrics(const QString& clipId) const
{
    return s_controller && s_controller->hasLyrics(clipId.toStdString());
}

void SunoBridge::fetchLyrics(const QString& clipId)
{
    if (s_controller) {
        s_controller->getLyrics(clipId.toStdString());
    }
}

void SunoBridge::onLibraryUpdated(const std::vector<vc::suno::SunoClip>& clips)
{
    Q_UNUSED(clips)
    isSyncing_ = false;
    emit syncingChanged();
    emit clipsChanged();
}

void SunoBridge::onClipUpdated(const std::string& clipId)
{
    Q_UNUSED(clipId)
    emit clipsChanged();
}

void SunoBridge::onStatusMessage(const std::string& message)
{
    statusMessage_ = QString::fromStdString(message);
    emit statusChanged();
}

QVariantMap SunoBridge::clipToVariant(const vc::suno::SunoClip& clip) const
{
    QVariantMap map;
    map[QStringLiteral("id")] = QString::fromStdString(clip.id);
    map[QStringLiteral("title")] = QString::fromStdString(clip.title);
    map[QStringLiteral("audioUrl")] = QString::fromStdString(clip.audio_url);
    map[QStringLiteral("imageUrl")] = QString::fromStdString(clip.image_url);
    map[QStringLiteral("imageLargeUrl")] = QString::fromStdString(clip.image_large_url);
    map[QStringLiteral("displayName")] = QString::fromStdString(clip.display_name);
    map[QStringLiteral("handle")] = QString::fromStdString(clip.handle);
    map[QStringLiteral("isLiked")] = clip.is_liked;
    map[QStringLiteral("isPublic")] = clip.is_public;
    map[QStringLiteral("createdAt")] = QString::fromStdString(clip.created_at);
    map[QStringLiteral("status")] = QString::fromStdString(clip.status);
    map[QStringLiteral("isStem")] = clip.isStem();
    map[QStringLiteral("prompt")] = QString::fromStdString(clip.metadata.prompt);
    map[QStringLiteral("tags")] = QString::fromStdString(clip.metadata.tags);
    map[QStringLiteral("duration")] = QString::fromStdString(clip.metadata.duration);
    map[QStringLiteral("bpm")] = QString::fromStdString(clip.metadata.bpm);
    map[QStringLiteral("key")] = QString::fromStdString(clip.metadata.key);
    map[QStringLiteral("isInstrumental")] = clip.metadata.make_instrumental;
    return map;
}

void SunoBridge::updateSearchResults()
{
    if (!s_controller || searchQuery_.isEmpty()) {
        searchResults_.clear();
        emit searchResultsChanged();
        return;
    }

    QString lowerQuery = searchQuery_.toLower();
    searchResults_.clear();

    for (const auto& clip : s_controller->clips()) {
        QString title = QString::fromStdString(clip.title).toLower();
        QString displayName = QString::fromStdString(clip.display_name).toLower();
        QString prompt = QString::fromStdString(clip.metadata.prompt).toLower();

        if (title.contains(lowerQuery) ||
            displayName.contains(lowerQuery) ||
            prompt.contains(lowerQuery)) {
            searchResults_.append(clipToVariant(clip));
        }
    }
    emit searchResultsChanged();
}

} // namespace qml_bridge
