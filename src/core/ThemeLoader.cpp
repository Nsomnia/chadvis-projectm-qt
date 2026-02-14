/**
 * @file ThemeLoader.cpp
 * @brief Qt stylesheet/theme loader implementation.
 */

#include "ThemeLoader.hpp"
#include "Logger.hpp"
#include <QFile>
#include <QFontDatabase>
#include <QStyleFactory>

namespace vc {

ThemeLoader::ThemeLoader(QApplication* app)
    : app_(app)
    , currentTheme_("dark")
{
}

void ThemeLoader::load(const QString& themeName) {
    currentTheme_ = themeName;

    app_->setStyle(QStyleFactory::create("Fusion"));

    QFontDatabase::addApplicationFont(":/fonts/liberation-sans.ttf");

    QString stylePath = QString(":/styles/%1.qss").arg(themeName);

    QFile styleFile(stylePath);
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QString style = styleFile.readAll();
        app_->setStyleSheet(style);
        LOG_DEBUG("Loaded theme: {}", themeName.toStdString());
    } else {
        if (themeName != "dark") {
            LOG_WARN("Theme not found: {}, using default dark theme", themeName.toStdString());
        } else {
            LOG_DEBUG("Dark theme requested, using built-in fallback");
        }
        applyFallbackDark();
    }
}

void ThemeLoader::reload() {
    load(currentTheme_);
}

void ThemeLoader::applyFallbackDark() {
    app_->setStyleSheet(R"(
        QMainWindow, QDialog, QWidget {
            background-color: #1e1e1e;
            color: #ffffff;
        }
        QPushButton {
            background-color: #3c3c3c;
            border: 1px solid #555555;
            border-radius: 4px;
            padding: 6px 12px;
            color: #ffffff;
        }
        QPushButton:hover {
            background-color: #4a4a4a;
        }
        QPushButton:pressed {
            background-color: #2a2a2a;
        }
        QListWidget, QTreeWidget, QTableWidget {
            background-color: #252525;
            border: 1px solid #3c3c3c;
            color: #ffffff;
        }
        QSlider::groove:horizontal {
            height: 4px;
            background: #3c3c3c;
        }
        QSlider::handle:horizontal {
            background: #00ff88;
            width: 12px;
            margin: -4px 0;
            border-radius: 6px;
        }
    )");
}

} // namespace vc
