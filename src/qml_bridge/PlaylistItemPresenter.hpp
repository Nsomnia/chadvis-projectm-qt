#pragma once
/**
 * @file PlaylistItemPresenter.hpp
 * @file Purpose: single conversion point from vc::PlaylistItem to QML-facing
 *                strings/variant maps (title/artist/path).
 *                Does NOT touch Suno clip maps (different entity) and does NOT
 *                own playlist state.
 *
 * @version 1.0.0 - 2026-08-25
 */

#include <QString>
#include <QVariantMap>

namespace vc {
struct PlaylistItem;
}

namespace qml_bridge {

/**
 * @brief Presents a PlaylistItem to QML ({title, artist, path} maps and roles).
 */
class PlaylistItemPresenter {
public:
    PlaylistItemPresenter() = delete;

    static QString title(const vc::PlaylistItem& item);
    static QString artist(const vc::PlaylistItem& item);

    /// Local filesystem path (remote items report their unresolved path field).
    static QString path(const vc::PlaylistItem& item);

    /// Path for display: remote URL when present, local path otherwise.
    static QString displayPath(const vc::PlaylistItem& item);

    /// {title, artist, path} map as consumed by QML playlist/current-track views.
    static QVariantMap toVariantMap(const vc::PlaylistItem& item);
};

} // namespace qml_bridge
