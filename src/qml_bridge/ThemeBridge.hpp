#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QColor>
#include <QFont>

namespace qml_bridge {

/**
 * @brief ThemeBridge provides theme values to QML
 * 
 * This bridge exposes theme colors, fonts, and metrics to QML.
 * It's registered as a singleton "Theme" in the ChadVis module.
 */
class ThemeBridge : public QObject {
    Q_OBJECT

    // Colors - Primary palette (Cyan accent)
    Q_PROPERTY(QColor accent READ accent CONSTANT)
    Q_PROPERTY(QColor accentHover READ accentHover CONSTANT)
    Q_PROPERTY(QColor accentPressed READ accentPressed CONSTANT)
    Q_PROPERTY(QColor accentLight READ accentLight CONSTANT)
    Q_PROPERTY(QColor accentDark READ accentDark CONSTANT)

    // Background colors
    Q_PROPERTY(QColor background READ background CONSTANT)
    Q_PROPERTY(QColor backgroundAlt READ backgroundAlt CONSTANT)
    Q_PROPERTY(QColor surface READ surface CONSTANT)
    Q_PROPERTY(QColor surfaceRaised READ surfaceRaised CONSTANT)
    Q_PROPERTY(QColor surfaceOverlay READ surfaceOverlay CONSTANT)
    Q_PROPERTY(QColor surfaceVariant READ surfaceVariant CONSTANT)

    // Text colors
    Q_PROPERTY(QColor textPrimary READ textPrimary CONSTANT)
    Q_PROPERTY(QColor textSecondary READ textSecondary CONSTANT)
    Q_PROPERTY(QColor textDisabled READ textDisabled CONSTANT)
    Q_PROPERTY(QColor textOnAccent READ textOnAccent CONSTANT)
    Q_PROPERTY(QColor onSurface READ onSurface CONSTANT)
    Q_PROPERTY(QColor onBackground READ onBackground CONSTANT)
    Q_PROPERTY(QColor outline READ outline CONSTANT)
    Q_PROPERTY(QColor border READ border CONSTANT)

    // Semantic colors
    Q_PROPERTY(QColor success READ success CONSTANT)
    Q_PROPERTY(QColor warning READ warning CONSTANT)
    Q_PROPERTY(QColor error READ error CONSTANT)
    Q_PROPERTY(QColor recording READ recording CONSTANT)

    // Metrics - Spacing
    Q_PROPERTY(int spacingTiny READ spacingTiny CONSTANT)
    Q_PROPERTY(int spacingSmall READ spacingSmall CONSTANT)
    Q_PROPERTY(int spacingMedium READ spacingMedium CONSTANT)
    Q_PROPERTY(int spacingLarge READ spacingLarge CONSTANT)
    Q_PROPERTY(int spacingXL READ spacingXL CONSTANT)

    // Metrics - Border Radius
    Q_PROPERTY(int radiusSmall READ radiusSmall CONSTANT)
    Q_PROPERTY(int radiusMedium READ radiusMedium CONSTANT)
    Q_PROPERTY(int radiusLarge READ radiusLarge CONSTANT)

    // Metrics - Sizes
    Q_PROPERTY(int iconSmall READ iconSmall CONSTANT)
    Q_PROPERTY(int iconMedium READ iconMedium CONSTANT)
    Q_PROPERTY(int iconLarge READ iconLarge CONSTANT)
    Q_PROPERTY(int buttonHeight READ buttonHeight CONSTANT)
    Q_PROPERTY(int inputHeight READ inputHeight CONSTANT)
    Q_PROPERTY(int sidebarCollapsedWidth READ sidebarCollapsedWidth CONSTANT)
    Q_PROPERTY(int sidebarExpandedWidth READ sidebarExpandedWidth CONSTANT)
    Q_PROPERTY(int topBarHeight READ topBarHeight CONSTANT)
    Q_PROPERTY(int statusBarHeight READ statusBarHeight CONSTANT)

