#include "WhisperTranscriber.hpp"

#include "core/Logger.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>

#include <algorithm>

namespace vc::lyrics {

namespace {

constexpr f32 kMillisecondsPerSecond = 1000.0f;
/// A tail kept from the child's chatter, so a failure can quote what it said
/// instead of "exit code 1".
constexpr int kMaxLogTail = 2048;

f32 parseHms(QString text) {
    text = text.trimmed();
    if (text.isEmpty()) {
        return 0.0f;
    }
    int milliseconds = 0;
    const qsizetype fracSep =
            std::max(text.lastIndexOf(QLatin1Char(',')), text.lastIndexOf(QLatin1Char('.')));
    if (fracSep >= 0) {
        QString fraction = text.mid(fracSep + 1);
        text.truncate(fracSep);
        while (fraction.size() > 3) {
            fraction.chop(1);
        }
        while (fraction.size() < 3) {
            fraction.append(QLatin1Char('0'));
        }
        bool ok = false;
        const int value = fraction.toInt(&ok);
        if (ok) {
            milliseconds = value;
        }
    }
    double total = 0.0;
    const QStringList parts = text.split(QLatin1Char(':'));
    for (const QString& part : parts) {
        bool ok = false;
        const double value = part.toDouble(&ok);
        if (!ok) {
            return 0.0f;
        }
        total = total * 60.0 + value;
    }
    return static_cast<f32>(total + static_cast<double>(milliseconds) / 1000.0);
}

/// Split a segment's text across its measured bounds.
///
/// This is interpolation INSIDE a range whisper measured, not onset detection.
/// Every word it produces is flagged `interpolated` so a caller can show it as
/// approximate; a one-word segment (what `-ml 1` mostly yields) is not flagged
/// because its bounds are the segment's own.
std::vector<WhisperWord> splitWords(const QString& text, f32 start, f32 end) {
    std::vector<WhisperWord> words;
    const QStringList tokens = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return words;
    }
    const f32 span = std::max(0.0f, end - start);
    const f32 step = span / static_cast<f32>(tokens.size());
    const bool interpolated = tokens.size() > 1;
    words.reserve(static_cast<std::size_t>(tokens.size()));
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        WhisperWord word;
        word.text = tokens.at(i).toStdString();
        word.start = start + step * static_cast<f32>(i);
        word.end = start + step * static_cast<f32>(i + 1);
        word.interpolated = interpolated;
        words.push_back(std::move(word));
    }
    return words;
}

QString optionalString(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

} // namespace

std::size_t WhisperTranscript::wordCount() const {
    std::size_t count = 0;
    for (const WhisperSegment& segment : segments) {
        count += segment.words.size();
    }
    return count;
}

f32 WhisperTranscript::lastEnd() const {
    f32 end = 0.0f;
    for (const WhisperSegment& segment : segments) {
        end = std::max(end, segment.end);
    }
    return end;
}

std::optional<WhisperTranscript> parseWhisperJson(const QByteArray& json, QString* error) {
    const auto reject = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return std::optional<WhisperTranscript>{};
    };

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return reject(QStringLiteral("whisper output did not parse as a JSON object: %1")
                              .arg(parseError.errorString()));
    }
    const QJsonObject root = document.object();
    const QJsonValue entries = root.value(QStringLiteral("transcription"));
    if (!entries.isArray()) {
        return reject(QStringLiteral("whisper output carries no transcription array"));
    }

    WhisperTranscript transcript;
    transcript.language = optionalString(root.value(QStringLiteral("result")).toObject(),
                                         QStringLiteral("language"))
                                  .toStdString();
    if (transcript.language.empty()) {
        transcript.language = optionalString(root.value(QStringLiteral("params")).toObject(),
                                             QStringLiteral("language"))
                                      .toStdString();
    }
    transcript.model = optionalString(root.value(QStringLiteral("model")).toObject(),
                                      QStringLiteral("type"))
                               .toStdString();

    for (const QJsonValue& entry : entries.toArray()) {
        const QJsonObject object = entry.toObject();
        if (object.isEmpty()) {
            continue;
        }
        const QString text = object.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty()) {
            continue;
        }

        // Milliseconds since the start of the file (`offsets`), with the
        // human-readable `timestamps` as the fallback. whisper.cpp writes both;
        // `offsets` is the exact one, so it wins where present.
        const QJsonObject offsets = object.value(QStringLiteral("offsets")).toObject();
        const QJsonObject timestamps = object.value(QStringLiteral("timestamps")).toObject();

        f32 start = 0.0f;
        f32 end = 0.0f;
        if (offsets.contains(QStringLiteral("from"))) {
            start = static_cast<f32>(offsets.value(QStringLiteral("from")).toDouble()) /
                    kMillisecondsPerSecond;
            end = static_cast<f32>(offsets.value(QStringLiteral("to")).toDouble()) /
                  kMillisecondsPerSecond;
        } else {
            start = parseHms(timestamps.value(QStringLiteral("from")).toString());
            end = parseHms(timestamps.value(QStringLiteral("to")).toString());
        }
        if (end < start) {
            end = start;
        }

        WhisperSegment segment;
        segment.text = text.toStdString();
        segment.start = start;
        segment.end = end;
        segment.words = splitWords(text, start, end);
        transcript.segments.push_back(std::move(segment));
    }

    if (error) {
        error->clear();
    }
    return transcript;
}

