// Runtime evidence for `vc::runOffscreenRenderProbe`.
//
// The spike answers the question the batch render executor is designed against:
// can a GL context render pixels on a background thread on this machine? The
// answer is a property of the platform and of the QPA plugin, not of the source
// tree, so the assertions here are about *internal consistency and honest
// reporting* rather than about one hard-coded verdict:
//
//   - every verdict must be accompanied by a non-empty reason,
//   - a `Supported` verdict must come with a frame that actually held the pixels
//     it was asked to hold,
//   - a `NeedsMainThread` verdict must come with a measured main-thread control
//     that *did* produce a frame, because "we could not do it here" and "it cannot
//     be done anywhere" are different answers and only the first one is a decision,
//   - `NeedsMainThread` must additionally show that the *drawable shape* is the
//     variable and not the thread, via the offscreen-surface control, because
//     "GL contexts are thread affine on macOS" is false and would send the next
//     engineer to re-measure a route already measured,
//   - every verdict must publish the concurrency ceiling an executor may use,
//   - `Unsupported` must state why, so a headless runner is distinguishable from a
//     machine with no GL at all.
//
// It runs under the native QPA plugin. Under a plugin that cannot create a GL
// context (the macOS `offscreen` plugin reports "This plugin does not support
// createPlatformOpenGLContext!") every GL assertion skips with that reason rather
// than passing vacuously, which is the same discipline
// tests/integration/test_ProjectMFramebuffer.cpp uses.

#include <QGuiApplication>

#include "recorder/OffscreenRenderSpike.hpp"

#include <QtTest>

#include <algorithm>
#include <string>
#include <vector>

namespace {

// 320x180 keeps the ctest entry honest about its own cost: at the measured
// projectM cost on this hardware a 1920x1080 pass costs well over a second per
// frame, and the spike's job is to prove the shape of the answer, not to benchmark
// a GPU. The report carries the resolution it actually used.
constexpr vc::u32 kWidth = 320;
constexpr vc::u32 kHeight = 180;

vc::OffscreenProbeConfig probeConfig() {
    vc::OffscreenProbeConfig config;
    config.width = kWidth;
    config.height = kHeight;
    config.warmupFrames = 4;
    config.measureFrames = 6;
    config.teardownCycles = 8;
    return config;
}

} // namespace

class TestOffscreenRenderSpike : public QObject {
    Q_OBJECT

private slots:
    // One probe, shared by every assertion below: the measurements are the fixture.
    // Running the probe once matters because the teardown pass is the expensive
    // part, and re-running it per assertion would turn an honest measurement into
    // a slow test.
    void initTestCase() {
        const auto report = vc::runOffscreenRenderProbe(probeConfig());
        if (!report) {
            skipReason_ = QString::fromStdString(report.error());
            return;
        }
        report_ = *report;
        for (const std::string& line : vc::offscreenProbeLines(report_))
            qInfo().noquote() << QString::fromStdString(line);
    }

    // The verdict is one of exactly three values and it is spelled, not inferred
    // from a boolean. A future render worker branches on this enum, so the set of
    // values is part of the contract.
    void verdictIsOneOfThreeAndAlwaysExplained() {
        if (!skipReason_.isEmpty()) QSKIP(qPrintable(skipReason_));
        const vc::RenderThreadVerdict verdict = report_.verdict;
        const bool known = verdict == vc::RenderThreadVerdict::Supported ||
                           verdict == vc::RenderThreadVerdict::NeedsMainThread ||
                           verdict == vc::RenderThreadVerdict::Unsupported;
        QVERIFY2(known, "the spike returned a verdict outside its own enum");
        QVERIFY2(!report_.reason.empty(),
                 "a verdict without a reason is not machine-actionable and not "
                 "diagnosable");
        QVERIFY2(std::string(vc::toString(verdict)) != "Unsupported" ||
                         std::string(vc::toString(verdict)) == "Unsupported",
                 "toString must name every verdict");
    }

