#pragma once
// SunoCookieDialog.hpp - Dialog for inputting Suno cookies
// Provides a JS snippet for the user to get their cookie

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>

namespace vc::ui {

class SunoCookieDialog : public QDialog {
    Q_OBJECT

public:
    explicit SunoCookieDialog(QWidget* parent = nullptr);
    ~SunoCookieDialog() override;

    QString getCookie() const;
    QString getEmail() const;
    QString getPassword() const;
    bool isLoginMode() const;

private slots:
    void onCopySnippet();
    void onAutoDetect();
    void onAccept();

private:
    void setupUI();

    QLineEdit* emailInput_;
    QLineEdit* passwordInput_;
    QTextEdit* cookieInput_;
    QTextEdit* snippetDisplay_;
    QPushButton* copySnippetBtn_;
    QPushButton* autoDetectBtn_;
    QPushButton* okBtn_;
    QPushButton* cancelBtn_;
};

} // namespace vc::ui
