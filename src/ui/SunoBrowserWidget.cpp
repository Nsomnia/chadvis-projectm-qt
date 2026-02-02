#include "SunoBrowserWidget.hpp"
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QDir>

namespace chadvis {

SunoBrowserWidget::SunoBrowserWidget(QWidget* parent)
    : QWidget(parent)
    , embeddedMode_(true) {
    setupUI();
    setupWebEngine();
}

SunoBrowserWidget::~SunoBrowserWidget() = default;

void SunoBrowserWidget::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(0, 0, 0, 0);
    mainLayout_->setSpacing(4);
    
    // Toolbar
    toolbarLayout_ = new QHBoxLayout();
    toolbarLayout_->setSpacing(4);
    
    urlEdit_ = new QLineEdit(this);
    urlEdit_->setPlaceholderText("URL...");
    urlEdit_->setReadOnly(true);
    toolbarLayout_->addWidget(urlEdit_, 1);
    
    reloadButton_ = new QPushButton("Reload", this);
    connect(reloadButton_, &QPushButton::clicked, this, &SunoBrowserWidget::onReloadClicked);
    toolbarLayout_->addWidget(reloadButton_);
    
    extractButton_ = new QPushButton("Extract Cookies", this);
    extractButton_->setEnabled(false);
    connect(extractButton_, &QPushButton::clicked, this, &SunoBrowserWidget::onExtractClicked);
    toolbarLayout_->addWidget(extractButton_);
    
    clearButton_ = new QPushButton("Clear", this);
    connect(clearButton_, &QPushButton::clicked, this, &SunoBrowserWidget::onClearClicked);
    toolbarLayout_->addWidget(clearButton_);
    
    windowModeButton_ = new QPushButton("Float", this);
    connect(windowModeButton_, &QPushButton::clicked, this, &SunoBrowserWidget::toggleWindowMode);
    toolbarLayout_->addWidget(windowModeButton_);
    
    mainLayout_->addLayout(toolbarLayout_);
    
    // Web view
    webView_ = new QWebEngineView(this);
    webView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout_->addWidget(webView_, 1);
    
    // Progress bar
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setTextVisible(true);
    progressBar_->hide();
    mainLayout_->addWidget(progressBar_);
    
    // Status label
    statusLabel_ = new QLabel("Ready", this);
    statusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mainLayout_->addWidget(statusLabel_);
    
    // Auth check timer
    authCheckTimer_ = new QTimer(this);
    authCheckTimer_->setInterval(1000);
    connect(authCheckTimer_, &QTimer::timeout, this, &SunoBrowserWidget::checkForAuthCompletion);
    
    setLayout(mainLayout_);
    setMinimumSize(400, 300);
}

void SunoBrowserWidget::setupWebEngine() {
    profile_ = new QWebEngineProfile("suno_auth_persistent", this);

    // Enable persistent cookies for session persistence across restarts
    profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    // Set storage paths in user's config directory
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + "/.config";
    }
    QString storagePath = configDir + "/chadvis-projectm-qt/webengine/suno/storage";
    QString cachePath = configDir + "/chadvis-projectm-qt/webengine/suno/cache";

    // Ensure directories exist
    QDir().mkpath(storagePath);
    QDir().mkpath(cachePath);

    profile_->setPersistentStoragePath(storagePath);
    profile_->setCachePath(cachePath);

    auto* page = new QWebEnginePage(profile_, this);
    webView_->setPage(page);

    connect(webView_, &QWebEngineView::loadStarted, this, &SunoBrowserWidget::onLoadStarted);
    connect(webView_, &QWebEngineView::loadProgress, this, &SunoBrowserWidget::onLoadProgress);
    connect(webView_, &QWebEngineView::loadFinished, this, &SunoBrowserWidget::onLoadFinished);
    connect(webView_, &QWebEngineView::urlChanged, this, &SunoBrowserWidget::onUrlChanged);

    auto* cookieStore = profile_->cookieStore();
    connect(cookieStore, &QWebEngineCookieStore::cookieAdded, this, &SunoBrowserWidget::onCookieAdded);

    // Load all cookies from persistent storage
    cookieStore->loadAllCookies();

    // Check for existing session after a short delay
    QTimer::singleShot(500, this, [this]() {
        // Check if we already have valid cookies from persistent storage
        if (cookieData_.isValid()) {
            updateStatus("Existing session restored from storage");
            emit authenticated(cookieData_);
        }
    });
}

