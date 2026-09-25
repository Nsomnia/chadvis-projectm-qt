#include "SunoAccountManager.hpp"

#include "ClipParser.hpp"
#include "core/Logger.hpp"

#include <QtGlobal>
#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

namespace vc::suno {

namespace {

QString stringOrNumber(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (value.isString()) return value.toString();
    if (!value.isDouble()) return {};
    const double number = value.toDouble();
    if (std::floor(number) == number) {
        return QString::number(static_cast<qint64>(number));
    }
    return QString::number(number, 'g', 15);
}

/// Tolerant string-list read for capabilities/features/badges arrays.
std::vector<std::string> optStringList(const QJsonObject& obj, const QString& key) {
    std::vector<std::string> out;
    const QJsonValue v = obj.value(key);
    if (!v.isArray()) {
        return out;
    }
    for (const auto& item : v.toArray()) {
        if (item.isString()) {
            out.push_back(item.toString().toStdString());
        }
    }
    return out;
}

std::optional<SunoUserSummary> parseUser(const QJsonObject& root) {
    // Session envelope: { user: {...}, models: [...] } — tolerate nesting.
    QJsonObject userObj = root.value(QStringLiteral("user")).toObject();
    if (userObj.isEmpty()) {
        userObj = root.value(QStringLiteral("session")).toObject()
                          .value(QStringLiteral("user")).toObject();
    }
    if (userObj.isEmpty()) {
        userObj = root.value(QStringLiteral("data")).toObject()
                          .value(QStringLiteral("user")).toObject();
    }
    if (userObj.isEmpty() && root.contains(QStringLiteral("id"))) {
        userObj = root; // flat fallback
    }
    if (userObj.isEmpty()) {
        return std::nullopt;
    }

    SunoUserSummary user;
    using P = ClipParser;
    user.email = P::optString(userObj, "email").toStdString();
    user.username = P::optString(userObj, "username").toStdString();
    user.id = P::optString(userObj, "id").toStdString();
    user.clerk_id = P::optString(userObj, "clerk_id").toStdString();
    user.display_name = P::optString(userObj, "display_name").toStdString();
    user.handle = P::optString(userObj, "handle").toStdString();
    user.avatar_image_url = P::optString(userObj, "avatar_image_url").toStdString();
    user.is_vip = P::optBool(userObj, "is_vip", false);
    user.total_clips = P::optInt(userObj, "total_clips");
    return user;
}

QList<SunoModelInfo> parseModels(const QJsonObject& root) {
    QList<SunoModelInfo> models;
    QJsonValue modelsValue = root.value(QStringLiteral("models"));
    if (!modelsValue.isArray()) {
        modelsValue = root.value(QStringLiteral("session")).toObject()
                             .value(QStringLiteral("models"));
    }
    if (!modelsValue.isArray()) {
        modelsValue = root.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("models"));
    }
    if (!modelsValue.isArray()) {
        return models;
    }
    using P = ClipParser;
    for (const auto& entry : modelsValue.toArray()) {
        const QJsonObject m = entry.toObject();
        if (m.isEmpty()) continue;

        SunoModelInfo model;
        model.name = P::optString(m, "name").toStdString();
        model.external_key = P::optString(m, "external_key").toStdString();
        model.major_version = stringOrNumber(m, QStringLiteral("major_version")).toStdString();
        model.description = P::optString(m, "description").toStdString();
        model.can_use = P::optBool(m, "can_use", false);
        model.is_default_model = P::optBool(m, "is_default_model", false);
        model.is_default_free_model = P::optBool(m, "is_default_free_model", false);
        model.capabilities = optStringList(m, "capabilities");
        model.features = optStringList(m, "features");
        model.badges = optStringList(m, "badges");

        const QJsonObject limits =
                m.value(QStringLiteral("max_lengths")).toObject();
        model.max_lengths.title = P::optInt(limits, "title");
        model.max_lengths.prompt = P::optInt(limits, "prompt");
        model.max_lengths.tags = P::optInt(limits, "tags");
        model.max_lengths.negative_tags = P::optInt(limits, "negative_tags");
        model.max_lengths.gpt_description_prompt =
                P::optInt(limits, "gpt_description_prompt");

        if (!model.name.empty() || !model.external_key.empty()) {
            models.push_back(std::move(model));
        }
    }
    return models;
}

std::optional<SunoBillingInfo> parseBilling(const QJsonObject& root) {
    if (root.isEmpty()) {
        return std::nullopt;
    }
    SunoBillingInfo info;
    using P = ClipParser;
    info.credits = P::optInt(root, "credits");
    info.is_active = P::optBool(root, "is_active", false);
    info.subscription_type = P::optBool(root, "subscription_type", false);
    info.period = P::optString(root, "period").toStdString();
    info.monthly_usage = P::optInt(root, "monthly_usage");
    info.monthly_limit = P::optInt(root, "monthly_limit");
    info.renews_on = P::optString(root, "renews_on").toStdString();

    const QJsonObject plan = root.value(QStringLiteral("plan")).toObject();
    info.plan.plan_key = P::optString(plan, "plan_key").toStdString();
    info.plan.name = P::optString(plan, "name").toStdString();
    info.plan.level = stringOrNumber(plan, QStringLiteral("level")).toStdString();
    info.plan.monthly_price_usd = P::optDouble(plan, "monthly_price_usd", 0.0);

    const QJsonValue packs = root.value(QStringLiteral("credit_packs"));
    info.credit_pack_count = packs.isArray() ? static_cast<i64>(packs.toArray().size()) : 0;

    return info;
}

} // namespace

