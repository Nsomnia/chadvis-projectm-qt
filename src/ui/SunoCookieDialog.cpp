#include "SunoCookieDialog.hpp"
#include "ui/SunoPersistentAuth.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QNetworkCookie>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include "core/Logger.hpp"

namespace vc::ui {

SunoCookieDialog::SunoCookieDialog(QWidget* parent, chadvis::SunoPersistentAuth* auth) 
    : QDialog(parent), auth_(auth) {
    setupUI();
    setWindowTitle("Suno AI Login");
    resize(1024, 768);
}

SunoCookieDialog::~SunoCookieDialog() = default;

void SunoCookieDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    tabWidget_ = new QTabWidget(this);

    // --- Tab 1: Browser Login ---
    auto* browserTab = new QWidget();
    auto* browserLayout = new QVBoxLayout(browserTab);

    // Status Header
    auto* headerLayout = new QHBoxLayout();
    statusLabel_ = new QLabel("Please log in to Suno.com below...\n(Note: Site may take a moment to load fully on some systems)", this);
    statusLabel_->setStyleSheet("font-weight: bold; font-size: 14px;");
    headerLayout->addWidget(statusLabel_);
    browserLayout->addLayout(headerLayout);

    // System Auth Button
    systemAuthBtn_ = new QPushButton("Login with System Browser (Google)", this);
    systemAuthBtn_->setStyleSheet("background-color: #4285f4; color: white; padding: 8px; font-weight: bold;");
    connect(systemAuthBtn_, &QPushButton::clicked, this, &SunoCookieDialog::startSystemAuthRequested);
    browserLayout->addWidget(systemAuthBtn_);

    // Web View
    if (auth_) {
        // Use the managed view from persistent auth
        webView_ = auth_->createWebView(this);
        // Connect cookie monitoring is handled by auth manager, but we also listen for immediate feedback in dialog
        connect(auth_->profile()->cookieStore(), &QWebEngineCookieStore::cookieAdded,
                this, &SunoCookieDialog::onCookieAdded);
    } else {
        // Fallback to local view creation (legacy behavior)
        webView_ = new QWebEngineView(this);
        
        // Configure profile with persistent storage for session persistence
        QWebEngineProfile* profile = webView_->page()->profile();
        
        // Set a modern, real-looking User Agent that matches the underlying Chromium major version (134)
        profile->setHttpUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");

        // Enable features that can help with login security checks
        webView_->settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
        webView_->settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
        webView_->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
        webView_->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        webView_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
        webView_->settings()->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
        webView_->settings()->setAttribute(QWebEngineSettings::AutoLoadImages, true);
        
        // Inject Stealth Script to mock navigator properties
        QWebEngineScript script;
        script.setName("stealth_injection");
        script.setInjectionPoint(QWebEngineScript::DocumentCreation);
        script.setWorldId(QWebEngineScript::MainWorld);
        script.setRunsOnSubFrames(true);
        
        QString stealthJs = R"(
            (function() {
                // Stealth Script to mock standard browser properties and hide automation signals
                Object.defineProperty(navigator, 'plugins', {
                    get: () => {
                        return [
                            { name: "Chrome PDF Plugin", filename: "internal-pdf-viewer", description: "Portable Document Format" },
                            { name: "Chrome PDF Viewer", filename: "mhjfbmdgcfjbbpaeojofohoefgiehjai", description: "Portable Document Format" },
                            { name: "Native Client", filename: "internal-nacl-plugin", description: "" }
                        ];
                    }
                });
                Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
                Object.defineProperty(navigator, 'languages', { get: () => ['en-US', 'en'] });
                if (!window.chrome) {
                    window.chrome = {
                        runtime: {},
                        loadTimes: function() {},
                        csi: function() {},
                        app: { isInstalled: false }
                    };
                }
            })();
        )";
        script.setSourceCode(stealthJs);
        profile->scripts()->insert(script);

        // Enable persistent cookies
        profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

