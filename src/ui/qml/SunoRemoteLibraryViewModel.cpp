#include "ui/qml/SunoRemoteLibraryViewModel.hpp"

#include <QMetaObject>
#include <QThread>

#include <algorithm>

#include "ui/controllers/SunoController.hpp"

namespace vc::ui::qml {

namespace {

QString clipTitle(const vc::suno::SunoClip& clip) {
    if (!clip.title.empty()) {
        return QString::fromStdString(clip.title);
    }

    return QString::fromStdString(clip.id);
}

QString clipArtist(const vc::suno::SunoClip& clip) {
    if (!clip.display_name.empty()) {
        return QString::fromStdString(clip.display_name);
    }

    if (!clip.handle.empty()) {
        return QString::fromStdString(clip.handle);
    }

    return QStringLiteral("Suno Artist");
}

} // namespace

SunoRemoteLibraryViewModel::SunoRemoteLibraryViewModel(vc::suno::SunoController* controller, QObject* parent)
    : QObject(parent)
    , controller_(controller)
    , model_(this) {
    if (!controller_) {
        setStatusMessage(QStringLiteral("Suno controller unavailable"));
        return;
    }

    bindControllerSignals();

    allClips_ = controller_->clips();
    applyFilter();
}

SunoRemoteLibraryViewModel::~SunoRemoteLibraryViewModel() {
    if (!controller_) {
        return;
    }

    if (libraryUpdatedConnection_) {
        controller_->libraryUpdated.disconnect(*libraryUpdatedConnection_);
    }

    if (clipUpdatedConnection_) {
        controller_->clipUpdated.disconnect(*clipUpdatedConnection_);
    }

    if (statusMessageConnection_) {
        controller_->statusMessage.disconnect(*statusMessageConnection_);
    }
}

int SunoRemoteLibraryViewModel::clipCount() const {
    return model_.rowCount();
}

int SunoRemoteLibraryViewModel::totalClipCount() const {
    return static_cast<int>(allClips_.size());
}

int SunoRemoteLibraryViewModel::lyricsReadyCount() const {
    if (!controller_) {
        return 0;
    }

    int count = 0;
    for (const auto& clip : allClips_) {
        if (controller_->hasLyrics(clip.id)) {
            ++count;
        }
    }

    return count;
}

void SunoRemoteLibraryViewModel::refresh() {
    if (!controller_) {
        return;
    }

    setSyncing(true);
    setStatusMessage(QStringLiteral("Syncing Suno library..."));
    controller_->refreshLibrary();
}

void SunoRemoteLibraryViewModel::authenticate() {
    if (!controller_) {
        return;
    }

    setSyncing(true);
    setStatusMessage(QStringLiteral("Opening Suno authentication flow..."));
    controller_->syncDatabase(true);
}

void SunoRemoteLibraryViewModel::playClipAt(int row) {
    const auto* clip = model_.clipAt(row);
    if (!clip) {
        return;
    }

    playClipById(clip->id);
}

void SunoRemoteLibraryViewModel::playClipById(const QString& clipId) {
    if (!controller_) {
        return;
    }

    const auto id = clipId.toStdString();
    const auto it = std::find_if(allClips_.begin(), allClips_.end(), [&id](const auto& clip) {
        return clip.id == id;
    });

    if (it == allClips_.end()) {
        setStatusMessage(QStringLiteral("Selected Suno clip no longer in cache"));
        return;
    }

    controller_->downloadAndPlay(*it);
    setStatusMessage(QStringLiteral("Queueing Suno clip: %1").arg(clipTitle(*it)));
}

void SunoRemoteLibraryViewModel::setSearchQuery(const QString& query) {
    if (searchQuery_ == query) {
        return;
    }

    searchQuery_ = query;
    emit searchQueryChanged();
    applyFilter();
}

void SunoRemoteLibraryViewModel::bindControllerSignals() {
    libraryUpdatedConnection_ = controller_->libraryUpdated.connect([this](const auto& clips) {
        invokeOnUiThread([this, clips] {
            allClips_ = clips;
            applyFilter();
            setSyncing(false);
            setStatusMessage(QStringLiteral("Suno library updated (%1 clips)").arg(static_cast<int>(clips.size())));
            emit totalClipCountChanged();
            emit lyricsReadyCountChanged();
        });
    });

    clipUpdatedConnection_ = controller_->clipUpdated.connect([this](const std::string&) {
        invokeOnUiThread([this] {
            applyFilter();
            emit lyricsReadyCountChanged();
        });
    });

    statusMessageConnection_ = controller_->statusMessage.connect([this](const std::string& message) {
        invokeOnUiThread([this, message] {
            const auto messageText = QString::fromStdString(message);
            setStatusMessage(messageText);

            const auto lowered = messageText.toLower();
            if (lowered.contains(QStringLiteral("error")) || lowered.contains(QStringLiteral("failed")) ||
                lowered.contains(QStringLiteral("done"))) {
                setSyncing(false);
            }
        });
    });
}

void SunoRemoteLibraryViewModel::applyFilter() {
    std::vector<SunoClipListModel::ClipRow> rows;
    rows.reserve(allClips_.size());

    const auto query = searchQuery_.trimmed().toLower();

    for (const auto& clip : allClips_) {
        const auto title = clipTitle(clip);
        const auto artist = clipArtist(clip);
        const auto tags = QString::fromStdString(clip.metadata.tags);
        const auto status = QString::fromStdString(clip.status.empty() ? "ready" : clip.status);
        const auto duration = QString::fromStdString(clip.metadata.duration);
        const auto created = QString::fromStdString(clip.created_at);

        if (!query.isEmpty()) {
            const auto searchable = (title + " " + artist + " " + tags + " " + status).toLower();
            if (!searchable.contains(query)) {
                continue;
            }
        }

        QString lyricsPreview = QStringLiteral("Lyrics pending sync");
        const auto hasLyrics = controller_ && controller_->hasLyrics(clip.id);
        if (hasLyrics && controller_) {
            const auto lyricsResult = controller_->getLyrics(clip.id);
            if (lyricsResult.isOk() && !lyricsResult.value().lines.empty()) {
                lyricsPreview = QString::fromStdString(lyricsResult.value().lines.front().text);
            } else {
                lyricsPreview = QStringLiteral("Lyrics ready");
            }
        }

        SunoClipListModel::ClipRow row;
        row.id = QString::fromStdString(clip.id);
        row.title = title;
        row.artist = artist;
        row.tags = tags;
        row.status = status;
        row.duration = duration;
        row.createdAt = created;
        row.lyricsPreview = lyricsPreview;
        row.hasLyrics = hasLyrics;
        row.imageUrl = QString::fromStdString(clip.image_large_url.empty() ? clip.image_url : clip.image_large_url);

        rows.push_back(std::move(row));
    }

    model_.setRows(std::move(rows));
    emit clipCountChanged();
}

void SunoRemoteLibraryViewModel::setStatusMessage(const QString& message) {
    if (statusMessage_ == message) {
        return;
    }

    statusMessage_ = message;
    emit statusMessageChanged();
}

void SunoRemoteLibraryViewModel::setSyncing(bool syncing) {
    if (syncing_ == syncing) {
        return;
    }

    syncing_ = syncing;
    emit syncingChanged();
}

void SunoRemoteLibraryViewModel::invokeOnUiThread(std::function<void()> fn) {
    if (!fn) {
        return;
    }

    if (QThread::currentThread() == thread()) {
        fn();
        return;
    }

    QMetaObject::invokeMethod(this, [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);
}

} // namespace vc::ui::qml
