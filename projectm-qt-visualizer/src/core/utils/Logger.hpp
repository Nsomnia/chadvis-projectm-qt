/**
 * @file Logger.hpp
 * @brief Logging utility with file output
 */
#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <QString>

class Logger
{
public:
    static void init(const QString& path = QString());
    static void debug(const QString& msg);
    static void info(const QString& msg);
    static void warning(const QString& msg);
    static void error(const QString& msg);
    static void critical(const QString& msg);

private:
    static void log(const QString& message, const QString& level);
};

#endif // LOGGER_HPP