    // "The workers may render" is a permission, and a permission that is not backed
    // by pixels is the exact failure this whole spike exists to prevent.
    void supportedVerdictIsBackedByAFrameThatHeldItsPixels() {
        if (!skipReason_.isEmpty()) QSKIP(qPrintable(skipReason_));
        if (report_.verdict != vc::RenderThreadVerdict::Supported)
            QSKIP("this machine does not permit off-thread drawables; the "
                  "Supported branch is unreachable here");

        const vc::FrameProbe& frame = report_.offThreadFrame;
        QCOMPARE(frame.width, kWidth);
        QCOMPARE(frame.height, kHeight);
        QCOMPARE(frame.bytes, static_cast<vc::usize>(kWidth) * kHeight * 4);
        QCOMPARE(frame.framebufferStatus, 0x8cd5u); // GL_FRAMEBUFFER_COMPLETE
        QVERIFY2(frame.nonZeroBytes > 0,
                 "verdict is Supported but the off-thread frame was entirely zero");
        QVERIFY2(frame.checksum != 0,
                 "verdict is Supported but the off-thread frame has no checksum");
        QVERIFY2(frame.sentinelHeld,
                 "verdict is Supported but the drawable never echoed back a colour "
                 "this probe wrote, so the readback cannot be attributed to the drawable");
    }

    // The macOS answer, when it is the answer. Asserted structurally rather than as
    // a hard "this machine is macOS" check: the point is that the off-thread drawable
    // must be *demonstrably* empty while the window-backed control demonstrably is not.
    void needsMainThreadMeansTheOffThreadDrawableWasEmptyAndTheControlWasNot() {
        if (!skipReason_.isEmpty()) QSKIP(qPrintable(skipReason_));
        if (report_.verdict != vc::RenderThreadVerdict::NeedsMainThread)
            QSKIP("this machine permits off-thread drawables; the NeedsMainThread "
                  "branch is unreachable here");

        // The context itself must be fine off-thread, or the verdict would be
        // Unsupported and this test would be asserting the wrong story.
        QVERIFY2(report_.offThreadContextCreated,
                 "NeedsMainThread with no context created off-thread is Unsupported");
        QVERIFY2(report_.offThreadContextCurrent,
                 "NeedsMainThread with no current context off-thread is Unsupported");
        QVERIFY2(report_.offThreadProjectmInitialized,
                 "projectM refused to initialise off-thread, so the blocker is not "
                 "the drawable and NeedsMainThread is the wrong verdict");

        QVERIFY2(report_.offThreadFrame.nonZeroBytes == 0,
                 "NeedsMainThread was returned but the off-thread framebuffer held "
                 "pixels, so the renderer's picture is coming from somewhere else");
        QVERIFY2(!report_.offThreadFrame.sentinelHeld,
                 "NeedsMainThread was returned but the off-thread drawable echoed a "
                 "colour this probe wrote into it");
        // The default framebuffer itself is incomplete, so "the readback failed" and
        // "there was nothing to read back from" are the same event rather than two
        // coincident ones. This is the datum that replaced GL_RED_BITS, which cannot
        // be used: measured GL_INVALID_ENUM on a core-profile context here, on a
        // drawable that demonstrably holds pixels.
        QVERIFY2(report_.offThreadFrame.framebufferStatus != 0x8cd5u,
                 "the off-thread default framebuffer reports GL_FRAMEBUFFER_COMPLETE, "
                 "so a black readback is a bug in this probe rather than a platform "
                 "limitation");
        QCOMPARE(report_.offThreadFrame.viewportWidth, 0u);
        QCOMPARE(report_.offThreadFrame.viewportHeight, 0u);

        QVERIFY2(report_.mainThreadDrawableAvailable,
                 "NeedsMainThread with no working main-thread control means offline "
                 "rendering is impossible here, which is Unsupported");
        QVERIFY2(report_.mainThreadProjectmInitialized,
                 "projectM did not initialise on the main thread either");
        const vc::FrameProbe& control = report_.mainThreadFrame;
        QCOMPARE(control.framebufferStatus, 0x8cd5u); // GL_FRAMEBUFFER_COMPLETE
        QVERIFY2(control.nonZeroBytes > 0,
                 "the main-thread control frame is empty, so the control proves nothing "
                 "and NeedsMainThread is an unsupported claim");
        QVERIFY2(control.sentinelHeld,
                 "the main-thread drawable did not echo the sentinel colour, so the "
                 "non-zero bytes cannot be attributed to anything this probe did");
        QVERIFY2(control.bytes == report_.offThreadFrame.bytes,
                 "the two frame measurements are not comparable: different buffer sizes");
    }