SunoAccountManager::SunoAccountManager(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

SunoAccountManager::~SunoAccountManager() = default;

void SunoAccountManager::refreshAll() {
    if (!client_ || !client_->isAuthenticated()) {
        clearSnapshots();
        return;
    }

    client_->enqueueAuthenticatedRequest(
            qstr(vc::suno::endpoints::SESSION), "GET", {},
            [this](QNetworkReply* reply) { handleSessionReply(reply); });

    refreshBilling();
}

void SunoAccountManager::refreshBilling() {
    if (!client_ || !client_->isAuthenticated()) {
        return;
    }

    client_->enqueueAuthenticatedRequest(
            qstr(vc::suno::endpoints::BILLING_INFO), "GET", {},
            [this](QNetworkReply* reply) { handleBillingReply(reply); });
}

void SunoAccountManager::clearSnapshots() {
    const bool hadSnapshot = user_.has_value() || !models_.isEmpty() ||
                             billing_.has_value();
    user_.reset();
    models_.clear();
    billing_.reset();
    if (hadSnapshot) {
        emit accountInfoReady();
        emit billingInfoReady();
    }
}

void SunoAccountManager::handleSessionReply(QNetworkReply* reply) {
    const bool authenticated = client_ && client_->isAuthenticated();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();

    if (!authenticated) {
        clearSnapshots();
        return;
    }
    if (error != QNetworkReply::NoError) {
        LOG_WARN("SunoAccountManager: session fetch failed: {}",
                 errorString.toStdString());
        emit accountError(errorString);
        return;
    }

    auto user = parseUser(root);
    if (!user) {
        LOG_WARN("SunoAccountManager: session envelope had no usable user object");
        emit accountError(QStringLiteral("session envelope had no user object"));
        return;
    }
    user_ = std::move(*user);
    models_ = parseModels(root);
    LOG_INFO("SunoAccountManager: session loaded ({} model(s), user '{}')",
             static_cast<i64>(models_.size()), user_->display_name);
    emit accountInfoReady();
}

void SunoAccountManager::handleBillingReply(QNetworkReply* reply) {
    const bool authenticated = client_ && client_->isAuthenticated();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();

    if (!authenticated) {
        return;
    }
    if (error != QNetworkReply::NoError) {
        LOG_WARN("SunoAccountManager: billing fetch failed: {}",
                 errorString.toStdString());
        emit accountError(errorString);
        return;
    }

    auto info = parseBilling(root);
    if (!info) {
        emit accountError(QStringLiteral("billing envelope was empty"));
        return;
    }
    billing_ = *info;
    emit billingInfoReady();
}

} // namespace vc::suno
