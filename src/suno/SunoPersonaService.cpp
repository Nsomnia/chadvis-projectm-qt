#include "SunoPersonaService.hpp"

#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include <utility>

namespace vc::suno {

namespace {

/// The edit body names the identity `persona_id`; a read may also carry `id`.
QString personaId(const QJsonObject& object)
{
    const QJsonValue primary = object.value(QStringLiteral("persona_id"));
    if (primary.isString()) {
        return primary.toString();
    }
    if (primary.isDouble()) {
        return QString::number(primary.toDouble());
    }
    const QJsonValue fallback = object.value(QStringLiteral("id"));
    if (fallback.isString()) {
        return fallback.toString();
    }
    if (fallback.isDouble()) {
        return QString::number(fallback.toDouble());
    }
    return {};
}

QString unreadableBody()
{
    return QStringLiteral("Couldn't load personas: unreadable response body.");
}

} // namespace

SunoPersonaService::SunoPersonaService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

std::expected<QList<SunoPersonaService::Persona>, QString>
SunoPersonaService::parseList(const QJsonValue& root)
{
    QJsonArray entries;
    if (root.isArray()) {
        entries = root.toArray();
    } else if (root.isObject()) {
        static const QStringList keys{QStringLiteral("personas"), QStringLiteral("results"),
                                      QStringLiteral("items"), QStringLiteral("data")};
        bool found = false;
        for (const QString& key : keys) {
            const QJsonValue value = root.toObject().value(key);
            if (value.isArray()) {
                entries = value.toArray();
                found = true;
                break;
            }
        }
        if (!found) {
            return std::unexpected(
                    QStringLiteral("Persona list response did not contain a persona list."));
        }
    } else {
        return std::unexpected(
                QStringLiteral("Persona list response was not a JSON object."));
    }

    QList<Persona> out;
    out.reserve(entries.size());
    for (const QJsonValue& entry : entries) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject object = entry.toObject();
        Persona persona{personaId(object), object.value(QStringLiteral("name")).toString(),
                        object};
        // An entry with neither identity nor name is noise, not a persona.
        if (persona.id.isEmpty() && persona.name.isEmpty()) {
            continue;
        }
        out.append(std::move(persona));
    }
    return out;
}

std::expected<SunoPersonaService::Persona, QString>
SunoPersonaService::parseOne(const QJsonValue& root)
{
    if (!root.isObject()) {
        return std::unexpected(QStringLiteral("Persona response was not a JSON object."));
    }
    QJsonObject object = root.toObject();
    const QJsonValue nested = object.value(QStringLiteral("persona"));
    if (nested.isObject()) {
        object = nested.toObject();
    }
    Persona persona{personaId(object), object.value(QStringLiteral("name")).toString(), object};
    if (persona.id.isEmpty() && persona.name.isEmpty()) {
        return std::unexpected(QStringLiteral("Persona response did not contain a persona."));
    }
    return persona;
}

void SunoPersonaService::loadList()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_ = {};
    state_.loading = true;
    ++generation_;
    emit changed();

    enqueue(Route::List, qstr(endpoints::PERSONA_LIST));
}

void SunoPersonaService::loadOne(const QString& personaId)
{
    // No id is "nothing to ask about", not an auth failure.
    if (personaId.isEmpty()) {
        return;
    }
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_ = {};
    state_.loading = true;
    ++generation_;
    emit changed();

    const QString path = QString(qstr(endpoints::PERSONA_GET))
                                 .replace(QStringLiteral("{}"),
                                          QString::fromLatin1(QUrl::toPercentEncoding(personaId)));
    enqueue(Route::One, path);
}

void SunoPersonaService::enqueue(Route route, const QString& path)
{
    // One request at a time: a second load abandons the first, and a late reply
    // must not overwrite the newer state.
    const quint64 generation = generation_;
    QPointer<SunoPersonaService> guard(this);
    client_->enqueueAuthenticatedRequest(
            path, "GET", {}, [guard, route, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleReply(reply, route, generation);
            });
}

void SunoPersonaService::handleReply(QNetworkReply* reply, Route route, quint64 generation)
{
    if (generation != generation_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();

    if (!body) {
        fail(body.error());
        return;
    }
    if (status < 200 || status >= 300) {
        fail(QStringLiteral("Couldn't load personas (HTTP %1).").arg(status));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(unreadableBody());
        return;
    }
    const QJsonValue root = document.isArray() ? QJsonValue(document.array())
                                               : QJsonValue(document.object());
    if (route == Route::List) {
        auto records = parseList(root);
        if (!records) {
            fail(records.error());
            return;
        }
        state_ = {};
        state_.records = std::move(*records);
    } else {
        auto one = parseOne(root);
        if (!one) {
            fail(one.error());
            return;
        }
        state_ = {};
        state_.single = std::move(*one);
    }
    state_.loading = false;
    emit changed();
}

void SunoPersonaService::fail(const QString& message)
{
    state_ = {};
    state_.failureReason = message;
    emit failed(message);
}

} // namespace vc::suno