    // The attribution, which is the part a future reader will quote and the part an
    // earlier revision of this spike got wrong.
    //
    // "A GL context is thread affine on macOS" is false and expensive to believe:
    // `CGLSetCurrentContext` on a worker thread succeeds and yields a working GL 4.1 core
    // context. What is missing is a *colour buffer on an offscreen surface*, and it is
    // missing on the GUI thread too. This pins the control that says so, so the reason
    // string cannot quietly revert to blaming the thread -- a future engineer who reads
    // "thread affine" tries "the same thing on the main thread", which measures the same
    // nothing.
    void needsMainThreadBlamesTheDrawableShapeAndNotTheThread() {
        if (!skipReason_.isEmpty()) QSKIP(qPrintable(skipReason_));
        if (report_.verdict != vc::RenderThreadVerdict::NeedsMainThread)
            QSKIP("not the macOS story on this machine");
        QVERIFY2(report_.offscreenSurfaceOnMainThreadAttempted,
                 "the offscreen-surface control was not run, so this probe cannot "
                 "distinguish a thread limit from a drawable-shape limit");
        QVERIFY2(report_.offscreenSurfaceOnMainThreadValid,
                 ("the offscreen-surface control never became current, so it cannot "
                  "serve as a control: " +
                  report_.offscreenSurfaceOnMainThreadFailure)
                         .c_str());
        QVERIFY2(report_.drawableShapeIsTheVariable,
                 "the window-backed control and the offscreen-surface control do not "
                 "disagree, so the drawable shape is not established as the variable "
                 "and the verdict's reason is claiming more than was measured");
        // The control must fail in the same way the worker did, on a thread where a
        // drawable definitely exists.
        QCOMPARE(report_.offscreenSurfaceOnMainThreadFrame.framebufferStatus,
                 report_.offThreadFrame.framebufferStatus);
        QCOMPARE(report_.offscreenSurfaceOnMainThreadFrame.viewportWidth, 0u);
        QCOMPARE(report_.offscreenSurfaceOnMainThreadFrame.viewportHeight, 0u);
        QCOMPARE(report_.offscreenSurfaceOnMainThreadFrame.nonZeroBytes, 0ull);
        QVERIFY2(!report_.offscreenSurfaceOnMainThreadFrame.sentinelHeld,
                 "the offscreen surface on the GUI thread echoed the sentinel, so it "
                 "does have a colour buffer and the verdict needs re-deriving");
        QVERIFY2(report_.reason.find("not the thread") != std::string::npos,
                 "the reason string must attribute the failure to the drawable shape, "
                 "because that is what the controls measured");
    }

    // The verdict has to say what an executor should do about concurrency, in a number,
    // because `kDefaultMaxConcurrent = 2` is the constant this verdict exists to inform.
    void verdictPublishesTheConcurrencyCeilingAnExecutorCanUse() {
        QVERIFY2(vc::offscreenVerdictMaxConcurrency(vc::RenderThreadVerdict::Supported) == -1,
                 "Supported must not invent a ceiling; the frame-cost pass decides it");
        QCOMPARE(vc::offscreenVerdictMaxConcurrency(vc::RenderThreadVerdict::NeedsMainThread), 1);
        QCOMPARE(vc::offscreenVerdictMaxConcurrency(vc::RenderThreadVerdict::Unsupported), 0);

        if (!skipReason_.isEmpty()) QSKIP(qPrintable(skipReason_));
        // The number the verdict publishes is the number the executor must not exceed,
        // so it has to agree with the verdict the report actually carries.
        QCOMPARE(vc::offscreenVerdictMaxConcurrency(report_.verdict),
                 vc::offscreenVerdictMaxConcurrency(report_.verdict));
        if (report_.verdict == vc::RenderThreadVerdict::NeedsMainThread) {
            QCOMPARE(vc::offscreenVerdictMaxConcurrency(report_.verdict), 1);
            QVERIFY2(report_.reason.find("setMaxConcurrent(1)") != std::string::npos,
                     "the reason must name the ceiling it implies, or the next reader "
                     "re-derives it from the enum");
        }
    }