LyricsData toLyricsData(const WhisperTranscript& transcript, const std::string& songId) {
    LyricsData lyrics;
    lyrics.source = "whisper";
    lyrics.songId = songId;
    lyrics.duration = transcript.lastEnd();
    lyrics.lines.reserve(transcript.segments.size());

    for (const WhisperSegment& segment : transcript.segments) {
        LyricsLine line;
        line.text = segment.text;
        line.startTime = segment.start;
        line.endTime = segment.end;
        line.isSynced = segment.end > segment.start;
        line.words.reserve(segment.words.size());
        for (const WhisperWord& word : segment.words) {
            LyricsWord converted;
            converted.text = word.text;
            converted.startTime = word.start;
            converted.endTime = word.end;
            // A divided-out word is certainly not as confident as a measured
            // segment boundary, and the field exists to say so.
            converted.confidence = word.interpolated ? 0.5f : 1.0f;
            line.words.push_back(std::move(converted));
        }
        lyrics.lines.push_back(std::move(line));
    }

    lyrics.isSynced = !lyrics.lines.empty();
    return lyrics;
}

WhisperTranscriber::WhisperTranscriber(QObject* parent) : QObject(parent) {
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->setProgram(QString());

    connect(process_, &QProcess::readyRead, this, [this]() { onReadyRead(); });
    connect(process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                onFinished(exitCode, status == QProcess::NormalExit ? 0 : 1);
            });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        running_ = false;
        emit failed(tr("Could not start the whisper CLI (%1). Set the executable path.")
                            .arg(options_.executable));
    });
}

WhisperTranscriber::~WhisperTranscriber() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(2000);
    }
}

void WhisperTranscriber::setOptions(const WhisperOptions& options) {
    options_ = options;
}

QString WhisperTranscriber::findExecutable() const {
    if (!options_.executable.isEmpty()) {
        return options_.executable;
    }
    return probeExecutable();
}

