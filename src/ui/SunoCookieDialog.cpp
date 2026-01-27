#include "SunoCookieDialog.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include "core/Config.hpp"

namespace vc::ui {

SunoCookieDialog::SunoCookieDialog(QWidget* parent) : QDialog(parent) {
    setupUI();
    setWindowTitle("Suno AI Authentication");
    resize(600, 600);
}

SunoCookieDialog::~SunoCookieDialog() = default;

void SunoCookieDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);

    // --- Automated Login Tab ---
    auto* loginTab = new QWidget();
    auto* loginLayout = new QVBoxLayout(loginTab);
    
    auto* loginIntro = new QLabel(
        "<h3>Automated Login</h3>"
        "Enter your Suno.com credentials to automatically obtain session cookies.<br>"
        "This is the recommended method as it allows ChadVis to refresh your session automatically.",
        loginTab);
    loginIntro->setWordWrap(true);
    loginLayout->addWidget(loginIntro);

    auto* formLayout = new QFormLayout();
    emailInput_ = new QLineEdit(loginTab);
    emailInput_->setPlaceholderText("email@example.com");
    emailInput_->setText(QString::fromStdString(CONFIG.suno().email));
    formLayout->addRow("Email:", emailInput_);

    passwordInput_ = new QLineEdit(loginTab);
    passwordInput_->setEchoMode(QLineEdit::Password);
    passwordInput_->setText(QString::fromStdString(CONFIG.suno().password));
    formLayout->addRow("Password:", passwordInput_);
    
    loginLayout->addLayout(formLayout);
    loginLayout->addStretch();
    
    tabs->addTab(loginTab, "Automated Login");

    // --- Manual Cookie Tab ---
    auto* cookieTab = new QWidget();
    auto* cookieLayout = new QVBoxLayout(cookieTab);

    auto* introLabel = new QLabel(
            "<h3>Manual Cookie Entry</h3>"
            "If automated login fails, you can manually provide session cookies.<br><br>"
            "<b>Step 1: Get Fresh Cookies</b><br>"
            "1. Go to <b>suno.com</b> and make sure you're logged in.<br>"
            "2. Press <b>F12</b> to open Developer Tools.<br>"
            "3. Go to the <b>Console</b> tab.<br>"
            "4. Copy and paste the JavaScript code below, then press <b>Enter</b>.<br>"
            "5. The script will show which cookies are valid/expired.<br>"
            "6. <b>Ctrl+A</b> to select all, <b>Ctrl+C</b> to copy.<br>"
            "7. Click Cancel on the prompt.<br><br>"
            "<b>Step 2: Connect in ChadVis</b><br>"
            "1. Click <b>Auto-Detect from Clipboard</b> below.<br>"
            "2. Click <b>Connect</b>.",
            cookieTab);
    introLabel->setWordWrap(true);
    cookieLayout->addWidget(introLabel);

    snippetDisplay_ = new QTextEdit(cookieTab);
    snippetDisplay_->setReadOnly(true);
    snippetDisplay_->setPlainText(
            "(async () => {\n"
            "  try {\n"
            "    const now = Date.now();\n"
            "    let validCookies = [];\n"
            "    let expiredCookies = [];\n"
            "    \n"
            "    // Method 1: Chrome DevTools cookieStore API (access HttpOnly cookies)\n"
            "    if (window.cookieStore) {\n"
            "      const all = await cookieStore.getAll();\n"
            "      \n"
            "      // Filter for __client and __session cookies\n"
            "      for (const c of all) {\n"
            "        if (c.name.startsWith('__client') || c.name.startsWith('__session')) {\n"
            "          // cookieStore uses 'expires' as milliseconds since epoch (number), not Date\n"
            "          const expMs = c.expires;  // This is a number, not Date\n"
            "          const expDate = expMs ? new Date(expMs) : null;\n"
            "          const isExpired = expMs && expMs < now;\n"
            "          const info = {\n"
            "            name: c.name,\n"
            "            value: c.value,\n"
            "            expires: expDate ? expDate.toISOString() : 'session',\n"
            "            isExpired: isExpired\n"
            "          };\n"
            "          if (isExpired) expiredCookies.push(info);\n"
            "          else validCookies.push(info);\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "    \n"
            "    // Method 2: Fallback to document.cookie (non-HttpOnly only)\n"
            "    if (validCookies.length < 2) {\n"
            "      const dc = document.cookie;\n"
            "      const dcCookies = dc.split(';').map(c => c.trim()).filter(c => \n"
            "        c.startsWith('__client') || c.startsWith('__session')\n"
            "      );\n"
            "      for (const c of dcCookies) {\n"
            "        const name = c.split('=')[0];\n"
            "        // Check if we already have this cookie (prefer cookieStore)\n"
            "        if (!validCookies.find(existing => existing.name === name) &&\n"
            "            !expiredCookies.find(existing => existing.name === name)) {\n"
            "          // Assume session cookie (may or may not be expired)\n"
            "          validCookies.push({name, value: c.split('=').slice(1).join('='), expires: 'unknown', isExpired: false});\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "    \n"
            "    // Show debug info\n"
            "    console.log('=== Suno Cookie Debug ===');\n"
            "    console.log('Valid cookies:', validCookies);\n"
            "    if (expiredCookies.length > 0) {\n"
            "      console.log('Expired cookies (will be skipped):', expiredCookies);\n"
            "    }\n"
            "    \n"
            "    // Filter for required cookies (valid only)\n"
            "    const clientCookies = validCookies.filter(c => c.name.startsWith('__client'));\n"
            "    const sessionCookies = validCookies.filter(c => c.name.startsWith('__session'));\n"
            "    \n"
            "    // Prefer newest: no suffix > _Jnxw-muT > others\n"
            "    const pickNewest = (cookies) => {\n"
            "      if (cookies.length === 0) return null;\n"
            "      // Sort: no suffix first, then _Jnxw-muT, then others\n"
            "      return cookies.sort((a, b) => {\n"
            "        const getPriority = (c) => {\n"
            "          if (!c.name.includes('_')) return 0;  // no suffix - newest\n"
            "          if (c.name.includes('_Jnxw-muT')) return 1;\n"
            "          return 2;\n"
            "        };\n"
            "        return getPriority(a) - getPriority(b);\n"
            "      })[0];\n"
            "    };\n"
            "    \n"
            "    const client = pickNewest(clientCookies);\n"
            "    const session = pickNewest(sessionCookies);\n"
            "    \n"
            "    if (client && session) {\n"
            "      const cookieStr = client.name + '=' + client.value + '; ' + session.name + '=' + session.value;\n"
            "      console.log('✅ Using valid cookies:');\n"
            "      console.log('  __client: ' + client.name + ' (expires: ' + client.expires + ')');\n"
            "      console.log('  __session: ' + session.name + ' (expires: ' + session.expires + ')');\n"
            "      console.log('Combined cookie:', cookieStr);\n"
            "      \n"
            "      // Show in prompt for easy copying\n"
            "      const result = prompt(\n"
            "        '✅ Valid cookies found!\\n\\n' +\n"
            "        '1. SELECT ALL text in the box below (Ctrl+A)\\n' +\n"
            "        '2. COPY (Ctrl+C)\\n' +\n"
            "        '3. Click CANCEL\\n\\n' +\n"
            "        'Then paste in ChadVis app:',\n"
            "        cookieStr\n"
            "      );\n"
            "      \n"
            "      alert('✅ Done! Paste in ChadVis and click Connect.');\n"
            "    } else {\n"
            "      let msg = '';\n"
            "      if (!client) msg += 'Missing __client cookie.\\n';\n"
            "      if (!session) msg += 'Missing __session cookie.\\n';\n"
            "      if (expiredCookies.length > 0) {\n"
            "        msg += '\\n⚠️ ' + expiredCookies.length + ' expired cookie(s) were skipped:\\n';\n"
            "        expiredCookies.forEach(c => msg += '  - ' + c.name + ' (expired: ' + c.expires + ')\\n');\n"
            "      }\n"
            "      msg += '\\nPlease refresh suno.com and try again.';\n"
            "      console.log(msg);\n"
            "      prompt(msg + '\\n\\nManual extraction required:', '');\n"
            "    }\n"
            "  } catch (e) {\n"
            "    console.error('Error:', e);\n"
            "    alert('Error: ' + e.message + '\\n\\nUse F12 → Application → Cookies manually.');\n"
            "  }\n"
            "})();");
    snippetDisplay_->setMaximumHeight(200);
    cookieLayout->addWidget(snippetDisplay_);

    copySnippetBtn_ = new QPushButton("Copy Snippet", cookieTab);
    connect(copySnippetBtn_,
            &QPushButton::clicked,
            this,
            &SunoCookieDialog::onCopySnippet);
    cookieLayout->addWidget(copySnippetBtn_);

    autoDetectBtn_ = new QPushButton("📋 Auto-Detect from Clipboard", cookieTab);
    autoDetectBtn_->setToolTip(
            "Attempts to find Suno cookies in your clipboard");
    connect(autoDetectBtn_,
            &QPushButton::clicked,
            this,
            &SunoCookieDialog::onAutoDetect);
    cookieLayout->addWidget(autoDetectBtn_);

    auto* inputLabel = new QLabel("<b>Paste Cookie Here:</b>", cookieTab);
    cookieLayout->addWidget(inputLabel);

    cookieInput_ = new QTextEdit(cookieTab);
    cookieInput_->setPlaceholderText("__client=eyJ...; _ga=...");
    cookieLayout->addWidget(cookieInput_);
    
    tabs->addTab(cookieTab, "Manual Cookie");

    mainLayout->addWidget(tabs);

    auto* btnLayout = new QHBoxLayout();
    okBtn_ = new QPushButton("Connect", this);
    cancelBtn_ = new QPushButton("Cancel", this);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn_);
    btnLayout->addWidget(cancelBtn_);
    mainLayout->addLayout(btnLayout);

    connect(okBtn_, &QPushButton::clicked, this, &SunoCookieDialog::onAccept);
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);
}

