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
    QString getOTPCode() const;
    bool isLoginMode() const;

    void setSignInId(const std::string& id) { signInId_ = id; }
    std::string getSignInId() const { return signInId_; }
    void onCodeSent(bool success);

signals:
    void requestCodeRequested(const QString& email);

private slots:
    void onCopySnippet();
    void onAutoDetect();
    void onAccept();
    void onRequestCode();

private:
    void setupUI();

    std::string signInId_;
    QLineEdit* emailInput_;
    QLineEdit* passwordInput_;
    QLineEdit* otpInput_;
    QPushButton* requestCodeBtn_;
    QTextEdit* cookieInput_;
    QTextEdit* snippetDisplay_;
    QPushButton* copySnippetBtn_;
    QPushButton* autoDetectBtn_;
    QPushButton* okBtn_;
    QPushButton* cancelBtn_;
};

} // namespace vc::ui
