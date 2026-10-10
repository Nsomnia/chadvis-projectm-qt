#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "audio/AudioEngine.hpp"
#include "audio/AudioQueue.hpp"
#include "audio/Playlist.hpp"

using namespace vc;

namespace {

/// QVERIFY2's failure message must be a const char*, and QTest::toString has no
/// overload for std::optional or std::string (its fallback needs a QDebug stream
/// operator). These two helpers give the assertions something printable without
/// a .c_str() at every call site.
const char* why(std::string text) {
    static thread_local std::string buffer;
    buffer = std::move(text);
    return buffer.c_str();
}

std::string show(const std::optional<usize>& value) {
    return value ? std::to_string(*value) : std::string("<none>");
}

/// A half-second 16-bit mono WAV. The playback tests need a file the platform
/// decoder can actually open: a bogus URL would fail to load, so "is it
/// playing?" could not distinguish the bug from a missing audio backend.
fs::path writeToneWav(const fs::path& path, u32 sampleRate = 44100, f32 seconds = 0.5f) {
    const u32 samples = static_cast<u32>(sampleRate * seconds);
    QByteArray pcm;
    pcm.resize(static_cast<int>(samples) * 2);
    auto* out = reinterpret_cast<qint16*>(pcm.data());
    for (u32 i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        out[i] = static_cast<qint16>(12000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * t));
    }

    QByteArray wav;
    const auto append32 = [&wav](quint32 value) {
        wav.append(static_cast<char>(value & 0xFF));
        wav.append(static_cast<char>((value >> 8) & 0xFF));
        wav.append(static_cast<char>((value >> 16) & 0xFF));
        wav.append(static_cast<char>((value >> 24) & 0xFF));
    };
    const auto append16 = [&wav](quint16 value) {
        wav.append(static_cast<char>(value & 0xFF));
        wav.append(static_cast<char>((value >> 8) & 0xFF));
    };

    wav.append("RIFF");
    append32(36 + static_cast<quint32>(pcm.size()));
    wav.append("WAVE");
    wav.append("fmt ");
    append32(16);
    append16(1); // PCM
    append16(1); // mono
    append32(sampleRate);
    append32(sampleRate * 2);
    append16(2);
    append16(16);
    wav.append("data");
    append32(static_cast<quint32>(pcm.size()));
    wav.append(pcm);

    QFile file(QString::fromStdString(path.string()));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(wav);
        file.close();
    }
    return path;
}

std::string show(const std::vector<usize>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        out += (i ? ", " : "") + std::to_string(values[i]);
    }
    return out + "]";
}

/// Records every currentChanged emission *and* what a re-entrant reader saw at
/// emit time. vc::Signal is not a Qt signal, so QSignalSpy cannot be used.
struct CurrentTrace {
    std::vector<std::optional<usize>> emissions;
    /// Playlist::currentIndex() as read from inside the slot.
    std::vector<std::optional<usize>> indexDuringEmit;
    /// Playlist::currentItem() title as read from inside the slot, "<none>"
    /// when there is no current item.
    std::vector<std::string> currentTitleDuringEmit;
    /// Playlist::itemAt(*emitted) title as read from inside the slot.
    std::vector<std::string> itemAtTitleDuringEmit;
    /// Playlist::size() as read from inside the slot.
    std::vector<usize> sizeDuringEmit;

    void attach(Playlist& playlist) {
        playlist.currentChanged.connect([this, &playlist](std::optional<usize> index) {
            emissions.push_back(index);
            indexDuringEmit.push_back(playlist.currentIndex());
            sizeDuringEmit.push_back(playlist.size());

            const auto current = playlist.currentItem();
            currentTitleDuringEmit.push_back(current ? current->title() : std::string("<none>"));

            std::string atIndex = "<none>";
            if (index) {
                if (const auto item = playlist.itemAt(*index)) {
                    atIndex = item->title();
                }
            }
            itemAtTitleDuringEmit.push_back(atIndex);
        });
    }

    [[nodiscard]] std::size_t count() const { return emissions.size(); }
};

/// Frames actually stored in one of AudioQueue's two consumer queues. Counted
/// by SUMMING AudioFrame::sampleCount, not by counting dequeues: one AudioFrame
/// carries up to AUDIO_FRAME_SAMPLES (8) frames, and the identity under test is
/// about frames the producer offered.
u64 drainFrames(AudioQueue& queue, bool viz) {
    u64 frames = 0;
    AudioFrame frame;
    for (;;) {
        const bool popped = viz ? queue.popViz(frame) : queue.popRec(frame);
        if (!popped) break;
        frames += frame.sampleCount;
    }
    return frames;
}

