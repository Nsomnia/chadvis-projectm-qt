#include "PlaylistItemPresenter.hpp"
#include "audio/Playlist.hpp"

namespace qml_bridge {

QString PlaylistItemPresenter::title(const vc::PlaylistItem& item) {
    return QString::fromStdString(item.metadata.displayTitle());
}

QString PlaylistItemPresenter::artist(const vc::PlaylistItem& item) {
    return QString::fromStdString(item.metadata.displayArtist());
}

QString PlaylistItemPresenter::path(const vc::PlaylistItem& item) {
    return QString::fromStdString(item.path.string());
}

QString PlaylistItemPresenter::displayPath(const vc::PlaylistItem& item) {
    return item.isRemote ? QString::fromStdString(item.url) : path(item);
}

QVariantMap PlaylistItemPresenter::toVariantMap(const vc::PlaylistItem& item) {
    QVariantMap result;
    result["title"] = title(item);
    result["artist"] = artist(item);
    result["path"] = path(item);
    return result;
}

} // namespace qml_bridge
