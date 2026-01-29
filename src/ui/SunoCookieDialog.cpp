#include "SunoCookieDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QNetworkCookie>
#include "core/Logger.hpp"

namespace vc::ui {

SunoCookieDialog::SunoCookieDialog(QWidget* parent) : QDialog(parent) {
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
    statusLabel_ = new QLabel("Please log in to Suno.com below...", this);
    statusLabel_->setStyleSheet("font-weight: bold; font-size: 14px;");
    headerLayout->addWidget(statusLabel_);
    browserLayout->addLayout(headerLayout);

    // Web View
    webView_ = new QWebEngineView(this);
    
    // Configure profile and cookie monitoring
    QWebEngineProfile* profile = webView_->page()->profile();
    profile->setHttpUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    
    connect(profile->cookieStore(), &QWebEngineCookieStore::cookieAdded,
            this, &SunoCookieDialog::onCookieAdded);
            
    webView_->load(QUrl("https://suno.com/login"));
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
