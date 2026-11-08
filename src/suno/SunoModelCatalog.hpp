#pragma once

#include "SunoModels.hpp"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <expected>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// `generation_config` of `GET /custom-model/shared`: the only server-side
/// statement of what a model can do and which capabilities may be chained.
///
/// Every field is the server's own data, kept structured. `capabilities` keeps
/// the captured order because the UI may present it that way, and the
/// combinations are held as lists rather than a joined string because
/// `allowed_condition_combinations` is the authority on chaining: a caller
/// checks membership, and a re-serialised string would only make that harder.
struct SunoGenerationConfig {
    QString majorVersion;
    QStringList capabilities;
    QList<QStringList> allowedConditionCombinations;
    SunoModelLimits maxLengths;
};

/// Read-only model catalogue: capabilities, allowed chaining combinations, and
/// the per-model character limits surfaced by `GET /custom-model/shared`.
///
/// A failure leaves the catalogue empty and names the reason, so a caller asking
/// "can this model cover?" sees an explicit failure rather than a stale answer.
class SunoModelCatalog final : public QObject
{
    Q_OBJECT

public:
    struct State {
        SunoGenerationConfig generation;
        QList<SunoModelInfo> items;
        /// Empty when the last load succeeded.
        QString failureReason;
    };

    explicit SunoModelCatalog(SunoClient* client, QObject* parent = nullptr);

    /// Missing `generation_config` is an empty config, not an error: the
    /// catalogue is still worth showing. A present but non-object value is
    /// malformed and reported.
    static std::expected<SunoGenerationConfig, QString>
    parseGenerationConfig(const QJsonObject& root);
    /// `items[]` entries mapped onto the same record the account manager reads.
    /// A non-array `items` is empty, and malformed entries are skipped.
    static std::expected<QList<SunoModelInfo>, QString> parseItems(const QJsonObject& root);

    void load();

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const SunoGenerationConfig& generation() const { return state_.generation; }
    [[nodiscard]] const QList<SunoModelInfo>& items() const { return state_.items; }
    [[nodiscard]] QString failureReason() const { return state_.failureReason; }
    [[nodiscard]] bool isLoading() const { return loading_; }

signals:
    void catalogChanged();
    void failed(const QString& message);

private:
    void handleReply(QNetworkReply* reply, quint64 generation);
    void fail(const QString& message);

    SunoClient* client_;
    State state_;
    bool loading_{false};
    quint64 generation_{0};
};

} // namespace vc::suno