        // Set storage paths in user's config directory
        QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        if (configDir.isEmpty()) {
            configDir = QDir::homePath() + "/.config";
        }
        QString storagePath = configDir + "/chadvis-projectm-qt/webengine/suno/storage";
        QString cachePath = configDir + "/chadvis-projectm-qt/webengine/suno/cache";

        QDir().mkpath(storagePath);
        QDir().mkpath(cachePath);

        profile->setPersistentStoragePath(storagePath);
        profile->setCachePath(cachePath);

        connect(profile->cookieStore(), &QWebEngineCookieStore::cookieAdded,
                this, &SunoCookieDialog::onCookieAdded);

        // Load existing cookies from persistent storage
        profile->cookieStore()->loadAllCookies();
    }

    webView_->load(QUrl("https://suno.com/sign-in"));
    browserLayout->addWidget(webView_);

    tabWidget_->addTab(browserTab, "Browser Login");

    // --- Tab 2: Manual Entry ---
    auto* manualTab = new QWidget();
    auto* manualLayout = new QVBoxLayout(manualTab);
    
    auto* manualLabel = new QLabel("Paste your cookie string here (e.g. __client=...; __session=...):", this);
    manualLayout->addWidget(manualLabel);
    
    manualCookieEdit_ = new QTextEdit(this);
    manualCookieEdit_->setPlaceholderText("__client=...; __session=...");
    connect(manualCookieEdit_, &QTextEdit::textChanged, this, &SunoCookieDialog::onManualTextChanged);
    manualLayout->addWidget(manualCookieEdit_);
    
    tabWidget_->addTab(manualTab, "Manual Entry");

    mainLayout->addWidget(tabWidget_);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    okBtn_ = new QPushButton("Connect", this);
    okBtn_->setEnabled(false); // Disabled until cookie found
    cancelBtn_ = new QPushButton("Cancel", this);
    
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn_);
    btnLayout->addWidget(cancelBtn_);
    mainLayout->addLayout(btnLayout);

    connect(okBtn_, &QPushButton::clicked, this, &SunoCookieDialog::onAccept);
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);
    
    // Enable OK button if switching to manual tab with text
    connect(tabWidget_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) { // Manual
            okBtn_->setEnabled(!manualCookieEdit_->toPlainText().trimmed().isEmpty());
        } else { // Browser
            okBtn_->setEnabled(!sessionCookie_.isEmpty());
        }
    });
}

void SunoCookieDialog::onManualTextChanged() {
    if (tabWidget_->currentIndex() == 1) {
        okBtn_->setEnabled(!manualCookieEdit_->toPlainText().trimmed().isEmpty());
    }
}

void SunoCookieDialog::onCookieAdded(const QNetworkCookie& cookie) {
    QString name = QString::fromUtf8(cookie.name());
    QString value = QString::fromUtf8(cookie.value());

    if (name == "__session") {
        sessionCookie_ = value;
        LOG_INFO("SunoCookieDialog: Found __session cookie");
    } else if (name == "__client") {
        clientCookie_ = value;
        LOG_INFO("SunoCookieDialog: Found __client cookie");
    }

    if (!sessionCookie_.isEmpty()) {
        statusLabel_->setText("✅ Session found! Click Connect to proceed.");
        statusLabel_->setStyleSheet("color: green; font-weight: bold; font-size: 14px;");
        if (tabWidget_->currentIndex() == 0) {
            okBtn_->setEnabled(true);
        }
    }
}

void SunoCookieDialog::onAccept() {
    accept();
}

QString SunoCookieDialog::getCookie() const {
    if (tabWidget_->currentIndex() == 1) {
        return manualCookieEdit_->toPlainText().trimmed();
    }
    
    QStringList parts;
    if (!clientCookie_.isEmpty()) parts << "__client=" + clientCookie_;
    if (!sessionCookie_.isEmpty()) parts << "__session=" + sessionCookie_;
    return parts.join("; ");
}

} // namespace vc::ui