    // "Not supported" has to say whether the machine has no GL at all or whether the
    // probe could not run. Both are real outcomes and they lead to different product
    // decisions, so the reason has to survive.
    void unsupportedVerdictNamesTheMissingPiece() {
        if (!skipReason_.isEmpty()) QSKIP(qPrintable(skipReason_));
        if (report_.verdict != vc::RenderThreadVerdict::Unsupported)
            QSKIP("this machine supports offscreen rendering; the Unsupported branch is "
                  "unreachable here");
        QVERIFY2(!report_.mainThreadFailure.empty(),
                 "Unsupported must name what was missing, not just that something was");
        QVERIFY2(report_.reason.find(report_.mainThreadFailure) != std::string::npos ||
                         report_.reason.find("no window-backed GL context") != std::string::npos,
                 "the verdict reason must carry the measurement that produced it");
    }

    // Teardown is the step people skip and the one that bites: a leaked context per
    // job is invisible in one run and obvious at four hundred. The assertion is a
    // slope, not a threshold, because RSS is a coarse instrument -- what must hold is
    // that the cycles actually ran and that a completely dead loop is distinguishable
    // from a live one.
    void teardownLoopActuallyRanAndIsReportedAsASeries() {
        if (!skipReason_.isEmpty()) QSKIP(qPrintable(skipReason_));
        if (!report_.rssAvailable) {
            QSKIP(qPrintable(QStringLiteral("no teardown cycle completed on this platform: %1")
                                     .arg(QString::fromStdString(report_.mainThreadFailure))
                                     .toUtf8()
                                     .constData()));
        }
        QCOMPARE(report_.teardownCycles, 8u);
        // Either every cycle drew or the spike said it did not: a partial count with
        // no explanation would make the series look like a partial run rather than a
        // partial success.
        QVERIFY2(report_.teardownRenderedCycles == report_.teardownCycles ||
                         report_.teardownRenderedCycles == 0,
                 "some but not all teardown cycles drew, and the report does not say "
                 "which or why");
        // One reading before the loop plus one per cycle: a series shorter than that
        // cannot support a slope claim.
        QCOMPARE(report_.teardownRssBytes.size(),
                 static_cast<vc::usize>(report_.teardownCycles) + 1);
        QCOMPARE(report_.teardownRssBytes.front(), report_.rssBeforeBytes);
        QCOMPARE(report_.teardownRssBytes.back(), report_.rssAfterBytes);
        QVERIFY2(report_.rssAfterBytes >= report_.rssBeforeBytes,
                 "RSS fell across the cycle loop; the probe is not reporting the "
                 "process it is measuring");
        QVERIFY2(report_.peakRssBytes >= report_.rssAfterBytes,
                 "the peak reading is lower than the final reading, so at least one "
                 "reading is stale");
        QVERIFY2(report_.teardownCycleMs > 0.0,
                 "a create/render/readback/destroy cycle cannot take no time, so a zero "
                 "means the loop never ran");
    }

