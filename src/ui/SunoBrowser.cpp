#include "SunoBrowser.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include "controllers/SunoController.hpp"

namespace vc::suno {

SunoBrowser::SunoBrowser(SunoController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setupUI();

    // Connect to controller
    controller_->libraryUpdated.connect([this](const auto& clips) {
        QMetaObject::invokeMethod(this, [this, clips] { updateList(clips); });
    });

    controller_->statusMessage.connect([this](const auto& msg) {
        QMetaObject::invokeMethod(this, [this, msg] {
            statusLabel_->setText(QString::fromStdString(msg));
        });
    });

    if (!controller_->clips().empty()) {
        updateList(controller_->clips());
    }
}

SunoBrowser::~SunoBrowser() = default;

void SunoBrowser::setupUI() {
    auto* layout = new QVBoxLayout(this);

    auto* topLayout = new QHBoxLayout();
    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText("Search Suno library...");
    topLayout->addWidget(searchEdit_);

    refreshBtn_ = new QPushButton("Sync Library");
    topLayout->addWidget(refreshBtn_);

    syncBtn_ = new QPushButton("Auth / Cookie");
    topLayout->addWidget(syncBtn_);

    layout->addLayout(topLayout);

    clipTable_ = new QTableWidget();
    clipTable_->setColumnCount(7);
    clipTable_->setHorizontalHeaderLabels({"Title", "Model", "Version", "Tags", "Duration", "Created", "Status"});
    clipTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    clipTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    clipTable_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(clipTable_);

    statusLabel_ = new QLabel("Arch Linux (btw)");
    layout->addWidget(statusLabel_);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(refreshBtn_, &QPushButton::clicked, this, &SunoBrowser::onRefreshClicked);
    connect(syncBtn_, &QPushButton::clicked, this, &SunoBrowser::onSyncClicked);
    connect(clipTable_, &QTableWidget::cellDoubleClicked, this, &SunoBrowser::onItemDoubleClicked);
    connect(searchEdit_, &QLineEdit::textChanged, this, &SunoBrowser::onSearchChanged);
}

void SunoBrowser::updateList(const std::vector<SunoClip>& clips) {
    currentClips_ = clips;
    clipTable_->setRowCount(0);
    for (const auto& clip : clips) {
        int row = clipTable_->rowCount();
        clipTable_->insertRow(row);

        auto* titleItem = new QTableWidgetItem(QString::fromStdString(clip.title));
        titleItem->setData(Qt::UserRole, QString::fromStdString(clip.id));
        clipTable_->setItem(row, 0, titleItem);
        clipTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(clip.model_name)));
        clipTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(clip.major_model_version)));
        clipTable_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(clip.metadata.tags)));
        clipTable_->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(clip.metadata.duration)));
        clipTable_->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(clip.created_at)));
        clipTable_->setItem(row, 6, new QTableWidgetItem(QString::fromStdString(clip.status)));
    }
    statusLabel_->setText(QString("Found %1 clips").arg(clips.size()));
}

void SunoBrowser::onItemDoubleClicked(int row, int column) {
    auto* item = clipTable_->item(row, 0);
    if (!item) return;

    std::string id = item->data(Qt::UserRole).toString().toStdString();
    for (const auto& clip : currentClips_) {
        if (clip.id == id) {
            controller_->downloadAndPlay(clip);
            break;
        }
    }
}

void SunoBrowser::onSearchChanged(const QString& text) {
    for (int i = 0; i < clipTable_->rowCount(); ++i) {
        bool match = false;
        for (int j = 0; j < clipTable_->columnCount(); ++j) {
            if (clipTable_->item(i, j)->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        clipTable_->setRowHidden(i, !match);
    }
}

void SunoBrowser::onRefreshClicked() {
    statusLabel_->setText("Fetching...");
    controller_->refreshLibrary();
}

void SunoBrowser::onSyncClicked() {
    statusLabel_->setText("Opening Auth Dialog...");
    controller_->syncDatabase(true);
}

} // namespace vc::suno
