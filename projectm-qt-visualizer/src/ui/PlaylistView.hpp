#pragma once
// PlaylistView.hpp - Playlist widget with drag-drop
// Where songs go to wait their turn

#include "util/Types.hpp"
#include "audio/Playlist.hpp"

#include <QListWidget>
#include <QMenu>

namespace vc {

class PlaylistView : public QListWidget {
    Q_OBJECT
    
public:
    explicit PlaylistView(QWidget* parent = nullptr);
    
    void setPlaylist(Playlist* playlist);
    
signals:
    void trackDoubleClicked(usize index);
    void filesDropped(const QStringList& paths);
    
public slots:
    void refresh();
    void scrollToCurrent();
    
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    
private slots:
    void onRemoveSelected();
    void onClearPlaylist();
    void onShowInFolder();
    
private:
    void setupContextMenu();
    void updateItem(int row, const PlaylistItem& item);
    
    Playlist* playlist_{nullptr};
    QMenu* contextMenu_{nullptr};
    QAction* removeAction_{nullptr};
    QAction* clearAction_{nullptr};
    QAction* showInFolderAction_{nullptr};
};

} // namespace vc