void SunoBrowserWidget::navigateToSuno() {
    navigateToUrl(QUrl("https://suno.com"));
}

void SunoBrowserWidget::navigateToUrl(const QUrl& url) {
    if (!url.isValid()) {
        updateStatus("Invalid URL");
        return;
    }
    
    webView_->load(url);
    urlEdit_->setText(url.toString());
    updateStatus("Loading...");
}

void SunoBrowserWidget::onLoadStarted() {
    loadInProgress_ = true;
    progressBar_->show();
    progressBar_->setValue(0);
    extractButton_->setEnabled(false);
    emit loadStarted();
}

void SunoBrowserWidget::onLoadProgress(int progress) {
    progressBar_->setValue(progress);
}

void SunoBrowserWidget::onLoadFinished(bool success) {
    loadInProgress_ = false;
    progressBar_->hide();
    
    if (success) {
        updateStatus("Page loaded");
        extractButton_->setEnabled(isSunoDomain(webView_->url()));
        
        if (isSunoDomain(webView_->url())) {
            authCheckTimer_->start();
        }
    } else {
        updateStatus("Failed to load page");
    }
    
    emit loadFinished(success);
}

void SunoBrowserWidget::onUrlChanged(const QUrl& url) {
    urlEdit_->setText(url.toString());
    
    if (isSunoDomain(url)) {
        extractButton_->setEnabled(!loadInProgress_);
    } else {
        extractButton_->setEnabled(false);
        authCheckTimer_->stop();
    }
}

void SunoBrowserWidget::onCookieAdded(const QNetworkCookie& cookie) {
    const QString name = cookie.name();
    const QString value = QString::fromUtf8(cookie.value());
    
    if (name == "__session") {
        pendingSessionCookie_ = value;
        cookieData_.sessionCookie = value;
    } else if (name == "__client") {
        pendingClientCookie_ = value;
        cookieData_.clientCookie = value;
    }
}

void SunoBrowserWidget::checkForAuthCompletion() {
    if (cookieData_.isValid()) {
        authCheckTimer_->stop();
        updateStatus("Authentication detected - cookies extracted");
        emit authenticated(cookieData_);
    }
}

void SunoBrowserWidget::onExtractClicked() {
    extractCookies();
    
    if (cookieData_.isValid()) {
        updateStatus("Cookies extracted successfully");
        emit authenticated(cookieData_);
    } else {
        updateStatus("No authentication cookies found - please log in");
    }
}

void SunoBrowserWidget::extractCookies() {
    pendingSessionCookie_.clear();
    pendingClientCookie_.clear();
    
    auto* cookieStore = profile_->cookieStore();
    cookieStore->loadAllCookies();
}

void SunoBrowserWidget::onClearClicked() {
    clearAuthentication();
    webView_->page()->profile()->cookieStore()->deleteAllCookies();
    webView_->reload();
}

void SunoBrowserWidget::clearAuthentication() {
    cookieData_ = SunoCookieData{};
    pendingSessionCookie_.clear();
    pendingClientCookie_.clear();
    updateStatus("Authentication cleared");
}

void SunoBrowserWidget::onReloadClicked() {
    webView_->reload();
}

void SunoBrowserWidget::setEmbeddedMode(bool embedded) {
    embeddedMode_ = embedded;
    windowModeButton_->setText(embedded ? "Float" : "Dock");
}

void SunoBrowserWidget::toggleWindowMode() {
    if (embeddedMode_) {
        setParent(nullptr);
        setWindowFlags(Qt::Window);
        setEmbeddedMode(false);
        show();
        resize(800, 600);
        
        if (QApplication::primaryScreen()) {
            auto* screen = QApplication::primaryScreen();
            auto center = screen->availableGeometry().center();
            move(center.x() - width() / 2, center.y() - height() / 2);
        }
    } else {
        setEmbeddedMode(true);
        emit cancelled();
    }
}

bool SunoBrowserWidget::isSunoDomain(const QUrl& url) const {
    const QString host = url.host().toLower();
    return host.contains("suno.com") || host.contains("suno.ai");
}

void SunoBrowserWidget::updateStatus(const QString& message) {
    statusLabel_->setText(message);
    emit statusMessage(message);
}

} // namespace chadvis
