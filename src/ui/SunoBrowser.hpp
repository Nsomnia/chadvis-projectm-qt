#pragma once
// SunoBrowser.hpp - UI for browsing Suno AI library
// Because the browser is too slow and we use Arch btw

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>
#include "suno/SunoModels.hpp"

namespace vc::suno {

class SunoController;

class SunoBrowser : public QWidget {
    Q_OBJECT

public:
    explicit SunoBrowser(SunoController* controller, QWidget* parent = nullptr);
    ~SunoBrowser() override;

public slots:
    void updateList(const std::vector<SunoClip>& clips);

private slots:
    void onRefreshClicked();
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();

    SunoController* controller_;

    QListWidget* clipList_;
    QPushButton* refreshBtn_;
    QLineEdit* searchEdit_;
    QLabel* statusLabel_;

    std::vector<SunoClip> currentClips_;
};

} // namespace vc::suno
