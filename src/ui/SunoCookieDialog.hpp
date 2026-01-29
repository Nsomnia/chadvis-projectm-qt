#pragma once
// SunoCookieDialog.hpp - Dialog for Suno authentication via embedded browser

#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QTextEdit>

class QWebEngineView;
class QNetworkCookie;

namespace vc::ui {

class SunoCookieDialog : public QDialog {
    Q_OBJECT

public:
    explicit SunoCookieDialog(QWidget* parent = nullptr);
    ~SunoCookieDialog() override;

    QString getCookie() const;

private slots:
    void onAccept();
    void onCookieAdded(const QNetworkCookie& cookie);
    void onManualTextChanged();

private:
    void setupUI();

    QTabWidget* tabWidget_;
    QWebEngineView* webView_;
    QTextEdit* manualCookieEdit_;
    QLabel* statusLabel_;
    QPushButton* okBtn_;
    QPushButton* cancelBtn_;
    
    QString sessionCookie_;
    QString clientCookie_;
};

} // namespace vc::ui