/// Stereo interleaved PCM of `frames` frames, with every sample distinct so a
/// mis-indexed read or a stray zero would be visible rather than plausible.
std::vector<float> makePcm(u32 frames) {
    std::vector<float> pcm(static_cast<usize>(frames) * 2);
    for (u32 i = 0; i < frames; ++i) {
        pcm[i * 2] = static_cast<float>(i);
        pcm[i * 2 + 1] = static_cast<float>(i) + 0.5f;
    }
    return pcm;
}

} // namespace

class TestPlaylist : public QObject {
    Q_OBJECT

private slots:
    void currentChangedAnnouncesEverySelectionChange() {
        Playlist playlist;
        CurrentTrace trace;
        trace.attach(playlist);

        playlist.addUrl("a", "A");
        playlist.addUrl("b", "B");
        playlist.addUrl("c", "C");
        // Appending cannot change the selection.
        QCOMPARE(trace.count(), std::size_t{0});
        QVERIFY(!playlist.currentIndex());

        // jumpTo always announces.
        QVERIFY(playlist.jumpTo(1));
        QCOMPARE(trace.count(), std::size_t{1});
        QVERIFY2(trace.emissions.back() == std::optional<usize>{1},
                 why(show(trace.emissions.back())));

        // Removing an item *below* the current one renumbers the selection but
        // leaves the selected track alone, so it must stay silent: announcing it
        // would make the engine reload the very same track and re-emit
        // trackChanged for a deletion the listener did not ask about.
        playlist.removeAt(0);
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0},
                 why(show(playlist.currentIndex())));
        QCOMPARE(playlist.size(), usize{2});
        QCOMPARE(trace.count(), std::size_t{1});

        // Removing an item above the current one changes nothing either.
        playlist.removeAt(1);
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0},
                 why(show(playlist.currentIndex())));
        QCOMPARE(trace.count(), std::size_t{1});

        // Removing the selected item is the transition the old Signal<usize>
        // could not express: the selection becomes "none", and it must be
        // announced as such.
        playlist.removeAt(0);
        QCOMPARE(playlist.size(), usize{0});
        QVERIFY2(!playlist.currentIndex(), why(show(playlist.currentIndex())));
        QCOMPARE(trace.count(), std::size_t{2});
        QVERIFY2(!trace.emissions.back().has_value(), why(show(trace.emissions.back())));

        // clear() on an already-empty selection changed nothing, so it is
        // silent: announcing a transition that did not happen is its own lie.
        playlist.clear();
        QCOMPARE(trace.count(), std::size_t{2});

        // clear() with a live selection announces the loss.
        playlist.addUrl("d", "D");
        playlist.addUrl("e", "E");
        QVERIFY(playlist.jumpTo(1));
        QCOMPARE(trace.count(), std::size_t{3});
        playlist.clear();
        QVERIFY2(!playlist.currentIndex(), why(show(playlist.currentIndex())));
        QCOMPARE(trace.count(), std::size_t{4});
        QVERIFY2(!trace.emissions.back().has_value(), why(show(trace.emissions.back())));

        // move() renumbers the selection so the *moved* item stays selected, so
        // it is never a selection change and must not announce.
        playlist.addUrl("f", "F");
        playlist.addUrl("g", "G");
        QVERIFY(playlist.jumpTo(0));
        const std::size_t beforeMove = trace.count();
        playlist.move(0, 1);
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{1},
                 why(show(playlist.currentIndex())));
        QVERIFY2(playlist.currentItem().has_value(), why("the moved item lost its selection"));
        QVERIFY2(playlist.currentItem()->title() == "F", why(playlist.currentItem()->title()));
        QCOMPARE(trace.count(), beforeMove);
    }

    void snapshotOutlivesLaterMutation() {
        Playlist playlist;
        playlist.addUrl("a", "A");
        playlist.addUrl("b", "B");
        playlist.addUrl("c", "C");
        QVERIFY(playlist.jumpTo(1));

        // The read API hands out values, so this view is the caller's own copy
        // and cannot be invalidated by anything the live Playlist does next.
        // That is the regression guard for the deleted `items()` reference.
        const Playlist::Snapshot pinned = playlist.snapshot();
        QCOMPARE(pinned.size(), usize{3});
        QVERIFY(pinned.currentIndex == std::optional<usize>{1});
        QVERIFY2(pinned.items[0].url == "a", why(pinned.items[0].url));

        // A reference and a pointer taken out of the snapshot stay usable too,
        // which is the whole reason callers could bind `const auto&` before.
        const PlaylistItem& pinnedItem = pinned.items[0];
        const PlaylistItem* pinnedPointer = &pinned.items[2];

        playlist.removeAt(0);
        playlist.clear();

        // Live state moved on...
        QCOMPARE(playlist.size(), usize{0});
        QVERIFY(!playlist.currentIndex());
        // ...and the snapshot is still whole, still correct, still readable.
        QCOMPARE(pinned.size(), usize{3});
        QVERIFY(pinned.currentIndex == std::optional<usize>{1});
        QVERIFY2(pinned.items[0].url == "a", why(pinned.items[0].url));
        QVERIFY2(pinned.items[1].url == "b", why(pinned.items[1].url));
        QVERIFY2(pinned.items[2].url == "c", why(pinned.items[2].url));
        QVERIFY2(pinnedItem.title() == "A", why(pinnedItem.title()));
        QVERIFY2(pinnedPointer->url == "c", why(pinnedPointer->url));
    }

    void reentrantReaderSeesSettledState() {
        Playlist playlist;
        CurrentTrace trace;
        trace.attach(playlist);

        playlist.addUrl("a", "A");
        playlist.addUrl("b", "B");
        playlist.addUrl("c", "C");

        // Nothing is left dangling after an emit: the live state read back
        // straight after an announcing call must be exactly the state the slot
        // observed during it.
        const auto announceThenSettle = [&](const char* label, auto&& operation) {
            const std::size_t before = trace.count();
            operation();
            if (trace.count() == before) {
                return; // announced nothing, so there is nothing to settle
            }
            QVERIFY2(playlist.size() == trace.sizeDuringEmit.back(),
                     why(std::string(label) + ": size after the call is " +
                         std::to_string(playlist.size()) + " but the slot saw " +
                         std::to_string(trace.sizeDuringEmit.back())));
            QVERIFY2(playlist.currentIndex() == trace.indexDuringEmit.back(),
                     why(std::string(label) + ": index after the call is " +
                         show(playlist.currentIndex()) + " but the slot saw " +
                         show(trace.indexDuringEmit.back())));
        };

        announceThenSettle("jumpTo(1)", [&] { QVERIFY(playlist.jumpTo(1)); });
        announceThenSettle("next()", [&] { QVERIFY(playlist.next()); });

        // move() keeps the same item selected, so it must not announce at all.
        const std::size_t beforeMove = trace.count();
        playlist.move(0, 2);
        QCOMPARE(trace.count(), beforeMove);

        // Renumbering removal: no announcement.
        announceThenSettle("removeAt(0) below the selection", [&] { playlist.removeAt(0); });

        // Deleting the selected item: announced, and the slot must already have
        // seen the post-removal list and the lost selection.
        announceThenSettle("removeAt(0) of the selected item", [&] { playlist.removeAt(0); });

        // clear() on an already-empty selection is silent by contract.
        const std::size_t beforeClear = trace.count();
        playlist.clear();
        QCOMPARE(trace.count(), beforeClear);

        // Every emit site mutates fully before emitting, so a slot that reads
        // back sees the settled post-transition state and nothing else. Locked
        // in here so a future edit that reorders a mutation after its emit fails
        // loudly instead of quietly re-entering half-mutated state.
        QVERIFY2(!trace.emissions.empty(), why("no currentChanged emission was recorded"));
        for (std::size_t i = 0; i < trace.count(); ++i) {
            QVERIFY2(trace.indexDuringEmit[i] == trace.emissions[i],
                     why("emission " + std::to_string(i) + ": slot saw index " +
                         show(trace.indexDuringEmit[i]) + " but was handed " +
                         show(trace.emissions[i])));
            // itemAt(emitted) and currentItem() must agree mid-emit, which is
            // only true once the mutation is complete.
            QVERIFY2(trace.itemAtTitleDuringEmit[i] == trace.currentTitleDuringEmit[i],
                     why("emission " + std::to_string(i) + ": itemAt said " +
                         trace.itemAtTitleDuringEmit[i] + " but currentItem said " +
                         trace.currentTitleDuringEmit[i]));
        }

        // The last emission was the removal of the selected item, and it must
        // already have seen the shrunk list (a, b, c -> [c, a] -> [a]) and the
        // gone selection.
        const std::size_t last = trace.count() - 1;
        QVERIFY(!trace.emissions[last].has_value());
        QCOMPARE(trace.sizeDuringEmit[last], usize{1});
        QVERIFY2(trace.currentTitleDuringEmit[last] == "<none>",
                 why(trace.currentTitleDuringEmit[last]));

        // The trailing clear() emptied the list after that emission, silently,
        // because the selection was already gone by then.
        QCOMPARE(playlist.size(), usize{0});
        QVERIFY(!playlist.currentIndex());
    }

    void threadContractIsEnforced() {
        Playlist playlist;

        // Constructed on this thread, so the owner token must accept it and
        // every public method below must run without tripping the guard. A
        // tripped guard is an abort, so reaching the end of this function is
        // itself the "normal path is clean" assertion.
        QVERIFY(playlist.onOwnerThread());
        playlist.addUrl("a", "A");
        QVERIFY(playlist.jumpTo(0));
        QVERIFY(playlist.shuffle() == false);
        QVERIFY(playlist.repeatMode() == RepeatMode::Off);
        QCOMPARE(playlist.size(), usize{1});
        QVERIFY(!playlist.empty());
        QVERIFY(playlist.currentIndex().has_value());
        QVERIFY(playlist.currentItem().has_value());
        QVERIFY(playlist.itemAt(0).has_value());
        QCOMPARE(playlist.snapshot().size(), usize{1});
        playlist.setShuffle(true);
        QVERIFY(playlist.shuffle());
        playlist.cycleRepeatMode();
        QVERIFY(playlist.repeatMode() == RepeatMode::All);
        playlist.cycleRepeatMode();
        QVERIFY(playlist.repeatMode() == RepeatMode::One);
        // Navigation below needs a mode that actually has somewhere to go from
        // a one-item queue.
        playlist.setRepeatMode(RepeatMode::All);
        QVERIFY(playlist.next());
        QVERIFY(playlist.previous());
        playlist.move(0, 0);
        playlist.removeAt(0);

        // Another thread must not be recognised as the owner. The guard itself
        // aborts rather than returning, so what is asserted here is the exact
        // predicate it tests.
        bool probed = false;
        bool ownerOnOtherThread = true;
        std::thread probe([&] {
            ownerOnOtherThread = playlist.onOwnerThread();
            probed = true;
        });
        probe.join();
        QVERIFY2(probed, why("the off-thread probe never ran"));
        QVERIFY2(!ownerOnOtherThread, why("a foreign thread was accepted as the owner"));

        // And the token is captured, not queried: this thread is still the owner
        // after a foreign thread touched the object.
        QVERIFY2(playlist.onOwnerThread(), why("the owner token was not captured in the ctor"));
    }

    void nextExhaustsPredictablyUnderRepeatAndShuffle() {
        Playlist playlist;

        // Nothing to walk through.
        QVERIFY(!playlist.next());
        QVERIFY(!playlist.previous());
        QVERIFY(!playlist.jumpTo(0));

        playlist.addUrl("a", "A");
        playlist.addUrl("b", "B");
        playlist.addUrl("c", "C");
        QVERIFY(playlist.repeatMode() == RepeatMode::Off);

        // Repeat::Off walks the list once, then reports exhaustion and clears the
        // selection. The clearing is the half of this transition a usize payload
        // could never report, so it is asserted explicitly.
        QVERIFY(playlist.next());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0},
                 why(show(playlist.currentIndex())));
        QVERIFY(playlist.next());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{1},
                 why(show(playlist.currentIndex())));
        QVERIFY(playlist.next());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{2},
                 why(show(playlist.currentIndex())));
        QVERIFY2(!playlist.next(), why("next() advanced past the end of the list"));
        QVERIFY2(!playlist.currentIndex(), why(show(playlist.currentIndex())));
        // previous() from a cleared selection is exhausted too.
        QVERIFY(!playlist.previous());

        // Repeat::All wraps instead of stopping.
        playlist.setRepeatMode(RepeatMode::All);
        QVERIFY(playlist.repeatMode() == RepeatMode::All);
        QVERIFY(playlist.next());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0},
                 why(show(playlist.currentIndex())));
        QVERIFY(playlist.next());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{1},
                 why(show(playlist.currentIndex())));
        QVERIFY(playlist.next());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{2},
                 why(show(playlist.currentIndex())));
        QVERIFY(playlist.next());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0},
                 why("Repeat::All did not wrap: " + show(playlist.currentIndex())));
        // ...and previous() wraps backwards off the front.
        QVERIFY(playlist.previous());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{2},
                 why(show(playlist.currentIndex())));

        // Repeat::One re-emits the *same* index: a replay request, not a
        // transition, which is why it is the one documented exception to "the
        // payload always names a different item".
        playlist.setRepeatMode(RepeatMode::One);
        QVERIFY(playlist.jumpTo(1));
        CurrentTrace trace;
        trace.attach(playlist);
        QVERIFY(playlist.next());
        QCOMPARE(trace.count(), std::size_t{1});
        QVERIFY2(trace.emissions.back() == std::optional<usize>{1},
                 why("Repeat::One did not re-announce the current index: " +
                     show(trace.emissions.back())));
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{1},
                 why(show(playlist.currentIndex())));
        // Only next() is pinned by Repeat::One; previous() still steps back.
        QVERIFY(playlist.previous());
        QVERIFY2(playlist.currentIndex() == std::optional<usize>{0},
                 why(show(playlist.currentIndex())));

        // Shuffle walks a permutation, and a pass that has not started begins
        // at its first entry: next() used to pre-increment shufflePosition_ from
        // 0 even with no selection, so a fresh three-item queue visited two of
        // its tracks under Repeat::Off. Starting the traversal is now
        // startPlayback()'s job, and next() only steps when something is
        // already playing.
        Playlist shuffled;
        shuffled.addUrl("a", "A");
        shuffled.addUrl("b", "B");
        shuffled.addUrl("c", "C");
        shuffled.setShuffle(true);
        QVERIFY(shuffled.shuffle());
        QVERIFY(!shuffled.currentIndex());

        std::vector<usize> visited;
        while (shuffled.next()) {
            QVERIFY2(shuffled.currentIndex().has_value(),
                     why("shuffled next() reported success without a selection"));
            visited.push_back(*shuffled.currentIndex());
        }
        QCOMPARE(visited.size(), usize{3});
        {
            std::set<usize> once(visited.begin(), visited.end());
            QCOMPARE(once.size(), usize{3});
        }

        // Under Repeat::All the shuffle walk wraps and reshuffles, which does
        // reach shuffle position 0, so all three tracks are eventually visited.
        shuffled.setRepeatMode(RepeatMode::All);
        std::set<usize> distinct(visited.begin(), visited.end());
        for (int i = 0; i < 8 && distinct.size() < 3; ++i) {
            QVERIFY2(shuffled.next(), why("Repeat::All shuffle failed to wrap"));
            QVERIFY(shuffled.currentIndex().has_value());
            distinct.insert(*shuffled.currentIndex());
        }
        QCOMPARE(distinct.size(), std::size_t{3});
    }

    // A user skip must not resume a paused transport. The handler is bound to
    // currentChanged, which fires for every index change, and it used to call
    // play() unconditionally, so pressing "next" while paused started playing.
    void skippingWhilePausedStaysPaused() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const fs::path sessionPath = directory.path().toStdString() + "/session.m3u";
        const fs::path first = writeToneWav(directory.path().toStdString() + "/a.wav");
        const fs::path second = writeToneWav(directory.path().toStdString() + "/b.wav");
        QVERIFY(fs::exists(first) && fs::exists(second));

        AudioEngine engine(sessionPath);
        QVERIFY(engine.init());
        engine.playlist().addFile(first);
        engine.playlist().addFile(second);
        QCOMPARE(engine.playlist().size(), usize{2});

        // Positive control: without a decoder that can open the file, "paused"
        // and "playing" are the same observation and the test proves nothing.
        engine.play();
        if (!QTest::qWaitFor([&engine] { return engine.isPlaying(); }, 3000)) {
            QSKIP("no usable audio backend in this environment; playback state is unobservable");
        }
        QCOMPARE(engine.playlist().currentIndex(), std::optional<usize>{0});

        engine.pause();
        QVERIFY(QTest::qWaitFor([&engine] { return engine.state() == PlaybackState::Paused; },
                                3000));

        QVERIFY(engine.playlist().next());
        QCOMPARE(engine.playlist().currentIndex(), std::optional<usize>{1});
        QTest::qWait(150);
        QVERIFY2(engine.state() != PlaybackState::Playing,
                 why("skipping a track while paused started playback"));
    }

    // The other half of the same decision: a skip while already playing must
    // keep playing. If the fix had simply dropped the unconditional play(), a
    // user skipping mid-song would stop the transport, and this is what would
    // notice. Note the observation is deliberately weak - a backend can report
    // Playing without decoding - so it guards "a skip must not stop playback",
    // nothing about audio actually coming out.
    void skippingWhilePlayingKeepsPlaying() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const fs::path sessionPath = directory.path().toStdString() + "/session.m3u";
        const fs::path first = writeToneWav(directory.path().toStdString() + "/a.wav");
        const fs::path second = writeToneWav(directory.path().toStdString() + "/b.wav");
        QVERIFY(fs::exists(first) && fs::exists(second));

        AudioEngine engine(sessionPath);
        QVERIFY(engine.init());
        engine.playlist().addFile(first);
        engine.playlist().addFile(second);
        QCOMPARE(engine.playlist().size(), usize{2});

        engine.play();
        if (!QTest::qWaitFor([&engine] { return engine.isPlaying(); }, 3000))
            QSKIP("no usable audio backend in this environment; playback state is unobservable");

        QVERIFY(engine.playlist().next());
        QCOMPARE(engine.playlist().currentIndex(), std::optional<usize>{1});
        QVERIFY(QTest::qWaitFor([&engine] { return engine.isPlaying(); }, 3000));
    }

    // The automatic advance is the one selection change allowed to start
    // playback by itself, and it is the reason that permission is passed
    // explicitly instead of guessed from the transport state: Qt makes no
    // promise about whether the player has already reported Stopped by the
    // time EndOfMedia arrives. A real short file plays to its end here, so the
    // transition is the genuine article rather than a simulated one.
    void reachingTheEndOfATrackKeepsPlaying() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const fs::path sessionPath = directory.path().toStdString() + "/session.m3u";
        const fs::path first = writeToneWav(directory.path().toStdString() + "/a.wav", 44100, 0.4f);
        const fs::path second =
                writeToneWav(directory.path().toStdString() + "/b.wav", 44100, 0.4f);
        QVERIFY(fs::exists(first) && fs::exists(second));

        AudioEngine engine(sessionPath);
        QVERIFY(engine.init());
        engine.playlist().addFile(first);
        engine.playlist().addFile(second);
        QCOMPARE(engine.playlist().size(), usize{2});
        QVERIFY(engine.playlist().jumpTo(0));

        engine.play();
        if (!QTest::qWaitFor([&engine] { return engine.isPlaying(); }, 3000))
            QSKIP("no usable audio backend in this environment; playback state is unobservable");

        // A backend that reports Playing without decoding would let the track
        // run forever: position stays 0, EndOfMedia never arrives, and the wait
        // below would be asserting a timeout rather than the behaviour. Skipping
        // here is honest; pretending to have verified the auto-advance is not.
        if (!QTest::qWaitFor([&engine] { return engine.position().count() > 0; }, 3000)) {
            QSKIP("audio backend does not advance the playhead in this environment, "
                  "so end-of-track cannot be produced");
        }

        QVERIFY2(QTest::qWaitFor(
                         [&engine] {
                             return engine.playlist().currentIndex() == std::optional<usize>{1} &&
                                    engine.isPlaying();
                         },
                         10000),
                 why("the track ended and the queue did not advance into playback"));
    }

    // Enabling shuffle must actually shuffle where playback starts, and a
    // fresh pass must cover the whole queue. Both used to be false: play()
    // pinned the first selection to index 0, which placed the traversal in the
    // middle of its own permutation, and the entry before it was never played.
    void shuffledPlaybackCoversEveryTrack() {
        for (int attempt = 0; attempt < 25; ++attempt) {
            Playlist playlist;
            playlist.addUrl("a", "A");
            playlist.addUrl("b", "B");
            playlist.addUrl("c", "C");
            playlist.setShuffle(true);
            QVERIFY(playlist.shuffle());

            QVERIFY2(playlist.startPlayback(),
                     why("startPlayback refused an empty selection on a non-empty queue"));
            std::vector<usize> visited{*playlist.currentIndex()};
            while (playlist.next()) {
                QVERIFY(playlist.currentIndex().has_value());
                visited.push_back(*playlist.currentIndex());
            }

            QCOMPARE(visited.size(), usize{3});
            std::set<usize> once(visited.begin(), visited.end());
            QVERIFY2(once.size() == 3, why("a shuffled pass skipped a track: " + show(visited)));
        }
    }

    // Adding a track to a shuffled queue mid-pass must not move the entry the
    // traversal is standing on. Splicing the new track in at a random position
    // used to desynchronise shufflePosition_ from the selection, so the pass
    // could play one track twice and skip another.
    void addingToAShuffledQueueKeepsTheTraversalConsistent() {
        Playlist playlist;
        playlist.addUrl("a", "A");
        playlist.addUrl("b", "B");
        playlist.setShuffle(true);
        QVERIFY(playlist.startPlayback());
        const usize playing = *playlist.currentIndex();
        QVERIFY(playlist.next());

        playlist.addUrl("c", "C");
        QCOMPARE(playlist.size(), usize{3});

        // previous() steps back to whatever the traversal was on before the
        // insertion, which is the selection that is still playing.
        QVERIFY(playlist.previous());
        QVERIFY2(*playlist.currentIndex() == playing,
                 why("inserting a track moved the traversal off the playing entry"));
    }

    void sessionPlaylistIsFlushedOnDestruction() {
        // The session M3U is written by a 500 ms debounce timer rather than on
        // every mutation, so the flush-on-destroy path is what stops a normal
        // quit from losing the playlist. Deterministic: the engine is destroyed
        // with the timer still armed, so the file can only exist if the
        // destructor flushed it. No waiting, no clock, no sleep.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto sessionPath = fs::path(directory.path().toStdString()) / "last_session.m3u";

        {
            // Two real files, so this is an actual round trip rather than a check
            // that the writer spelled a URL correctly.
            //
            // It used `addUrl` with two https:// lines and asserted they came
            // back as remote items -- which pinned the SSRF primitive as expected
            // behaviour. loadM3U reads local paths only now, so a line beginning
            // "http" is not classified as remote, it simply is not a path and the
            // existence check refuses it. Remote tracks reach the queue through
            // DownloadQueue, which validates the host against the captured
            // allowlist. The refusal itself is covered by
            // tests/unit/util/test_PathSafety.cpp; this test is about the flush.
            const fs::path firstTrack = fs::path(directory.path().toStdString()) / "first.mp3";
            const fs::path secondTrack = fs::path(directory.path().toStdString()) / "second.mp3";
            for (const auto& track : {firstTrack, secondTrack}) {
                QFile f(QString::fromStdString(track.string()));
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write("not audio");
            }

            AudioEngine engine(sessionPath);
            QVERIFY(engine.init());
            engine.playlist().addFile(firstTrack);
            engine.playlist().addFile(secondTrack);
            QCOMPARE(engine.playlist().size(), usize{2});
            // Coalescing itself: the file must not exist yet, because no event
            // loop has run and the debounce has not elapsed.
            QVERIFY2(!fs::exists(sessionPath),
                     why("the session playlist was written synchronously; coalescing is broken"));
        }

        QVERIFY2(fs::exists(sessionPath), why("the destructor did not flush the session playlist"));

        Playlist reloaded;
        QVERIFY(reloaded.loadM3U(sessionPath));
        QCOMPARE(reloaded.size(), usize{2});
        QVERIFY2(reloaded.itemAt(0).has_value(), why("the flushed playlist lost its first track"));
        QVERIFY2(!reloaded.itemAt(0)->isRemote, why("a local file came back marked remote"));
        QVERIFY2(reloaded.itemAt(1).has_value(), why("the flushed playlist lost its second track"));
        QVERIFY2(!reloaded.itemAt(1)->isRemote, why("a local file came back marked remote"));
    }

    // ------------------------------------------------------------------
    // AudioQueue drop accounting
    //
    // pushInternal() enqueues AUDIO_FRAME_SAMPLES (8) frames at a time and
    // returns on the first refused chunk, so the frames behind it are never
    // offered to that queue at all. Counting only the refused chunk -- one
    // chunk, i.e. 8 -- made 512 frames into a capacity-1 queue report
    // "8 dropped / 504 accepted" while totalPushed() still counted all 512, so
    // "pushed minus dropped" claimed 504 frames had reached the visualizer and
    // the recorder when 8 had. The invariant the metrics have to carry, per
    // queue, is therefore
    //
    //     frames_drained + dropCount == frames_offered_to_that_queue
    //
    // and totalPushed() counts offered frames once per queue, so after one
    // pushAll() of F frames it reads 2F. That doubling is not a bug and is
    // pinned here so a future "fix" does not quietly change what the number
    // means; the per-queue identity is what is actually asserted.
    // ------------------------------------------------------------------

    void partialRefusalAccountsForEveryFrameOffered() {
        AudioQueue queue(/*capacity=*/1);
        constexpr u32 kOffered = 512;
        const std::vector<float> pcm = makePcm(kOffered);

        // A refusal is reported, not swallowed.
        QVERIFY(!queue.pushAll(pcm.data(), kOffered, /*channels=*/2, 48000));

        // totalPushed_ counts what was OFFERED and is untouched by the refusal:
        // 512 offered to each of the two queues.
        QCOMPARE(queue.totalPushed(), u64{kOffered} * 2);

        const u64 vizAccepted = drainFrames(queue, /*viz=*/true);
        const u64 recAccepted = drainFrames(queue, /*viz=*/false);

        // A partial refusal really happened, so this is not "all in" and not
        // "all out": neither of those would be the bug.
        QVERIFY2(vizAccepted > 0, why("the visualizer queue accepted nothing at all"));
        QVERIFY2(vizAccepted < kOffered,
                 why("the visualizer queue accepted all " + std::to_string(vizAccepted) +
                     " frames, so nothing was refused"));
        QVERIFY2(recAccepted > 0, why("the recorder queue accepted nothing at all"));
        QVERIFY2(recAccepted < kOffered,
                 why("the recorder queue accepted all " + std::to_string(recAccepted) +
                     " frames, so nothing was refused"));

        // The defect itself: the un-attempted remainder was not counted. Pre-fix
        // these read 8 + 8 = 16, not 512.
        QCOMPARE(vizAccepted + queue.vizDropCount(), u64{kOffered});
        QCOMPARE(recAccepted + queue.recDropCount(), u64{kOffered});
        QVERIFY(queue.vizDropCount() > 0);
        QVERIFY(queue.recDropCount() > 0);
    }

    void dropIdentityHoldsForEveryOfferedAndCapacity_data() {
        QTest::addColumn<int>("offered");
        QTest::addColumn<int>("capacity");

        // Offer counts around the 8-frame chunk boundary, around whole frames,
        // and far past both, crossed with capacities on both sides of a chunk,
        // a frame and many frames -- so the walker's `remaining` is exercised at
        // every alignment the chunk loop can produce. Deliberately no capacity
        // is assumed of any pair: the invariant under test is the SUM, which
        // holds whatever the underlying queue's real capacity turns out to be.
        const int offered[] = {1, 8, 9, 16, 17, 64, 65, 512, 513};
        const int capacity[] = {1, 2, 3, 8, 64};
        for (int cap : capacity) {
            for (int off : offered) {
                const QByteArray tag =
                        QStringLiteral("offered-%1-capacity-%2").arg(off).arg(cap).toLatin1();
                QTest::newRow(tag.constData()) << off << cap;
            }
        }
    }

    void dropIdentityHoldsForEveryOfferedAndCapacity() {
        QFETCH(int, offered);
        QFETCH(int, capacity);
        const auto frames = static_cast<u32>(offered);
        const u64 offeredFrames = static_cast<u64>(offered);
        AudioQueue queue(static_cast<u32>(capacity));
        const std::vector<float> pcm = makePcm(frames);

        const bool accepted = queue.pushAll(pcm.data(), frames, 2, 48000);

        // Offered frames are counted in full, twice, whatever the queues did.
        QCOMPARE(queue.totalPushed(), offeredFrames * 2);

        const u64 vizAccepted = drainFrames(queue, /*viz=*/true);
        const u64 recAccepted = drainFrames(queue, /*viz=*/false);
        QCOMPARE(vizAccepted, recAccepted);

        // The identity, for both queues.
        QCOMPARE(vizAccepted + queue.vizDropCount(), offeredFrames);
        QCOMPARE(recAccepted + queue.recDropCount(), offeredFrames);

        // pushAll's result is the AND of the two queues, which are fed the same
        // chunk and share a capacity, so it is exactly "did either refuse".
        // Pinning it means the return value cannot drift from the counters.
        QCOMPARE(accepted, vizAccepted == offeredFrames);
    }

    void fullyAcceptedPushReportsZeroDrops() {
        AudioQueue queue(/*capacity=*/4096);
        constexpr u32 kOffered = 512;
        const std::vector<float> pcm = makePcm(kOffered);

        QVERIFY(queue.pushAll(pcm.data(), kOffered, /*channels=*/2, 48000));
        QCOMPARE(queue.vizDropCount(), u64{0});
        QCOMPARE(queue.recDropCount(), u64{0});
        QCOMPARE(queue.totalPushed(), u64{kOffered} * 2);
        QCOMPARE(drainFrames(queue, /*viz=*/true), u64{kOffered});
        QCOMPARE(drainFrames(queue, /*viz=*/false), u64{kOffered});

        // Draining is not a drop, and an exhausted queue reports empty -- which
        // is what makes the drain above a complete count rather than a partial
        // one, so the identity is not resting on an under-read.
        QCOMPARE(queue.vizDropCount(), u64{0});
        QCOMPARE(queue.recDropCount(), u64{0});
        AudioFrame frame;
        QVERIFY(!queue.popViz(frame));
        QVERIFY(!queue.popRec(frame));
        QCOMPARE(drainFrames(queue, /*viz=*/true), u64{0});
        QCOMPARE(drainFrames(queue, /*viz=*/false), u64{0});
    }

    // The invariant the identity rests on: totalPushed_ counts frames OFFERED,
    // so it keeps counting while every subsequent push is refused outright. If
    // it ever counted accepts instead, `drained + dropped == offered` would be
    // true by construction and would prove nothing.
    void totalPushedCountsOfferedFramesEvenWhenEverythingIsRefused() {
        AudioQueue queue(/*capacity=*/1);
        constexpr u32 kFirst = 64;
        const std::vector<float> first = makePcm(kFirst);

        QVERIFY(!queue.pushAll(first.data(), kFirst, /*channels=*/2, 48000));
        QCOMPARE(queue.totalPushed(), u64{kFirst} * 2);
        const u64 firstDrops = queue.vizDropCount();
        QVERIFY(firstDrops > 0);

        // The queue is now full and is never drained, so every following push is
        // refused on its very first chunk and contributes its WHOLE frame count
        // to the drop total -- 8 each, not 1.
        for (int i = 0; i < 4; ++i) {
            const std::vector<float> more = makePcm(8);
            QVERIFY(!queue.pushAll(more.data(), 8, /*channels=*/2, 48000));
        }
        QCOMPARE(queue.totalPushed(), u64{kFirst} * 2 + u64{8} * 4 * 2);
        QCOMPARE(queue.vizDropCount(), firstDrops + 8 * 4);
        QCOMPARE(queue.recDropCount(), firstDrops + 8 * 4);

        const u64 vizAccepted = drainFrames(queue, /*viz=*/true);
        const u64 recAccepted = drainFrames(queue, /*viz=*/false);
        QCOMPARE(vizAccepted, recAccepted);
        QCOMPARE(vizAccepted + queue.vizDropCount(), u64{kFirst + 8 * 4});
        QCOMPARE(recAccepted + queue.recDropCount(), u64{kFirst + 8 * 4});
    }

    // The renderer sizes its per-tick PCM batch from this value: a 44.1 kHz
    // sink batch-sized as 48 kHz misfeeds projectM ~9% of each second's PCM
    // and misaligns beat detection. The default only survives until the
    // first push.
    void theQueueCarriesThePushedSampleRate() {
        AudioQueue queue;
        QCOMPARE(queue.sampleRate(), u32{48'000});

        const std::vector<float> frame = makePcm(1);
        QVERIFY(queue.pushAll(frame.data(), 1, /*channels=*/2, 44'100));
        QCOMPARE(queue.sampleRate(), u32{44'100});
    }
};

#include "test_Playlist.moc"

int runTestPlaylist(int argc, char** argv) {
    TestPlaylist tc;
    return QTest::qExec(&tc, argc, argv);
}
