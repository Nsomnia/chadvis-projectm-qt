#pragma once
/**
 * @file SunoWorkspaceBridge.hpp
 * @file Purpose: the QML-facing bridge for `vc::suno::SunoWorkspace` — the LOCAL
 *                creation state (authored prompt, region edits, lyrics text) and the
 *                submission of a render to `RenderQueue`.
 *
 * @section This bridge is deliberately NOT registered
 * `BridgeRegistration.cpp` carries the reason at length and that comment is the
 * authority; nothing here may add the registration line. The short form: the
 * workspace underneath is honest, but two producer-side gaps remain and neither is
 * in this file. The bridge `new`s its own workspace (see the constructor) and so
 * has no way to give it a `RenderQueue`, which makes `startRender` refuse with
 * `NoQueueAttached`; and there is no `RenderFrameBackend` in the tree, so the only
 * render outcome reachable on this machine is `FailedPermanent`.
 *
 * @section What was deleted from this surface, and why
 * The previous version of this header advertised `generating`, `generatedClips`,
 * `cancelGeneration`, `stems`, `requestStems` and a `renderProgress` that was an
 * `f32` fraction. **Every one of those had no producer.** `generating` read
 * `SunoWorkspace::isGenerating()`, a member nothing ever assigned, so it reported
 * `false` forever while the Generate button it fed would have been pressed;
 * `generatedClips()` read a vector that was only ever *written* by the class it read
 * from, so it was permanently empty; `requestStems()` called a method that does not
 * exist; and `renderProgress` was a second unit for one progress bar, which is the
 * exact disagreement the workspace rewrite removed. They are gone rather than stubbed,
 * because a QML property that always reads a constant is a promise the C++ cannot
 * keep.
 *
 * @section How a refusal reaches QML
 * `SunoWorkspace` returns every refusal as a `std::expected` and deliberately emits
 * no `errorOccurred`, because one broadcast string consumed by every subsystem is
 * the recorded cause of a live phantom-failure bug in this tree. QML cannot read an
 * `std::expected`, so the three invokables that can refuse (`startGeneration`,
 * `startRender`, `saveWorkspace`/`loadWorkspace`) return a `QVariantMap` instead of
 * swallowing the reason, and `lastError` exposes the same text for a surface that
 * only wants to read a property.
 *
 * @section Progress is an integer percent, including -1
 * `renderProgressPercent` is 0..100 or **-1 for "no denominator is known"**, which is
 * `RenderResult::progressPercent`'s own convention. -1 is forwarded untouched and is
 * never clamped to 0: "0% of nothing" and "0% of a lot" are different claims, and a
 * bar that cannot measure its denominator must say so rather than look stuck at the
 * start. Nothing in this file converts a percent to a fraction.
 */

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "QmlSingletonBridge.hpp"
#include "suno/SunoWorkspace.hpp"

