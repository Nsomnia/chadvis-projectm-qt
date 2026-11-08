#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <expected>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// Reads of the persona surfaces: `GET /persona/get-personas/` and
/// `GET /persona/get-persona/{id}/`.
///
/// Both are idempotent and unmetered, so they are wired before the
/// credit-spending `POST /persona/create/`. Only the identity the capture pins
/// down is typed; every other field stays in `raw` so a field this build has
/// not seen survives a round trip instead of being dropped. A malformed reply
/// leaves the state empty and names the reason rather than failing the caller.
class SunoPersonaService final : public QObject
{
    Q_OBJECT

public:
    struct Persona {
        QString id;
        QString name;
        /// The record verbatim: the read shapes beyond `persona_id`/`name` are
        /// not canonical, so they are preserved rather than mapped.
        QJsonObject raw;
    };

    struct State {
        /// Filled by loadList(), empty after loadOne().
        QList<Persona> records;
        /// Filled by loadOne(), empty after loadList().
        Persona single;
        bool loading{false};
        /// Empty when the last load succeeded.
        QString failureReason;
    };

    explicit SunoPersonaService(SunoClient* client, QObject* parent = nullptr);

    /// Accepts the known envelope spellings plus a bare array.
    static std::expected<QList<Persona>, QString> parseList(const QJsonValue& root);
    /// Unwraps `{persona: {...}}` or a bare persona object.
    static std::expected<Persona, QString> parseOne(const QJsonValue& root);

    void loadList();
    void loadOne(const QString& personaId);

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const QList<Persona>& records() const { return state_.records; }
    [[nodiscard]] const Persona& persona() const { return state_.single; }
    [[nodiscard]] QString failureReason() const { return state_.failureReason; }
    [[nodiscard]] bool isLoading() const { return state_.loading; }

signals:
    void changed();
    void failed(const QString& message);

private:
    enum class Route { List, One };

    void enqueue(Route route, const QString& path);
    void handleReply(QNetworkReply* reply, Route route, quint64 generation);
    void fail(const QString& message);

    SunoClient* client_;
    State state_;
    quint64 generation_{0};
};

} // namespace vc::suno
