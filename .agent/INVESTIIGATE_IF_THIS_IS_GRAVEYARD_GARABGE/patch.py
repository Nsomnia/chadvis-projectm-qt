import re

with open('src/qml_bridge/ThemeBridge.hpp', 'r') as f:
    hpp = f.read()

hpp = hpp.replace('Q_PROPERTY(QColor accent READ accent CONSTANT)', 'Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)')
hpp = hpp.replace('Q_PROPERTY(QColor background READ background CONSTANT)', 'Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY backgroundChanged)')

hpp = hpp.replace('    QColor accent() const { return "#00bcd4"; }', '    QColor accent() const;\n    void setAccent(const QColor& color);')
hpp = hpp.replace('    QColor background() const { return "#1a1a1a"; }', '    QColor background() const;\n    void setBackground(const QColor& color);')

hpp = hpp.replace('private:', 'signals:\n    void accentChanged();\n    void backgroundChanged();\n\nprivate:')

with open('src/qml_bridge/ThemeBridge.hpp', 'w') as f:
    f.write(hpp)

with open('src/qml_bridge/ThemeBridge.cpp', 'r') as f:
    cpp = f.read()

cpp = cpp.replace('#include <QQmlEngine>', '#include <QQmlEngine>\n#include "core/Config.hpp"')

insert_pos = cpp.find('QString ThemeBridge::formatTime')
insert_str = """
QColor ThemeBridge::accent() const {
    return QColor(QString::fromStdString(vc::Config::instance().ui().accentColor.toHex()));
}

void ThemeBridge::setAccent(const QColor& color) {
    auto hex = color.name().toStdString();
    if (vc::Config::instance().ui().accentColor.toHex() != hex) {
        vc::Config::instance().ui().accentColor = vc::Color::fromHex(hex);
        vc::Config::instance().save(vc::Config::instance().configPath());
        emit accentChanged();
    }
}

QColor ThemeBridge::background() const {
    return QColor(QString::fromStdString(vc::Config::instance().ui().backgroundColor.toHex()));
}

void ThemeBridge::setBackground(const QColor& color) {
    auto hex = color.name().toStdString();
    if (vc::Config::instance().ui().backgroundColor.toHex() != hex) {
        vc::Config::instance().ui().backgroundColor = vc::Color::fromHex(hex);
        vc::Config::instance().save(vc::Config::instance().configPath());
        emit backgroundChanged();
    }
}

"""

cpp = cpp[:insert_pos] + insert_str + cpp[insert_pos:]

with open('src/qml_bridge/ThemeBridge.cpp', 'w') as f:
    f.write(cpp)