namespace qml_bridge {

class SunoWorkspaceBridge
    : public QObject,
      public QmlSingletonBridge<SunoWorkspaceBridge, SingletonPolicy::CachedUnparented> {
    friend class QmlSingletonBridge<SunoWorkspaceBridge, SingletonPolicy::CachedUnparented>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit SunoWorkspaceBridge(QObject* parent = nullptr);

    // ── Regions: the one authored timeline, and it has a real producer ──────
    /// Every mutator answers whether the list actually changed.
    ///
    /// `SunoWorkspace::addRegion` ignores a range where `startS >= endS` and
    /// `clearRegions` is silent on an already-empty list — both deliberate, because
    /// an inverted range adds a row no amount of rendering can use. Returning the
    /// verdict is what lets a QML caller learn its row was refused instead of
    /// watching it silently fail to appear.
    Q_PROPERTY(QVariantList regions READ regions NOTIFY regionsChanged)
    Q_INVOKABLE bool addRegion(const QString& clipId, double startS, double endS,
                               const QString& label = {});
    Q_INVOKABLE bool removeRegion(int index);
    Q_INVOKABLE bool clearRegions();
    QVariantList regions() const;

    // ── Lyrics: real, and persisted ─────────────────────────────────────────
    Q_PROPERTY(QString lyricsText READ lyricsText NOTIFY lyricsChanged)
    Q_INVOKABLE void setLyricsText(const QString& text);
    QString lyricsText() const;

    // ── Generation: refused, always, and it says so in the return value ─────
    /// Always returns a map whose `ok` is `false` and whose `kind` is
    /// `generation-unavailable`. The authored text is recorded and persisted before
    /// the refusal, so a prompt is never lost to a refusal.
    Q_INVOKABLE QVariantMap startGeneration(const QString& prompt, const QString& tags,
                                            bool instrumental, const QString& model = {});

    // ── Render: real, through a `RenderQueue` this bridge does not have ─────
    /// `CONSTANT` because nothing can attach one: the workspace is constructed in
    /// this file's constructor and `registerBridges` has no queue argument. It
    /// becomes a notifying property on the day the injection seam exists, and that
    /// change belongs with the seam rather than being pre-announced here.
    Q_PROPERTY(bool renderQueueAttached READ renderQueueAttached CONSTANT)
    /// A queued job IS live — `SunoWorkspace::isRendering()` is false unless an id
    /// is present, and an id is present from `renderStarted` to any terminal state.
    Q_PROPERTY(bool renderActive READ renderActive NOTIFY renderStateChanged)
    /// Empty unless a render is live. The job id, not an ordinal, because it is
    /// what a receipt, a cancel and a progress report are all keyed on.
    Q_PROPERTY(QString renderJobId READ renderJobId NOTIFY renderStateChanged)
    /// 0..100, or -1 for "unknown". See the file header.
    Q_PROPERTY(int renderProgressPercent READ renderProgressPercent NOTIFY renderProgressChanged)
    /// `state().lastError`, mirrored so a QML surface can read the reason without
    /// parsing the return value of an invokable. See `publishLastError()` for why
    /// this bridge has to announce the change itself.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    /// Submit `jobFields` to the attached queue. Returns `{ok: true}` on admission
    /// and otherwise a map naming the refusal. **In this build it always refuses**,
    /// with `kind: "no-queue-attached"`, and that refusal is the point rather than
    /// something to work around: a Render button that submits and reports a stall
    /// with no cause is what the workspace rewrite removed.
    ///
    /// `jobFields` is keyed by `RenderJob`'s own field names and nothing else; an
    /// unknown key is refused rather than ignored. See `jobFromMap()` in the .cpp
    /// for why that is a view of the job and not a second schema.
    Q_INVOKABLE QVariantMap startRender(const QVariantMap& jobFields);
    /// Returns whether the cancel was actually *requested*. A cancel is a request —
    /// the queue decides the outcome — so a true here does not mean the render
    /// stopped, and the outcome arrives as `renderFailed` with the queue's reason.
    Q_INVOKABLE bool cancelRender();

    // ── Persistence ─────────────────────────────────────────────────────────
    /// `{ok: true}` or `{ok: false, message}` — deliberately no `kind`/`subject`,
    /// because a file-open failure has no taxonomy worth inventing one for.
    Q_INVOKABLE QVariantMap saveWorkspace(const QString& path);
    Q_INVOKABLE QVariantMap loadWorkspace(const QString& path);

    bool renderQueueAttached() const;
    bool renderActive() const;
    QString renderJobId() const;
    int renderProgressPercent() const;
    QString lastError() const;

signals:
    void regionsChanged();
    void lyricsChanged();
    void renderStateChanged();
    /// `percent` is the queue's own unit: 0..100, or -1 when it cannot be measured.
    /// Never converted to a fraction — see the file header.
    void renderProgressChanged(int percent, const QString& stage);
    void renderCompleted(const QString& outputPath);
    void renderFailed(const QString& reason);
    void lastErrorChanged();

private slots:
    void onRegionAdded(const vc::suno::ClipRegion& region);
    void onRegionRemoved(std::size_t index);
    void onRegionsCleared();
    void onLyricsChanged(const std::string& text);
    void onRenderStarted(const std::string& jobId);
    void onRenderProgress(int percent, const std::string& stage);
    void onRenderCompleted(const vc::fs::path& outputPath);
    void onRenderFailed(const std::string& error);

private:
    /// Re-announce `state().lastError` if it moved.
    ///
    /// The class has no signal for it, and adding one would mean the class could be
    /// watched for a reason it does not own. It writes `lastError` in exactly seven
    /// places — the four refusals in `startGeneration`/`startRender`, the clear at
    /// render start, the clear at completion, and `settleFailed` — and all seven are
    /// either a call this bridge makes or a signal it already handles, so this is the
    /// complete set of points at which it can change.
    void publishLastError();

    /// Not nullable: the constructor unconditionally `new`s it with `this` as parent,
    /// so the `workspace_ ? ... : fallback` guards the previous version carried could
    /// only ever have hidden a bug rather than handled a case.
    vc::suno::SunoWorkspace* workspace_;

    /// Last value announced by `lastErrorChanged`, so an unchanged reason does not
    /// re-notify. Initialised from the (empty) workspace state in the constructor.
    QString lastErrorCache_;
};

} // namespace qml_bridge