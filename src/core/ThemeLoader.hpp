/**
 * @file ThemeLoader.hpp
 * @brief Qt stylesheet/theme loader.
 *
 * Single responsibility: Load and apply Qt stylesheets based on theme name.
 */

#pragma once
#include <QApplication>
#include <QString>
#include <memory>

namespace vc {

class ThemeLoader {
public:
    explicit ThemeLoader(QApplication* app);
    
    void load(const QString& themeName);
    void reload();

private:
    void applyFallbackDark();

    QApplication* app_;
    QString currentTheme_;
};

} // namespace vc
