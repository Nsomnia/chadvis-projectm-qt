/**
 * @file IconManager.hpp
 * @brief SVG icon loading and caching system
 */

#pragma once

#include <QPixmap>
#include <QSize>
#include <QColor>
#include <QString>
#include <QHash>
#include <QMutex>

namespace vc::ui {

class IconManager {
public:
    static IconManager& instance();

    QPixmap icon(const QString& name, const QSize& size = QSize(24, 24), 
                 const QColor& color = QColor(0, 188, 212));
    
    void setIconPath(const QString& path);
    void setDefaultColor(const QColor& color);
    void setAccentColor(const QColor& color);
    void clearCache();
    void preloadIcons(const QStringList& names);

private:
    IconManager();
    ~IconManager() = default;
    
    IconManager(const IconManager&) = delete;
    IconManager& operator=(const IconManager&) = delete;

    QString findIconPath(const QString& name) const;
    QPixmap colorizeSvg(const QString& svgPath, const QSize& size, const QColor& color);

    QString iconPath_;
    QColor defaultColor_{0, 188, 212};  // Cyan accent
    QColor accentColor_{0, 255, 136};   // Green accent
    
    struct CacheKey {
        QString name;
        QSize size;
        QColor color;
        
        bool operator==(const CacheKey& other) const {
            return name == other.name && size == other.size && color == other.color;
        }
    };
    
    friend size_t qHash(const CacheKey& key, size_t seed = 0) {
        return qHash(key.name, seed) ^ qHash(key.size.width(), seed) ^ 
               qHash(key.size.height(), seed) ^ qHash(key.color.rgb(), seed);
    }
    
    QHash<CacheKey, QPixmap> cache_;
    QMutex cacheMutex_;
    static constexpr int kMaxCacheSize = 100;
};

// Convenience macros
#define ICON(name) vc::ui::IconManager::instance().icon(name)
#define ICON_SIZED(name, size) vc::ui::IconManager::instance().icon(name, size)
#define ICON_COLORED(name, color) vc::ui::IconManager::instance().icon(name, QSize(24, 24), color)

} // namespace vc::ui
