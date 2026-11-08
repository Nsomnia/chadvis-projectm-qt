// T0171 frame-0 half, headless half: the entry condition of the PBO
// readback. captureAsync's emit choice is a pure static, so the gate that
// used to skip the first frame can be pinned without a GL context.

#include <QtTest>

#include "visualizer/VisualizerRenderer.hpp"

using namespace vc;

class TestVisualizerRendererCapture : public QObject {
    Q_OBJECT

private slots:
    // The regression: after a successful setupPBOs the FIRST capture must
    // reach the emit path. The old gate (pboAvailable_ hardcoded false,
    // flipped to true by the first capture itself) returned "emit nothing"
    // for it by construction, so frame 0 needed a second render to exist,
    // and never reached the encoder at all in a one-render recording.
    void firstCaptureEmitsItsOwnFrame() {
        QCOMPARE(VisualizerRenderer::captureEmit(true, false),
                 VisualizerRenderer::CaptureEmit::InlineOwnFrame);
        // firstCapture dominates: even a somehow-pending sibling is not the
        // frame this call owns.
        QCOMPARE(VisualizerRenderer::captureEmit(true, true),
                 VisualizerRenderer::CaptureEmit::InlineOwnFrame);
    }

    void pendingFrameEmitsPipelined() {
        QCOMPARE(VisualizerRenderer::captureEmit(false, true),
                 VisualizerRenderer::CaptureEmit::EmitPendingFrame);
    }

    void primingCaptureDefersExactlyOnce() {
        // The call right after the inline first frame seeds the pipeline: it
        // issues its readback and emits nothing, then every later call emits.
        QCOMPARE(VisualizerRenderer::captureEmit(false, false),
                 VisualizerRenderer::CaptureEmit::EmitNothing);
    }
};

#include "test_VisualizerRendererCapture.moc"

int runTestVisualizerRendererCapture(int argc, char** argv) {
    TestVisualizerRendererCapture tc;
    return QTest::qExec(&tc, argc, argv);
}
