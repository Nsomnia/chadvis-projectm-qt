#include "ThemeBridge.hpp"
#include <QQmlEngine>

namespace qml_bridge {

ThemeBridge* ThemeBridge::s_instance = nullptr;

ThemeBridge::ThemeBridge(QObject* parent)
    : QObject(parent)
{
}

ThemeBridge* ThemeBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    if (!s_instance) {
        s_instance = new ThemeBridge(qmlEngine);
    }
    return s_instance;
}

QString ThemeBridge::formatTime(int ms) const
{
    if (ms < 0) return "0:00";
    int totalSec = ms / 1000;
    int min = totalSec / 60;
    int sec = totalSec % 60;
    return QString("%1:%2").arg(min).arg(sec, 2, 10, QChar('0'));
}

QColor ThemeBridge::lighten(const QColor& color, double amount) const
{
    return color.lighter(100 + static_cast<int>(amount * 100));
}

QColor ThemeBridge::darken(const QColor& color, double amount) const
{
    return color.darker(100 + static_cast<int>(amount * 100));
}

} // namespace qml_bridge
