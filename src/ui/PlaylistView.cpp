#include "PlaylistView.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDesktopServices>
#include <QFileInfo>

namespace vc {

PlaylistView::PlaylistView(QWidget* parent)
    : QListWidget(parent)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAlternatingRowColors(true);
    
    setupContextMenu();
}

void PlaylistView::setPlaylist(Playlist* playlist) {
    playlist_ = playlist;
    
    if (playlist_) {
        playlist_->changed.connect([this] {
            QMetaObject::invokeMethod(this, &PlaylistView::refresh);
        });
        
        playlist_->currentChanged.connect([this](usize) {
            QMetaObject::invokeMethod(this, &PlaylistView::scrollToCurrent);
        });
        
        refresh();
    }
}

void PlaylistView::refresh() {
    clear();
    
    if (!playlist_) return;
    
    for (usize i = 0; i < playlist_->size(); ++i) {
        const auto* item = playlist_->itemAt(i);
        if (!item) continue;
        
        QString text = QString::fromStdString(item->metadata.displayArtist()) + " - " +
                       QString::fromStdString(item->metadata.displayTitle());
        
        auto* listItem = new QListWidgetItem(text, this);
        
        // Duration as tooltip
        QString duration = QString::fromStdString(file::formatDuration(item->metadata.duration));
        listItem->setToolTip(QString::fromStdString(item->path.string()) + "\n" + duration);
        
        // Highlight current
        if (playlist_->currentIndex() && *playlist_->currentIndex() == i) {
            listItem->setBackground(QColor(0, 255, 136, 50));
            QFont font = listItem->font();
            font.setBold(true);
            listItem->setFont(font);
        }
    }
}

void PlaylistView::scrollToCurrent() {
    if (!playlist_ || !playlist_->currentIndex()) return;
    
    usize idx = *playlist_->currentIndex();
    if (idx < static_cast<usize>(count())) {
        scrollToItem(item(idx));
        refresh();  // Update highlighting
    }
}

void PlaylistView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QListWidget::dragEnterEvent(event);
    }
}

void PlaylistView::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QListWidget::dragMoveEvent(event);
    }
}

void PlaylistView::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        QStringList paths;
        for (const auto& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths.append(url.toLocalFile());
            }
        }
        
        if (!paths.isEmpty()) {
            emit filesDropped(paths);
        }
        
        event->acceptProposedAction();
    } else {
        QListWidget::dropEvent(event);
    }
}

void PlaylistView::contextMenuEvent(QContextMenuEvent* event) {
    contextMenu_->exec(event->globalPos());
}

void PlaylistView::mouseDoubleClickEvent(QMouseEvent* event) {
    auto* item = itemAt(event->pos());
    if (item) {
        int row = this->row(item);
        emit trackDoubleClicked(static_cast<usize>(row));
    }
    QListWidget::mouseDoubleClickEvent(event);
}

void PlaylistView::setupContextMenu() {
    contextMenu_ = new QMenu(this);
    
    removeAction_ = contextMenu_->addAction("Remove Selected", this, &PlaylistView::onRemoveSelected);
    removeAction_->setShortcut(QKeySequence::Delete);
    
    clearAction_ = contextMenu_->addAction("Clear Playlist", this, &PlaylistView::onClearPlaylist);
    
    contextMenu_->addSeparator();
    
    showInFolderAction_ = contextMenu_->addAction("Show in Folder", this, &PlaylistView::onShowInFolder);
}

void PlaylistView::onRemoveSelected() {
    if (!playlist_) return;
    
    auto selected = selectedItems();
    
    // Remove from back to front to preserve indices
    std::vector<int> rows;
    for (auto* item : selected) {
        rows.push_back(row(item));
    }
    std::sort(rows.rbegin(), rows.rend());
    
    for (int r : rows) {
        playlist_->removeAt(static_cast<usize>(r));
    }
}

void PlaylistView::onClearPlaylist() {
    if (playlist_) {
        playlist_->clear();
    }
}

void PlaylistView::onShowInFolder() {
    auto selected = selectedItems();
    if (selected.isEmpty() || !playlist_) return;
    
    int r = row(selected.first());
    const auto* item = playlist_->itemAt(static_cast<usize>(r));
    if (item) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QString::fromStdString(item->path.parent_path().string())));
    }
}

} // namespace vc

#include "moc_PlaylistView.cpp"
