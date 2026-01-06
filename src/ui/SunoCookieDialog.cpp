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
            "To sync your library, you need to provide your session "
            "cookie.<br><br>"
            "<b>Method 1: Network Tab (Reliable)</b><br>"
            "1. Open <b>suno.com/create</b> and log in.<br>"
            "2. Press <b>F12</b> and go to the <b>Network</b> tab.<br>"
            "3. Refresh the page.<br>"
            "4. Filter for <b>?__clerk_api_version</b>.<br>"
            "5. Click the request, find <b>Cookie</b> in the 'Request Headers' "
            "section.<br>"
            "6. Copy the <b>entire value</b> (starts with __client... or "
            "contains __session...).<br><br>"
            "<b>Method 2: Console Snippet (Quick but might fail)</b><br>"
            "Run this in the <b>Console</b> tab. <i>Note: This may not work if "
            "cookies are secure/HttpOnly.</i>",
            this);
    introLabel->setWordWrap(true);
    layout->addWidget(introLabel);

    snippetDisplay_ = new QTextEdit(this);
    snippetDisplay_->setReadOnly(true);
    snippetDisplay_->setPlainText(
            "(function() {\n"
            "  const c = document.cookie;\n"
            "  if (!c.includes('__client=') && !c.includes('__session=')) {\n"
            "    alert('Required cookies missing from script access! Please "
            "use Method 1 (Network Tab) instead.');\n"
            "  } else {\n"
            "    console.log('Copy this:', c);\n"
            "    prompt('Copy your Suno Cookie:', c);\n"
            "  }\n"
            "})();");
    snippetDisplay_->setMaximumHeight(100);
    layout->addWidget(snippetDisplay_);

    copySnippetBtn_ = new QPushButton("Copy Snippet", this);
    connect(copySnippetBtn_,
            &QPushButton::clicked,
            this,
            &SunoCookieDialog::onCopySnippet);
    layout->addWidget(copySnippetBtn_);

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
