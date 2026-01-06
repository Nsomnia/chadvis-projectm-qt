#include "SunoBrowser.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
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
}

SunoBrowser::~SunoBrowser() = default;

void SunoBrowser::setupUI() {
    auto* layout = new QVBoxLayout(this);

    auto* topLayout = new QHBoxLayout();
    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText("Search Suno...");
    topLayout->addWidget(searchEdit_);

    refreshBtn_ = new QPushButton("Refresh");
    topLayout->addWidget(refreshBtn_);

    syncBtn_ = new QPushButton("Authenticate / Sync");
    topLayout->addWidget(syncBtn_);

    layout->addLayout(topLayout);

    clipList_ = new QListWidget();
    layout->addWidget(clipList_);

    statusLabel_ = new QLabel("Arch Linux (btw)");
    layout->addWidget(statusLabel_);

    connect(refreshBtn_,
            &QPushButton::clicked,
            this,
            &SunoBrowser::onRefreshClicked);
    connect(syncBtn_, &QPushButton::clicked, this, &SunoBrowser::onSyncClicked);
    connect(clipList_,
            &QListWidget::itemDoubleClicked,
            this,
            &SunoBrowser::onItemDoubleClicked);
}

void SunoBrowser::onRefreshClicked() {
    statusLabel_->setText("Fetching...");
    controller_->refreshLibrary();
}

void SunoBrowser::onSyncClicked() {
    statusLabel_->setText("Syncing...");
    controller_->syncDatabase();
}

void SunoBrowser::updateList(const std::vector<SunoClip>& clips) {
    currentClips_ = clips;
    clipList_->clear();
    for (const auto& clip : clips) {
        auto* item = new QListWidgetItem(QString::fromStdString(clip.title));
        item->setData(Qt::UserRole, QString::fromStdString(clip.id));
        clipList_->addItem(item);
    }
    statusLabel_->setText(QString("Found %1 clips").arg(clips.size()));
}

void SunoBrowser::onItemDoubleClicked(QListWidgetItem* item) {
    std::string id = item->data(Qt::UserRole).toString().toStdString();
    for (const auto& clip : currentClips_) {
        if (clip.id == id) {
            controller_->downloadAndPlay(clip);
            break;
        }
    }
}

} // namespace vc::suno
