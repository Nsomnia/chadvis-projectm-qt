#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
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

    /// Creation inputs: the `POST /persona/create/` body fields from
    /// ENDPOINT-INVENTORY §5.14, in the captured order. `voxAudioId` names the
    /// processed-clip (vocal-stem) id, not a gen id; the range and the clips
    /// are the stem's selection and the gen clip it was cut from. `persona_type`
    /// is always `"vox"`; it is not a parameter.
    struct CreatePersonaParams {
        QString rootClipId;
        QString name;
        QString description;
        bool isPublic{false};
        QString voxAudioId;
        double vocalStartS{0};
        double vocalEndS{0};
        QString userInputStyles;
        QStringList clips;
    };

    struct State {
        /// Filled by loadList(), empty after loadOne().
        QList<Persona> records;
        /// Filled by loadOne(), empty after loadList().
        Persona single;
        /// The raw reply object from a successful createPersona(), empty until
        /// the first success. The create response shape is not canonical in
        /// the inventory (§5.14 captures only the request body), so it is kept
        /// verbatim: a 2xx settles success, and nothing beyond that is read.
        QJsonObject created;
        bool loading{false};
        bool creating{false};
        /// Empty when the last load succeeded.
        QString failureReason;
    };

    explicit SunoPersonaService(SunoClient* client, QObject* parent = nullptr);

    /// Accepts the known envelope spellings plus a bare array.
    static std::expected<QList<Persona>, QString> parseList(const QJsonValue& root);
    /// Unwraps `{persona: {...}}` or a bare persona object.
    static std::expected<Persona, QString> parseOne(const QJsonValue& root);
    /// The exact `POST /persona/create/` body for `params`, in captured order.
    static QJsonObject buildCreateBody(const CreatePersonaParams& params);

    void loadList();
    void loadOne(const QString& personaId);
    /// Spends credits. Refuses with `failed` when `rootClipId` or `voxAudioId`
    /// is empty, without issuing any request.
    void createPersona(const CreatePersonaParams& params);

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const QList<Persona>& records() const { return state_.records; }
    [[nodiscard]] const Persona& persona() const { return state_.single; }
    [[nodiscard]] const QJsonObject& created() const { return state_.created; }
    [[nodiscard]] QString failureReason() const { return state_.failureReason; }
    [[nodiscard]] bool isLoading() const { return state_.loading; }
    [[nodiscard]] bool isCreating() const { return state_.creating; }

signals:
    void changed();
    void failed(const QString& message);

private:
    enum class Route { List, One, Create };

    void enqueue(Route route, const QString& path, const QString& method = QStringLiteral("GET"),
                 const QByteArray& data = {});
    void handleReply(QNetworkReply* reply, Route route, quint64 generation);
    void fail(const QString& message);

    SunoClient* client_;
    State state_;
    quint64 generation_{0};
};

} // namespace vc::suno
