#include "SunoDatabase.hpp"
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include "core/Logger.hpp"

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
                    "model_name TEXT, "
                    "status TEXT, "
                    "created_at TEXT, "
                    "prompt TEXT, "
                    "tags TEXT, "
                    "lyrics TEXT, "
                    "type TEXT, "
                    "aligned_lyrics_json TEXT"
                    ")")) {
        return Result<void>::err("Failed to create clips table: " +
                                 query.lastError().text().toStdString());
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
            "image_url, "
            "model_name, status, created_at, prompt, tags, lyrics, type) "
            "VALUES (:id, :title, :audio_url, :video_url, :image_url, "
            ":model_name, :status, :created_at, :prompt, :tags, :lyrics, "
            ":type)");

    query.bindValue(":id", QString::fromStdString(clip.id));
    query.bindValue(":title", QString::fromStdString(clip.title));
    query.bindValue(":audio_url", QString::fromStdString(clip.audio_url));
    query.bindValue(":video_url", QString::fromStdString(clip.video_url));
    query.bindValue(":image_url", QString::fromStdString(clip.image_url));
    query.bindValue(":model_name", QString::fromStdString(clip.model_name));
    query.bindValue(":status", QString::fromStdString(clip.status));
    query.bindValue(":created_at", QString::fromStdString(clip.created_at));
    query.bindValue(":prompt", QString::fromStdString(clip.metadata.prompt));
    query.bindValue(":tags", QString::fromStdString(clip.metadata.tags));
    query.bindValue(":lyrics", QString::fromStdString(clip.metadata.lyrics));
    query.bindValue(":type", QString::fromStdString(clip.metadata.type));

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
        clip.model_name = query.value("model_name").toString().toStdString();
        clip.status = query.value("status").toString().toStdString();
        clip.created_at = query.value("created_at").toString().toStdString();
        clip.metadata.prompt = query.value("prompt").toString().toStdString();
        clip.metadata.tags = query.value("tags").toString().toStdString();
        clip.metadata.lyrics = query.value("lyrics").toString().toStdString();
        clip.metadata.type = query.value("type").toString().toStdString();
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

} // namespace vc::suno
