#include "SunoDatabase.hpp"
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc::suno {

SunoDatabase::SunoDatabase() = default;

SunoDatabase::~SunoDatabase() {
    if (db_.isOpen()) {
        db_.close();
    }
}

Result<void> SunoDatabase::init(const std::string& dbPath) {
    db_ = QSqlDatabase::addDatabase("QSQLITE", "suno_db");
    db_.setDatabaseName(QString::fromStdString(dbPath));

    if (!db_.open()) {
        return Result<void>::err("Failed to open Suno database: " +
                                 db_.lastError().text().toStdString());
    }

    QSqlQuery query(db_);
    // Create clips table
    if (!query.exec("CREATE TABLE IF NOT EXISTS clips ("
                    "id TEXT PRIMARY KEY, "
                    "title TEXT, "
                    "audio_url TEXT, "
                    "video_url TEXT, "
                    "image_url TEXT, "
                    "image_large_url TEXT, "
                    "model_name TEXT, "
                    "major_model_version TEXT, "
                    "display_name TEXT, "
                    "handle TEXT, "
                    "is_liked INTEGER, "
                    "is_trashed INTEGER, "
                    "is_public INTEGER, "
                    "status TEXT, "
                    "created_at TEXT, "
                    "prompt TEXT, "
                    "tags TEXT, "
                    "lyrics TEXT, "
                    "type TEXT, "
                    "duration TEXT, "
                    "error_message TEXT, "
                    "aligned_lyrics_json TEXT"
                    ")")) {
        return Result<void>::err("Failed to create clips table: " +
                                 query.lastError().text().toStdString());
    }

    // Migration: Add missing columns if they don't exist
    QSqlRecord record = db_.record("clips");
    struct Column { QString name; QString type; };
    std::vector<Column> missingColumns = {
        {"image_large_url", "TEXT"},
        {"major_model_version", "TEXT"},
        {"display_name", "TEXT"},
        {"handle", "TEXT"},
        {"is_liked", "INTEGER"},
        {"is_trashed", "INTEGER"},
        {"is_public", "INTEGER"},
        {"duration", "TEXT"},
        {"error_message", "TEXT"},
        {"aligned_lyrics_json", "TEXT"}
    };

    for (const auto& col : missingColumns) {
        if (record.indexOf(col.name) == -1) {
            LOG_INFO("SunoDatabase: Migrating table clips, adding column {}", col.name.toStdString());
            if (!query.exec(QString("ALTER TABLE clips ADD COLUMN %1 %2").arg(col.name, col.type))) {
                LOG_ERROR("SunoDatabase: Failed to add column {}: {}", col.name.toStdString(), query.lastError().text().toStdString());
            }
        }
    }

    // Migration: Convert old x.x duration format to mm:ss
    if (query.exec("SELECT id, duration FROM clips WHERE duration LIKE '%.%'")) {
        while (query.next()) {
            QString id = query.value(0).toString();
            QString durStr = query.value(1).toString();
            bool ok;
            double secs = durStr.toDouble(&ok);
            if (ok) {
                QString formatted = QString::fromStdString(file::formatDuration(Duration(static_cast<i64>(secs * 1000))));
                QSqlQuery updateQuery(db_);
                updateQuery.prepare("UPDATE clips SET duration = :dur WHERE id = :id");
                updateQuery.bindValue(":dur", formatted);
                updateQuery.bindValue(":id", id);
                updateQuery.exec();
            }
        }
    }

    initialized_ = true;
    LOG_INFO("Suno database initialized at {}", dbPath);
    return Result<void>::ok();
}

Result<void> SunoDatabase::saveClip(const SunoClip& clip) {
    if (!initialized_)
        return Result<void>::err("Database not initialized");

    QSqlQuery query(db_);
    query.prepare(
            "INSERT OR REPLACE INTO clips (id, title, audio_url, video_url, "
            "image_url, image_large_url, model_name, major_model_version, "
            "display_name, handle, is_liked, is_trashed, is_public, "
            "status, created_at, prompt, tags, lyrics, type, duration, error_message) "
            "VALUES (:id, :title, :audio_url, :video_url, :image_url, "
            ":image_large_url, :model_name, :major_model_version, :display_name, "
            ":handle, :is_liked, :is_trashed, :is_public, :status, :created_at, "
            ":prompt, :tags, :lyrics, :type, :duration, :error_message)");

    query.bindValue(":id", QString::fromStdString(clip.id));
    query.bindValue(":title", QString::fromStdString(clip.title));
    query.bindValue(":audio_url", QString::fromStdString(clip.audio_url));
    query.bindValue(":video_url", QString::fromStdString(clip.video_url));
    query.bindValue(":image_url", QString::fromStdString(clip.image_url));
    query.bindValue(":image_large_url", QString::fromStdString(clip.image_large_url));
    query.bindValue(":model_name", QString::fromStdString(clip.model_name));
    query.bindValue(":major_model_version", QString::fromStdString(clip.major_model_version));
    query.bindValue(":display_name", QString::fromStdString(clip.display_name));
    query.bindValue(":handle", QString::fromStdString(clip.handle));
    query.bindValue(":is_liked", clip.is_liked ? 1 : 0);
    query.bindValue(":is_trashed", clip.is_trashed ? 1 : 0);
    query.bindValue(":is_public", clip.is_public ? 1 : 0);
    query.bindValue(":status", QString::fromStdString(clip.status));
    query.bindValue(":created_at", QString::fromStdString(clip.created_at));
    query.bindValue(":prompt", QString::fromStdString(clip.metadata.prompt));
    query.bindValue(":tags", QString::fromStdString(clip.metadata.tags));
    query.bindValue(":lyrics", QString::fromStdString(clip.metadata.lyrics));
    query.bindValue(":type", QString::fromStdString(clip.metadata.type));
    query.bindValue(":duration", QString::fromStdString(clip.metadata.duration));
    query.bindValue(":error_message", QString::fromStdString(clip.metadata.error_message));

    if (!query.exec()) {
        return Result<void>::err("Failed to save clip: " +
                                 query.lastError().text().toStdString());
    }

    return Result<void>::ok();
}

Result<void> SunoDatabase::saveClips(const std::vector<SunoClip>& clips) {
    db_.transaction();
    for (const auto& clip : clips) {
        auto res = saveClip(clip);
        if (!res) {
            db_.rollback();
            return res;
        }
    }
    db_.commit();
    return Result<void>::ok();
}

Result<std::vector<SunoClip>> SunoDatabase::getAllClips() {
    if (!initialized_)
        return Result<std::vector<SunoClip>>::err("Database not initialized");

    QSqlQuery query("SELECT * FROM clips ORDER BY created_at DESC", db_);
    std::vector<SunoClip> clips;

    while (query.next()) {
        SunoClip clip;
        clip.id = query.value("id").toString().toStdString();
        clip.title = query.value("title").toString().toStdString();
        clip.audio_url = query.value("audio_url").toString().toStdString();
        clip.video_url = query.value("video_url").toString().toStdString();
        clip.image_url = query.value("image_url").toString().toStdString();
        clip.image_large_url = query.value("image_large_url").toString().toStdString();
        clip.model_name = query.value("model_name").toString().toStdString();
        clip.major_model_version = query.value("major_model_version").toString().toStdString();
        clip.display_name = query.value("display_name").toString().toStdString();
        clip.handle = query.value("handle").toString().toStdString();
        clip.is_liked = query.value("is_liked").toInt() != 0;
        clip.is_trashed = query.value("is_trashed").toInt() != 0;
        clip.is_public = query.value("is_public").toInt() != 0;
        clip.status = query.value("status").toString().toStdString();
        clip.created_at = query.value("created_at").toString().toStdString();
        clip.metadata.prompt = query.value("prompt").toString().toStdString();
        clip.metadata.tags = query.value("tags").toString().toStdString();
        clip.metadata.lyrics = query.value("lyrics").toString().toStdString();
        clip.metadata.type = query.value("type").toString().toStdString();
        clip.metadata.duration = query.value("duration").toString().toStdString();
        clip.metadata.error_message = query.value("error_message").toString().toStdString();
        clips.push_back(clip);
    }

    return Result<std::vector<SunoClip>>::ok(clips);
}

Result<void> SunoDatabase::saveAlignedLyrics(
        const std::string& clipId, const std::string& alignedLyricsJson) {
    if (!initialized_)
        return Result<void>::err("Database not initialized");

    QSqlQuery query(db_);
    query.prepare(
            "UPDATE clips SET aligned_lyrics_json = :json WHERE id = :id");
    query.bindValue(":json", QString::fromStdString(alignedLyricsJson));
    query.bindValue(":id", QString::fromStdString(clipId));

    if (!query.exec()) {
        return Result<void>::err("Failed to save aligned lyrics: " +
                                 query.lastError().text().toStdString());
    }

    return Result<void>::ok();
}

Result<std::string> SunoDatabase::getAlignedLyrics(const std::string& clipId) {
    if (!initialized_)
        return Result<std::string>::err("Database not initialized");

    QSqlQuery query(db_);
    query.prepare("SELECT aligned_lyrics_json FROM clips WHERE id = :id");
    query.bindValue(":id", QString::fromStdString(clipId));

    if (query.exec() && query.next()) {
        return Result<std::string>::ok(query.value(0).toString().toStdString());
    }

    return Result<std::string>::err("Aligned lyrics not found");
}

bool SunoDatabase::hasLyrics(const std::string& clipId) const {
    if (!initialized_)
        return false;

    QSqlQuery query(db_);
    query.prepare("SELECT COUNT(*) FROM clips WHERE id = :id AND (lyrics IS NOT NULL AND lyrics != '')");
    query.bindValue(":id", QString::fromStdString(clipId));

    if (query.exec() && query.next()) {
        return query.value(0).toInt() > 0;
    }

    return false;
}

} // namespace vc::suno