    // Animation durations
    Q_PROPERTY(int durationInstant READ durationInstant CONSTANT)
    Q_PROPERTY(int durationFast READ durationFast CONSTANT)
    Q_PROPERTY(int durationNormal READ durationNormal CONSTANT)
    Q_PROPERTY(int durationSlow READ durationSlow CONSTANT)

    // Typography
    Q_PROPERTY(QFont fontCaption READ fontCaption CONSTANT)
    Q_PROPERTY(QFont fontBody READ fontBody CONSTANT)
    Q_PROPERTY(QFont fontSubtitle READ fontSubtitle CONSTANT)
    Q_PROPERTY(QFont fontTitle READ fontTitle CONSTANT)

public:
    static ThemeBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // Colors
    QColor accent() const { return "#00bcd4"; }
    QColor accentHover() const { return "#26c6da"; }
    QColor accentPressed() const { return "#0097a7"; }
    QColor accentLight() const { return "#80deea"; }
    QColor accentDark() const { return "#00838f"; }

    QColor background() const { return "#1a1a1a"; }
    QColor backgroundAlt() const { return "#1e1e1e"; }
    QColor surface() const { return "#252525"; }
    QColor surfaceRaised() const { return "#2d2d2d"; }
    QColor surfaceOverlay() const { return "#353535"; }
    QColor surfaceVariant() const { return "#3a3a3a"; }

    QColor textPrimary() const { return "#e0e0e0"; }
    QColor textSecondary() const { return "#888888"; }
    QColor textDisabled() const { return "#606060"; }
    QColor textOnAccent() const { return "#1a1a1a"; }
    QColor onSurface() const { return "#e0e0e0"; }
    QColor onBackground() const { return "#e0e0e0"; }
    QColor outline() const { return "#4a4a4a"; }
    QColor border() const { return "#3a3a3a"; }

    QColor success() const { return "#00ff88"; }
    QColor warning() const { return "#ffb300"; }
    QColor error() const { return "#ff4444"; }
    QColor recording() const { return "#ff0000"; }

    // Spacing
    int spacingTiny() const { return 4; }
    int spacingSmall() const { return 8; }
    int spacingMedium() const { return 16; }
    int spacingLarge() const { return 24; }
    int spacingXL() const { return 32; }

    // Border radius
    int radiusSmall() const { return 4; }
    int radiusMedium() const { return 8; }
    int radiusLarge() const { return 12; }

    // Sizes
    int iconSmall() const { return 16; }
    int iconMedium() const { return 24; }
    int iconLarge() const { return 32; }
    int buttonHeight() const { return 36; }
    int inputHeight() const { return 36; }
    int sidebarCollapsedWidth() const { return 60; }
    int sidebarExpandedWidth() const { return 280; }
    int topBarHeight() const { return 40; }
    int statusBarHeight() const { return 28; }

    // Durations
    int durationInstant() const { return 50; }
    int durationFast() const { return 150; }
    int durationNormal() const { return 250; }
    int durationSlow() const { return 400; }

    // Fonts
    QFont fontCaption() const {
        QFont font("Inter");
        font.setPixelSize(12);
        return font;
    }
    QFont fontBody() const {
        QFont font("Inter");
        font.setPixelSize(14);
        return font;
    }
    QFont fontSubtitle() const {
        QFont font("Inter");
        font.setPixelSize(16);
        font.setWeight(QFont::DemiBold);
        return font;
    }
    QFont fontTitle() const {
        QFont font("Inter");
        font.setPixelSize(18);
        font.setWeight(QFont::Bold);
        return font;
    }

    // Helper functions (invokable)
    Q_INVOKABLE QString formatTime(int ms) const;
    Q_INVOKABLE QColor lighten(const QColor& color, double amount) const;
    Q_INVOKABLE QColor darken(const QColor& color, double amount) const;

private:
    explicit ThemeBridge(QObject* parent = nullptr);
    static ThemeBridge* s_instance;
};

} // namespace qml_bridge