void SunoCookieDialog::onCopySnippet() {
    QGuiApplication::clipboard()->setText(snippetDisplay_->toPlainText());
}

void SunoCookieDialog::onAutoDetect() {
    QClipboard* clipboard = QGuiApplication::clipboard();
    QString text = clipboard->text().trimmed();
    bool found = false;

    // Direct cookie paste
    if (text.contains("__client") || text.contains("__session")) {
        cookieInput_->setText(text);
        found = true;
    }
    // HTTP Header block
    else if (text.contains("Cookie:", Qt::CaseInsensitive)) {
        QStringList lines = text.split('\n');
        for (const auto& line : lines) {
            if (line.trimmed().startsWith("Cookie:", Qt::CaseInsensitive)) {
                QString cookie = line.mid(line.indexOf(':') + 1).trimmed();
                if (cookie.contains("__client=") ||
                    cookie.contains("__session=")) {
                    cookieInput_->setText(cookie);
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) {
        cookieInput_->setPlaceholderText(
                "Could not detect Suno cookies in clipboard...");
    }
}

void SunoCookieDialog::onAccept() {
    if (isLoginMode()) {
        if (emailInput_->text().trimmed().isEmpty() || passwordInput_->text().trimmed().isEmpty()) {
            return;
        }
    } else {
        if (cookieInput_->toPlainText().trimmed().isEmpty()) {
            return;
        }
    }
    accept();
}

QString SunoCookieDialog::getCookie() const {
    return cookieInput_->toPlainText().trimmed();
}

QString SunoCookieDialog::getEmail() const {
    return emailInput_->text().trimmed();
}

QString SunoCookieDialog::getPassword() const {
    return passwordInput_->text().trimmed();
}

bool SunoCookieDialog::isLoginMode() const {
    // If we are on the first tab, it's login mode
    if (auto* tabs = findChild<QTabWidget*>()) {
        return tabs->currentIndex() == 0;
    }
    return false;
}

} // namespace vc::ui
