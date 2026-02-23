#include "ui/qml/QmlWorkspaceHost.hpp"

#include <QColor>
#include <QQmlContext>
#include <QQuickWidget>
#include <QVBoxLayout>

#include "core/Logger.hpp"
#include "ui/qml/PlaybackViewModel.hpp"
#include "ui/qml/PlaylistTrackModel.hpp"
#include "ui/qml/RecordingViewModel.hpp"
#include "ui/qml/SunoRemoteLibraryViewModel.hpp"
#include "ui/qml/VisualizerViewModel.hpp"

namespace vc::ui::qml {

QmlWorkspaceHost::QmlWorkspaceHost(PlaybackViewModel* playbackViewModel,
                                   PlaylistTrackModel* playlistTrackModel,
                                   SunoRemoteLibraryViewModel* sunoViewModel,
                                   RecordingViewModel* recordingViewModel,
                                   VisualizerViewModel* visualizerViewModel,
                                   QWidget* parent)
    : QWidget(parent)
    , playbackViewModel_(playbackViewModel)
    , playlistTrackModel_(playlistTrackModel)
    , sunoViewModel_(sunoViewModel)
    , recordingViewModel_(recordingViewModel)
    , visualizerViewModel_(visualizerViewModel) {
    setupUi();
}

void QmlWorkspaceHost::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    quickWidget_ = new QQuickWidget(this);
    quickWidget_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quickWidget_->setClearColor(QColor(QStringLiteral("#121A24")));

    auto* ctx = quickWidget_->rootContext();
    ctx->setContextProperty(QStringLiteral("playbackVm"), playbackViewModel_);
    ctx->setContextProperty(QStringLiteral("playlistModel"), playlistTrackModel_);
    ctx->setContextProperty(QStringLiteral("sunoVm"), sunoViewModel_);
    ctx->setContextProperty(QStringLiteral("recordingVm"), recordingViewModel_);
    ctx->setContextProperty(QStringLiteral("visualizerVm"), visualizerViewModel_);

    connect(quickWidget_, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
        if (status != QQuickWidget::Error) {
            return;
        }

        for (const auto& error : quickWidget_->errors()) {
            LOG_ERROR("QML workspace error: {}", error.toString().toStdString());
        }
    });

    quickWidget_->setSource(QUrl(QStringLiteral("qrc:/qml/MainWorkspace.qml")));
    rootLayout->addWidget(quickWidget_);
}

} // namespace vc::ui::qml
