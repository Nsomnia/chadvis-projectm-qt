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
    resize(500, 400);
}

SunoCookieDialog::~SunoCookieDialog() = default;

void SunoCookieDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);

    auto* introLabel = new QLabel(
            "To sync your Suno library, we need your session cookie/token.\n\n"
            "<b>How to get it:</b>\n"
            "1. Open <b>suno.com/create</b> and log in.\n"
            "2. Press <b>F12</b> to open Developer Tools.\n"
            "3. Go to the <b>Network</b> tab and refresh the page.\n"
            "4. Filter for <b>?__clerk</b> or <b>tokens</b>.\n"
            "5. Click a request, go to <b>Headers</b>, find the <b>Cookie</b> "
            "header.\n"
            "6. Copy the <b>entire value</b> of the Cookie header.\n\n"
            "Alternatively, try running this in the <b>Console</b> tab:",
            this);
    introLabel->setWordWrap(true);
    layout->addWidget(introLabel);

    snippetDisplay_ = new QTextEdit(this);
    snippetDisplay_->setReadOnly(true);
    snippetDisplay_->setPlainText(
            "(function() {\n"
            "  const c = document.cookie.split('; ').find(row => "
            "row.startsWith('__client='));\n"
            "  if (c) {\n"
            "    prompt('Copy your Suno Cookie:', document.cookie);\n"
            "  } else {\n"
            "    alert('__client cookie not found! Please refresh or ensure "
            "you are logged in.');\n"
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

    auto* inputLabel = new QLabel("Paste your cookie here:", this);
    layout->addWidget(inputLabel);

    cookieInput_ = new QTextEdit(this);
    layout->addWidget(cookieInput_);

    auto* btnLayout = new QHBoxLayout();
    okBtn_ = new QPushButton("OK", this);
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