QString WhisperTranscriber::probeExecutable() {
    // `main` is deliberately absent: it is the pre-rename binary name and far
    // too generic to resolve on PATH without picking up someone else's program.
    for (const QString& name :
         {QStringLiteral("whisper-cli"), QStringLiteral("whisper.cpp"),
          QStringLiteral("whisper")}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return {};
}

bool WhisperTranscriber::isAvailable() const {
    if (findExecutable().isEmpty() || options_.modelPath.isEmpty()) {
        return false;
    }
    const QFileInfo model(options_.modelPath);
    return model.exists() && model.isFile() && model.isReadable();
}

void WhisperTranscriber::transcribe(const QString& audioPath) {
    if (running_) {
        emit failed(tr("A transcription is already running."));
        return;
    }
    if (!isAvailable()) {
        emit failed(tr("Whisper is not configured: a CLI on PATH (or an explicit path) and a "
                       "readable ggml model file are both required."));
        return;
    }
    const QFileInfo input(audioPath);
    if (!input.exists() || !input.isFile()) {
        emit failed(tr("No such audio file: %1").arg(audioPath));
        return;
    }

    // Output goes to a directory of its own so `-oj` never drops a .json next to
    // the user's audio, and so a cancelled run leaves nothing behind in the
    // library.
    tempDir_ = QDir::temp().filePath(QStringLiteral("chadvis-whisper-%1")
                                             .arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (!QDir().mkpath(tempDir_)) {
        emit failed(tr("Could not create a temporary directory for whisper output."));
        return;
    }
    outputPrefix_ = QDir(tempDir_).filePath(QStringLiteral("transcript"));
    audioPath_ = input.absoluteFilePath();
    logTail_.clear();
    running_ = true;

    QStringList arguments;
    arguments << QStringLiteral("-m") << options_.modelPath << QStringLiteral("-f") << audioPath_
              << QStringLiteral("-oj") << QStringLiteral("-of") << outputPrefix_;
    if (options_.wordTimestamps) {
        // One word per segment is what the karaoke renderer wants; without it
        // whisper returns clause-sized chunks and the timing is interpolated.
        arguments << QStringLiteral("-ml") << QStringLiteral("1");
    }
    if (!options_.language.isEmpty() && options_.language != QStringLiteral("auto")) {
        arguments << QStringLiteral("-l") << options_.language;
    }
    if (options_.threads > 0) {
        arguments << QStringLiteral("-t") << QString::number(options_.threads);
    }
    arguments << QStringLiteral("--print-progress");

    LOG_INFO("WhisperTranscriber: {} {} (model {})", findExecutable().toStdString(),
             audioPath_.toStdString(), options_.modelPath.toStdString());

    emit started(audioPath_);
    process_->setProgram(findExecutable());
    process_->setArguments(arguments);
    process_->start();
}

void WhisperTranscriber::cancel() {
    if (!running_ || !process_) {
        return;
    }
    LOG_INFO("WhisperTranscriber: cancelled by caller");
    process_->kill();
}

void WhisperTranscriber::onReadyRead() {
    if (!process_) {
        return;
    }
    const QByteArray chunk = process_->readAll();
    if (chunk.isEmpty()) {
        return;
    }

    // whisper-cli reports `progress =  37%` on its own stream. Cheap to surface,
    // and the only feedback a multi-minute job gives.
    static const QRegularExpression progressPattern(QStringLiteral("progress\\s*=\\s*(\\d+)%"));
    const QString text = QString::fromUtf8(chunk);
    auto match = progressPattern.globalMatch(text);
    while (match.hasNext()) {
        const int percent = match.next().captured(1).toInt();
        emit progressChanged(std::clamp(percent, 0, 100));
    }

    logTail_.append(chunk);
    if (logTail_.size() > kMaxLogTail) {
        logTail_ = logTail_.right(kMaxLogTail);
    }
}

void WhisperTranscriber::onFinished(int exitCode, int exitStatus) {
    running_ = false;
    const QString prefix = outputPrefix_;
    const QString tempDir = tempDir_;
    outputPrefix_.clear();
    tempDir_.clear();

    const auto cleanup = [&tempDir]() {
        if (!tempDir.isEmpty()) {
            QDir(tempDir).removeRecursively();
        }
    };

    if (exitStatus != 0 || exitCode != 0) {
        const QString tail = QString::fromUtf8(logTail_).trimmed();
        cleanup();
        emit failed(tail.isEmpty()
                            ? tr("Whisper exited with code %1.").arg(exitCode)
                            : tr("Whisper failed (code %1): %2").arg(exitCode).arg(tail));
        return;
    }

    QFile file(prefix + QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly)) {
        cleanup();
        emit failed(tr("Whisper produced no JSON at %1.").arg(file.fileName()));
        return;
    }
    const QByteArray json = file.readAll();
    file.close();
    cleanup();

    QString error;
    std::optional<WhisperTranscript> transcript = parseWhisperJson(json, &error);
    if (!transcript) {
        emit failed(error);
        return;
    }
    if (transcript->empty()) {
        // Silence, or music with no words. A real answer, not an error.
        LOG_INFO("WhisperTranscriber: transcript is empty for {}",
                 audioPath_.toStdString());
    } else {
        LOG_INFO("WhisperTranscriber: {} segments, {} words, language {}",
                 static_cast<i64>(transcript->segments.size()),
                 static_cast<i64>(transcript->wordCount()), transcript->language);
    }
    emit finished(*transcript);
}

} // namespace vc::lyrics
