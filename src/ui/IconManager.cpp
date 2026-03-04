/**
 * @file IconManager.cpp
 * @brief SVG icon manager implementation
 */

#include "IconManager.hpp"
#include "core/Logger.hpp"

#include <QFile>
#include <QSvgRenderer>
#include <QPainter>
#include <QDir>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>

namespace vc::ui {

IconManager& IconManager::instance() {
    static IconManager instance;
    return instance;
}

IconManager::IconManager() {
    // Default icon path
    iconPath_ = ":/icons/modern";
    
    // Also check filesystem
    QString fsPath = QApplication::applicationDirPath() + "/../share/chadvis-projectm-qt/icons/modern";
    if (QDir(fsPath).exists()) {
        iconPath_ = fsPath;
    }
    
    LOG_DEBUG("IconManager initialized, path: {}", iconPath_.toStdString());
}

void IconManager::setIconPath(const QString& path) {
    iconPath_ = path;
    clearCache();
}

void IconManager::setDefaultColor(const QColor& color) {
    defaultColor_ = color;
    clearCache();
}

void IconManager::setAccentColor(const QColor& color) {
    accentColor_ = color;
}

void IconManager::clearCache() {
    QMutexLocker locker(&cacheMutex_);
    cache_.clear();
}

void IconManager::preloadIcons(const QStringList& names) {
    for (const auto& name : names) {
        icon(name);  // This will cache the icon
    }
}

QString IconManager::findIconPath(const QString& name) const {
    // Try resource path first
    QString resourcePath = QString(":/icons/modern/%1.svg").arg(name);
    if (QFile::exists(resourcePath)) {
        return resourcePath;
    }
    
    // Try filesystem
    QString fsPath = QString("%1/%2.svg").arg(iconPath_, name);
    if (QFile::exists(fsPath)) {
        return fsPath;
    }
    
    // Try common icon names
    static const QHash<QString, QString> fallbacks = {
        {"play", "media-playback-start"},
        {"pause", "media-playback-pause"},
        {"stop", "media-playback-stop"},
        {"next", "media-skip-forward"},
        {"prev", "media-skip-backward"},
        {"music", "audio-x-generic"},
        {"folder", "folder"},
        {"settings", "preferences-system"},
        {"search", "system-search"},
    };
    
    if (fallbacks.contains(name)) {
        QString fallbackPath = QString(":/icons/modern/%1.svg").arg(fallbacks[name]);
        if (QFile::exists(fallbackPath)) {
            return fallbackPath;
        }
    }
    
    return {};
}

QPixmap IconManager::icon(const QString& name, const QSize& size, const QColor& color) {
    CacheKey key{name, size, color};
    
    {
        QMutexLocker locker(&cacheMutex_);
        if (cache_.contains(key)) {
            return cache_[key];
        }
    }
    
    QString svgPath = findIconPath(name);
    if (svgPath.isEmpty()) {
        LOG_WARN("Icon not found: {}", name.toStdString());
        // Return empty pixmap
        QPixmap empty(size);
        empty.fill(Qt::transparent);
        return empty;
    }
    
    QPixmap pixmap = colorizeSvg(svgPath, size, color);
    
    {
        QMutexLocker locker(&cacheMutex_);
        
        // Prune old entries if cache is full
        if (cache_.size() >= kMaxCacheSize) {
            cache_.clear();
        }
        
        cache_[key] = pixmap;
    }
    
    return pixmap;
}

QPixmap IconManager::colorizeSvg(const QString& svgPath, const QSize& size, const QColor& color) {
    // Read SVG content
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR("Cannot open SVG: {}", svgPath.toStdString());
        return {};
    }
    
    QByteArray svgContent = file.readAll();
    file.close();
    
    // Replace currentColor or add fill attribute
    QString svgString = QString::fromUtf8(svgContent);
    
    // Simple colorization: replace stroke/fill attributes
    QString colorHex = color.name(QColor::HexRgb);
    
    // Replace currentColor
    svgString.replace("currentColor", colorHex);
    
    // Add fill if not present
    if (!svgString.contains("fill=")) {
        svgString.replace("<svg", QString("<svg fill=\"%1\"").arg(colorHex));
    }
    
    // Render SVG
    QSvgRenderer renderer(svgString.toUtf8());
    if (!renderer.isValid()) {
        LOG_ERROR("Invalid SVG: {}", svgPath.toStdString());
        return {};
    }
    
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter);
    
    return pixmap;
}

} // namespace vc::ui
