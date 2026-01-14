#include "SunoCookieDialog.hpp"
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace vc::ui {

SunoCookieDialog::SunoCookieDialog(QWidget* parent) : QDialog(parent) {
    setupUI();
    setWindowTitle("Suno AI Authentication");
    resize(600, 500);
}

SunoCookieDialog::~SunoCookieDialog() = default;

void SunoCookieDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);

    auto* introLabel = new QLabel(
            "<h3>Suno Authentication Required</h3>"
            "To sync your library, you need to provide valid session "
            "cookies.<br><br>"
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
            "2. Click <b>Connect</b>.<br><br>"
            "<b>Note:</b> The script automatically skips expired cookies and "
            "prefers the newest valid ones.",
            this);
    introLabel->setWordWrap(true);
    layout->addWidget(introLabel);

    snippetDisplay_ = new QTextEdit(this);
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
    snippetDisplay_->setMaximumHeight(250);
    layout->addWidget(snippetDisplay_);

    copySnippetBtn_ = new QPushButton("Copy Snippet", this);
    connect(copySnippetBtn_,
            &QPushButton::clicked,
            this,
            &SunoCookieDialog::onCopySnippet);
    layout->addWidget(copySnippetBtn_);

    autoDetectBtn_ = new QPushButton("📋 Auto-Detect from Clipboard", this);
    autoDetectBtn_->setToolTip(
            "Attempts to find Suno cookies in your clipboard");
    connect(autoDetectBtn_,
            &QPushButton::clicked,
            this,
            &SunoCookieDialog::onAutoDetect);
    layout->addWidget(autoDetectBtn_);

    auto* inputLabel = new QLabel("<b>Paste Cookie Here:</b>", this);
    layout->addWidget(inputLabel);

    cookieInput_ = new QTextEdit(this);
    cookieInput_->setPlaceholderText("__client=eyJ...; _ga=...");
    layout->addWidget(cookieInput_);

    auto* btnLayout = new QHBoxLayout();
    okBtn_ = new QPushButton("Connect", this);
    cancelBtn_ = new QPushButton("Cancel", this);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn_);
    btnLayout->addWidget(cancelBtn_);
    layout->addLayout(btnLayout);

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
    if (cookieInput_->toPlainText().trimmed().isEmpty()) {
        return;
    }
    accept();
}

QString SunoCookieDialog::getCookie() const {
    return cookieInput_->toPlainText().trimmed();
}

} // namespace vc::ui
