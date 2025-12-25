/**
 * @file Logger.hpp
 * @brief Simple logging utility.
 *
 * TODO: Implement file logging
 * For now, wraps qDebug/qWarning/qCritical
 */
#ifndef LOGGER_HPP
#define LOGGER_HPP
#include <QString>
#include <QDebug>
namespace Log {
inline void debug(const QString& msg) { qDebug().noquote() << "[DEBUG]" << msg; }
inline void info(const QString& msg) { qInfo().noquote() << "[INFO]" << msg; }
inline void warn(const QString& msg) { qWarning().noquote() << "[WARN]" << msg; }
inline void error(const QString& msg) { qCritical().noquote() << "[ERROR]" << msg; }
}
#endif // LOGGER_HPP
