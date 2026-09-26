#include <QtTest>
#include <QObject>
#include <QTemporaryDir>
#include <cstddef>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "audio/AudioEngine.hpp"
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
        playlist.currentChanged.connect(
                [this, &playlist](std::optional<usize> index) {
                    emissions.push_back(index);
                    indexDuringEmit.push_back(playlist.currentIndex());
                    sizeDuringEmit.push_back(playlist.size());

                    const auto current = playlist.currentItem();
                    currentTitleDuringEmit.push_back(
                            current ? current->title() : std::string("<none>"));

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
                return;  // announced nothing, so there is nothing to settle
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

        // Shuffle walks a permutation. Note the traversal starts at shuffle
        // position 1, because setShuffle() leaves shufflePosition_ at 0 and
        // next() pre-increments — so the first entry of the shuffled order is
        // the one item the first pass never visits. That is the current
        // behaviour, pinned on purpose: an off-by-one in the traversal start
        // would otherwise be invisible, and changing it is a deliberate
        // navigation-semantics decision, not a refactor side effect.
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
        QCOMPARE(visited.size(), usize{2});
        QVERIFY2(visited[0] != visited[1],
                 why("the shuffled walk visited the same track twice: " + show(visited)));

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
            AudioEngine engine(sessionPath);
            QVERIFY(engine.init());
            engine.playlist().addUrl("https://example.invalid/one");
            engine.playlist().addUrl("https://example.invalid/two");
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
        QVERIFY2(reloaded.itemAt(0)->url == "https://example.invalid/one",
                 why(reloaded.itemAt(0)->url));
        QVERIFY2(reloaded.itemAt(1).has_value(), why("the flushed playlist lost its second track"));
        QVERIFY2(reloaded.itemAt(1)->url == "https://example.invalid/two",
                 why(reloaded.itemAt(1)->url));
    }
};

#include "test_Playlist.moc"

int runTestPlaylist(int argc, char** argv) {
    TestPlaylist tc;
    return QTest::qExec(&tc, argc, argv);
}
