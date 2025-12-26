#include "Logger.hpp"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

static QString logFilePath;
static bool initialized = false;

void Logger::init(const QString& path)
{
    if (initialized) return;
    
    if (path.isEmpty()) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir dir(dataDir);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        logFilePath = dir.filePath("visualizer.log");
    } else {
        logFilePath = path;
    }
    
    initialized = true;
    
    QFile file(logFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << "\n=== " << QDateTime::currentDateTime().toString() << " ===\n";
    }
}

void Logger::log(const QString& message, const QString& level)
{
    if (!initialized) {
        init();
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString formatted = QString("[%1] %2: %3\n").arg(timestamp, level, message);
    
    // Console output
    if (level == "ERROR" || level == "CRITICAL") {
        qCritical() << message;
    } else if (level == "WARNING") {
        qWarning() << message;
    } else {
        qDebug() << message;
    }
    
    // File output
    QFile file(logFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << formatted;
    }
}

void Logger::debug(const QString& msg) { log(msg, "DEBUG"); }
void Logger::info(const QString& msg) { log(msg, "INFO"); }
void Logger::warning(const QString& msg) { log(msg, "WARNING"); }
void Logger::error(const QString& msg) { log(msg, "ERROR"); }
void Logger::critical(const QString& msg) { log(msg, "CRITICAL"); }
