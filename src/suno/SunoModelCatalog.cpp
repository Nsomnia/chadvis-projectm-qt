#include "SunoModelCatalog.hpp"

#include "ClipParser.hpp"
#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace vc::suno {

namespace {

QStringList toQStringList(const std::vector<std::string>& values)
{
    QStringList out;
    out.reserve(static_cast<int>(values.size()));
    for (const std::string& value : values) {
        out.append(QString::fromStdString(value));
    }
    return out;
}

QStringList combinationFromValue(const QJsonValue& value)
{
    if (value.isArray()) {
        return toQStringList(json::stringList(value));
    }
    // A bare string is a one-element combination; the capture may also wrap the
    // list in an object, in which case either known key is accepted.
    if (value.isString()) {
        return QStringList{value.toString()};
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const QString& key :
             {QStringLiteral("conditions"), QStringLiteral("capabilities")}) {
            if (object.value(key).isArray()) {
                return toQStringList(json::stringList(object.value(key)));
            }
        }
    }
    return {};
}

SunoModelInfo parseModel(const QJsonObject& object)
{
    using P = ClipParser;
    SunoModelInfo model;
    model.name = P::optString(object, QStringLiteral("name")).toStdString();
    model.external_key = P::optString(object, QStringLiteral("external_key")).toStdString();
    model.major_version =
            json::stringValue(object.value(QStringLiteral("major_version"))).toStdString();
    model.description = P::optString(object, QStringLiteral("description")).toStdString();
    model.can_use = P::optBool(object, QStringLiteral("can_use"), false);
    model.is_default_model = P::optBool(object, QStringLiteral("is_default_model"), false);
    model.is_default_free_model =
            P::optBool(object, QStringLiteral("is_default_free_model"), false);
    model.capabilities = json::stringList(object.value(QStringLiteral("capabilities")));
    model.features = json::stringList(object.value(QStringLiteral("features")));
    model.badges = json::stringList(object.value(QStringLiteral("badges")));

    const QJsonObject limits = object.value(QStringLiteral("max_lengths")).toObject();
    model.max_lengths.title = P::optInt(limits, QStringLiteral("title"));
    model.max_lengths.prompt = P::optInt(limits, QStringLiteral("prompt"));
    model.max_lengths.tags = P::optInt(limits, QStringLiteral("tags"));
    model.max_lengths.negative_tags = P::optInt(limits, QStringLiteral("negative_tags"));
    model.max_lengths.gpt_description_prompt =
            P::optInt(limits, QStringLiteral("gpt_description_prompt"));
    return model;
}

} // namespace

SunoModelCatalog::SunoModelCatalog(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

std::expected<SunoGenerationConfig, QString>
SunoModelCatalog::parseGenerationConfig(const QJsonObject& root)
{
    const QJsonValue raw = root.value(QStringLiteral("generation_config"));
    if (raw.isUndefined() || raw.isNull()) {
        return SunoGenerationConfig{};
    }
    if (!raw.isObject()) {
        return std::unexpected(
                QStringLiteral("The model catalogue's generation_config is not an object."));
    }
    const QJsonObject config = raw.toObject();

    SunoGenerationConfig out;
    out.majorVersion =
            json::stringValue(config.value(QStringLiteral("major_version")));
    out.capabilities =
            toQStringList(json::stringList(config.value(QStringLiteral("capabilities"))));

    const QJsonValue combinations = config.value(QStringLiteral("allowed_condition_combinations"));
    if (combinations.isArray()) {
        const QJsonArray entries = combinations.toArray();
        out.allowedConditionCombinations.reserve(entries.size());
        for (const QJsonValue& entry : entries) {
            QStringList combination = combinationFromValue(entry);
            if (!combination.isEmpty()) {
                out.allowedConditionCombinations.append(std::move(combination));
            }
        }
    }

    const QJsonObject limits = config.value(QStringLiteral("max_lengths")).toObject();
    out.maxLengths.title = ClipParser::optInt(limits, QStringLiteral("title"));
    out.maxLengths.prompt = ClipParser::optInt(limits, QStringLiteral("prompt"));
    out.maxLengths.tags = ClipParser::optInt(limits, QStringLiteral("tags"));
    out.maxLengths.negative_tags = ClipParser::optInt(limits, QStringLiteral("negative_tags"));
    out.maxLengths.gpt_description_prompt =
            ClipParser::optInt(limits, QStringLiteral("gpt_description_prompt"));
    return out;
}

std::expected<QList<SunoModelInfo>, QString> SunoModelCatalog::parseItems(const QJsonObject& root)
{
    const QJsonValue raw = root.value(QStringLiteral("items"));
    if (!raw.isArray()) {
        return QList<SunoModelInfo>{};
    }
    QList<SunoModelInfo> out;
    const QJsonArray entries = raw.toArray();
    out.reserve(entries.size());
    for (const QJsonValue& entry : entries) {
        if (!entry.isObject()) {
            continue;
        }
        SunoModelInfo model = parseModel(entry.toObject());
        // The same identity rule the account manager applies: an entry with
        // neither a name nor a key is not a model, it is noise.
        if (!model.name.empty() || !model.external_key.empty()) {
            out.append(std::move(model));
        }
    }
    return out;
}

void SunoModelCatalog::load()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_ = {};
    loading_ = true;
    ++generation_;
    emit catalogChanged();

    const quint64 generation = generation_;
    QPointer<SunoModelCatalog> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::CUSTOM_MODEL_SHARED), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleReply(reply, generation);
            });
}

void SunoModelCatalog::handleReply(QNetworkReply* reply, quint64 generation)
{
    // An answer from an abandoned load is dropped, not merged: the catalogue is
    // one document and a late reply would overwrite a newer one.
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
        fail(QStringLiteral("Couldn't load the model catalogue (HTTP %1).").arg(status));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("Couldn't load the model catalogue: unreadable response body."));
        return;
    }

    const QJsonObject root = document.object();
    auto config = parseGenerationConfig(root);
    if (!config) {
        fail(config.error());
        return;
    }
    auto items = parseItems(root);
    if (!items) {
        fail(items.error());
        return;
    }
    state_ = {};
    state_.generation = std::move(*config);
    state_.items = std::move(*items);
    loading_ = false;
    emit catalogChanged();
}

void SunoModelCatalog::fail(const QString& message)
{
    loading_ = false;
    state_ = {};
    state_.failureReason = message;
    emit failed(message);
}

} // namespace vc::suno
