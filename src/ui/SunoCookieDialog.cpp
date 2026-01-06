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
            "To scrape your Suno library, we need your session cookie.\n"
            "1. Open suno.com and log in.\n"
            "2. Run the following JS snippet in your browser console (F12):\n",
            this);
    layout->addWidget(introLabel);

    snippetDisplay_ = new QTextEdit(this);
    snippetDisplay_->setReadOnly(true);
    snippetDisplay_->setPlainText(
            "(function() {\n"
            "  const cookie = document.cookie;\n"
            "  prompt('Copy your Suno Cookie:', cookie);\n"
            "})();");
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
