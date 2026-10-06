#include "SunoAccountManager.hpp"

#include "ClipParser.hpp"
#include "core/Logger.hpp"

#include <QtGlobal>
#include <cmath>
#include <expected>

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

    // `accessible_features` — the 22-entry PlanFeature entitlement plane, and the
    // last of the seven gating planes this client was fetching and discarding.
    // Each element is an object carrying a single `name`, so both the object form
    // and a bare string are accepted: the wire has been seen only in the object
    // form, and accepting a bare string costs nothing while surviving a shape
    // change. A non-object element is skipped rather than coerced.
    const QJsonValue features = root.value(QStringLiteral("accessible_features"));
    if (features.isArray()) {
        for (const auto& entry : features.toArray()) {
            if (entry.isObject()) {
                const QJsonValue name = entry.toObject().value(QStringLiteral("name"));
                if (name.isString() && !name.toString().isEmpty()) {
                    info.accessible_features.push_back(name.toString().toStdString());
                }
            } else if (entry.isString() && !entry.toString().isEmpty()) {
                info.accessible_features.push_back(entry.toString().toStdString());
            }
        }
    }

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
    billingModels_.clear();
    billing_.reset();
    // Drop the flag map with the account it described. Leaving it behind would
    // keep every gate reading as it did for the signed-out user, so a UI could
    // show a surface as available to an account that no longer exists — and the
    // empty set is the honest one, since it makes every flag-gated surface read
    // `ServerGated` rather than stale-available.
    gates_.setCapabilities(SessionCapabilities{});
    emit gatesChanged();
    if (hadSnapshot) {
        emit accountInfoReady();
        emit billingInfoReady();
    }
}

void SunoAccountManager::handleSessionReply(QNetworkReply* reply) {
    const bool authenticated = client_ && client_->isAuthenticated();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();

    // Body before error, and the ordering is the point. SunoClient arms every
    // reply it issues with a readyRead-driven BodyReader, so a session body past
    // the studio-API cap was aborted mid-transfer and errorString() reports
    // "Operation canceled" -- which tells a user nothing about why their account
    // panel is empty. The sticky refusal names the limit instead.
    //
    // It is also the only path that still holds the bytes: this reply has already
    // been pumped, so readAll() would return an empty QByteArray, the parse would
    // produce an empty object, and the failure would be reported as "session
    // envelope had no user object" -- a wrong diagnosis of a real fault.
    auto body = client_ ? client_->readTrackedBody(reply)
                        : std::expected<QByteArray, QString>(std::unexpected(
                              QStringLiteral("account manager has no SunoClient to read the "
                                             "response with")));
    reply->deleteLater();

    if (!authenticated) {
        clearSnapshots();
        return;
    }
    if (!body) {
        LOG_WARN("SunoAccountManager: session response refused: {}",
                 body.error().toStdString());
        emit accountError(body.error());
        return;
    }
    if (error != QNetworkReply::NoError) {
        LOG_WARN("SunoAccountManager: session fetch failed: {}",
                 errorString.toStdString());
        emit accountError(errorString);
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(*body).object();

    // Capture the gating plane BEFORE the user parse, and unconditionally.
    //
    // Two reasons for the ordering. First, this is the whole point of the call:
    // the envelope carries 47 server flags anonymously, 57 authenticated, 58 on
    // staging, plus roles, statsig custom properties, `experiments` and
    // `configs.gen-endpoint` — and all of it was being discarded, so every gate
    // in the tree had to be a hardcoded `false`. Second, doing it before the
    // `parseUser` early-return means an envelope that carries a flag map but no
    // usable user object still yields the flag map. Losing the account because
    // one optional field was absent would lose the gating plane with it.
    gates_.setCapabilities(parseSessionCapabilities(root));
    emit gatesChanged();

    auto user = parseUser(root);
    if (!user) {
        LOG_WARN("SunoAccountManager: session envelope had no usable user object");
        emit accountError(QStringLiteral("session envelope had no user object"));
        return;
    }
    user_ = std::move(*user);
    models_ = parseModels(root);
    LOG_INFO("SunoAccountManager: session loaded ({} model(s), {} server flag(s), user '{}')",
             static_cast<i64>(models_.size()),
             static_cast<i64>(gates_.capabilities().flagCount()),
             user_->display_name);
    emit accountInfoReady();
}

void SunoAccountManager::handleBillingReply(QNetworkReply* reply) {
    const bool authenticated = client_ && client_->isAuthenticated();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();

    auto body = client_ ? client_->readTrackedBody(reply)
                        : std::expected<QByteArray, QString>(std::unexpected(
                              QStringLiteral("account manager has no SunoClient to read the "
                                             "response with")));
    reply->deleteLater();

    if (!authenticated) {
        return;
    }
    if (!body) {
        LOG_WARN("SunoAccountManager: billing response refused: {}",
                 body.error().toStdString());
        emit accountError(body.error());
        return;
    }
    if (error != QNetworkReply::NoError) {
        LOG_WARN("SunoAccountManager: billing fetch failed: {}",
                 errorString.toStdString());
        emit accountError(errorString);
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(*body).object();

    // The model catalogue lives here, not in `/api/session/`. Measured against
    // the captured bodies, the session `models` array is EMPTY in every capture
    // while this one carries the real catalogue with the identical field shape —
    // so `parseModels` is reused verbatim rather than a second parser being
    // written. It is parsed before the billing parse below so a billing envelope
    // that fails to yield a plan still yields a catalogue, which is the more
    // useful half.
    billingModels_ = parseModels(root);

    auto info = parseBilling(root);
    if (!info) {
        LOG_WARN("SunoAccountManager: billing envelope had no usable plan; kept the "
                 "{} model(s) it did carry",
                 static_cast<i64>(billingModels_.size()));
        emit accountError(QStringLiteral("billing envelope was empty"));
        return;
    }
    billing_ = *info;
    // `billingInfoReady` alone. An earlier revision of this also emitted
    // `accountInfoReady` so the model catalogue would reach the UI, which is
    // wrong on two counts: that signal means the USER object, not the model
    // list, and it made a billing-only refresh masquerade as an account
    // refresh -- `test_BoundedBody` caught it counting two emissions where the
    // envelope produced one. The catalogue is re-read on `billingInfoReady`,
    // which is the honest signal for "this reply changed account-derived data".
    emit billingInfoReady();
}

} // namespace vc::suno
