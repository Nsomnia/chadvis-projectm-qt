#pragma once
// SunoCookieDialog.hpp - Dialog for inputting Suno cookies
// Provides a JS snippet for the user to get their cookie

#include <QDialog>
#include <QPushButton>
#include <QTextEdit>

namespace vc::ui {

class SunoCookieDialog : public QDialog {
    Q_OBJECT

public:
    explicit SunoCookieDialog(QWidget* parent = nullptr);
    ~SunoCookieDialog() override;

    QString getCookie() const;

private slots:
    void onCopySnippet();
    void onAccept();

private:
    void setupUI();

    QTextEdit* cookieInput_;
    QTextEdit* snippetDisplay_;
    QPushButton* copySnippetBtn_;
    QPushButton* okBtn_;
    QPushButton* cancelBtn_;
};

} // namespace vc::ui