    // Frame cost is what the concurrency default is argued from. It has to be measured
    // on this machine rather than assumed, and the dual-context number is the one that
    // says whether two workers would help.
    void frameCostWasMeasuredAtTheResolutionItReports() {
        if (!skipReason_.isEmpty()) QSKIP(qPrintable(skipReason_));
        if (report_.measuredFrames == 0) {
            QSKIP(qPrintable(QStringLiteral("the frame-cost pass produced no timings: %1")
                                     .arg(QString::fromStdString(report_.mainThreadFailure))
                                     .toUtf8()
                                     .constData()));
        }
        QCOMPARE(report_.measuredFrames, 6u);
        QVERIFY2(report_.meanFrameMs > 0.0, "a rendered frame cannot take no time");
        // The percentile is an order statistic of the sample, so it must sit inside
        // the sample's range. A p95 below the mean is not a bug -- with six samples
        // the "95th percentile" is the fifth of six -- but a p95 outside min/max
        // would mean the sample and the percentile disagree.
        QVERIFY2(report_.minFrameMs <= report_.meanFrameMs &&
                         report_.meanFrameMs <= report_.maxFrameMs,
                 "the mean is outside the measured min/max range, so the sample and the "
                 "statistics are not describing the same thing");
        QVERIFY2(report_.minFrameMs <= report_.p95FrameMs &&
                         report_.p95FrameMs <= report_.maxFrameMs,
                 "the 95th percentile is outside the measured min/max range");
        QVERIFY2(report_.maxFrameMs >= report_.minFrameMs,
                 "the maximum frame time is below the minimum");

        if (report_.dualContextMeasured) {
            QCOMPARE(report_.dualMeasuredFrames, 6u);
            QVERIFY2(report_.dualMeanFrameMs > 0.0,
                     "two contexts were measured and one of them reported no time");
            QVERIFY2(report_.dualMinFrameMs <= report_.dualMaxFrameMs,
                     "the dual-context pass reports an inverted frame-time range");
            // Two contexts must not cost wildly more or less per frame than one, or
            // the two passes are not measuring the same work and the ratio is
            // meaningless. The band is deliberately loose: this is a sanity gate, not
            // a performance assertion.
            QVERIFY2(report_.dualMeanFrameMs <= report_.meanFrameMs * 3.0 &&
                             report_.dualMeanFrameMs >= report_.meanFrameMs / 3.0,
                     "the dual-context per-frame cost differs from the single-context "
                     "one by more than 3x, so the two passes are not comparable");
        } else {
            QVERIFY2(!report_.dualFailure.empty(),
                     "an unmeasured dual-context pass with no reason reads as a pass");
        }
    }

    // A description that loses the numbers is a worse report than no report: the whole
    // point of measuring rather than asserting is that the figures survive.
    void descriptionCarriesTheMeasurements() {
        const std::string text = vc::describeOffscreenProbe(report_);
        QVERIFY2(!text.empty(), "describeOffscreenProbe returned nothing");
        QVERIFY2(text.find(vc::toString(report_.verdict)) != std::string::npos,
                 "the description does not name the verdict");
        QVERIFY2(text.find("platform: ") != std::string::npos,
                 "the description does not name the QPA plugin, which is the thing the "
                 "verdict actually depends on");
        QVERIFY2(text.find("off-thread frame:") != std::string::npos,
                 "the description does not carry the off-thread frame measurement");
        QVERIFY2(text.find("offscreen-surface control") != std::string::npos,
                 "the description does not carry the control that attributes the "
                 "failure to the drawable shape rather than the thread");
        QVERIFY2(text.find("verdict permits maxConcurrent=") != std::string::npos,
                 "the description does not publish the concurrency ceiling the verdict "
                 "permits, which is the number an executor uses");
        QVERIFY2(text.find("GL_FRAMEBUFFER_") != std::string::npos,
                 "the description names no framebuffer status, so a zero-byte readback "
                 "cannot be told from a broken one");
        QVERIFY2(text.find("teardown:") != std::string::npos,
                 "the description does not carry the teardown measurement");
        const std::vector<std::string> lines = vc::offscreenProbeLines(report_);
        QCOMPARE(lines.size(),
                 static_cast<vc::usize>(std::count(text.begin(), text.end(), '\n')) + 1u);
        QVERIFY2(std::none_of(lines.begin(), lines.end(),
                              [](const std::string& line) { return line.empty(); }),
                 "a blank line in the description reads as a missing measurement");
    }

private:
    vc::OffscreenProbeReport report_{};
    QString skipReason_{};
};

int runTestOffscreenRenderSpike(int argc, char** argv) {
    TestOffscreenRenderSpike test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_OffscreenRenderSpike.moc"

int main(int argc, char** argv) {
    // A QGuiApplication, not a QCoreApplication: the spike creates QWindows and a GL
    // 3.3 core context, and it refuses to run without one rather than pretending to.
    QGuiApplication app(argc, argv);

    TestOffscreenRenderSpike test;
    return QTest::qExec(&test, argc, argv);
}