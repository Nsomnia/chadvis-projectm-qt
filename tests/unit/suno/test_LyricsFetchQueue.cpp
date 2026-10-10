#include <QtTest>

#include "suno/SunoLyricsManager.hpp"

using namespace vc::suno;

// The enqueue policy for lyrics fetches, tested on its own because the
// manager itself is not constructible in a unit test (its client owns a
// keychain-reading worker). A page pass may not double-enqueue an id; the
// pending set may not grow without limit; a "still processing" requeue must
// be able to return an id that just popped; and drop-new keeps FIFO order.
class TestLyricsFetchQueue : public QObject {
    Q_OBJECT

private slots:
    void duplicatesAreRefused();
    void theCapDropsNewNotOld();
    void aPoppedIdCanBeRequeued();
    void clearForgetsThePast();
    void fifoOrderIsPreserved();
};

void TestLyricsFetchQueue::duplicatesAreRefused() {
    LyricsFetchQueue queue;
    QVERIFY(queue.tryEnqueue("a"));
    QVERIFY(!queue.tryEnqueue("a")); // the O(n^2) page storm died here
    QCOMPARE(queue.size(), std::size_t{1});
}

void TestLyricsFetchQueue::theCapDropsNewNotOld() {
    LyricsFetchQueue queue;
    for (std::size_t i = 0; i < 3; ++i) {
        QVERIFY(queue.tryEnqueue(std::to_string(i), 3));
    }
    QVERIFY(!queue.tryEnqueue("overflow", 3)); // drop-new, FIFO survives

    const auto head = queue.pop();
    QVERIFY2(head.has_value() && *head == "0", "the cap dropped the FIFO head, not the tail");

    // The freed slot admits the next arrival.
    QVERIFY(queue.tryEnqueue("next", 3));
    QCOMPARE(queue.size(), std::size_t{3});
}

void TestLyricsFetchQueue::aPoppedIdCanBeRequeued() {
    LyricsFetchQueue queue;
    QVERIFY(queue.tryEnqueue("a"));
    const auto popped = queue.pop();
    QVERIFY2(popped.has_value() && *popped == "a", "pop did not return the head");
    QVERIFY(!queue.contains("a"));
    QVERIFY(queue.tryEnqueue("a")); // the "still processing" retry path
    QCOMPARE(queue.size(), std::size_t{1});
}

void TestLyricsFetchQueue::clearForgetsThePast() {
    LyricsFetchQueue queue;
    QVERIFY(queue.tryEnqueue("a"));
    QVERIFY(queue.tryEnqueue("b"));
    queue.clear();
    QVERIFY(queue.empty());
    QVERIFY(!queue.contains("a"));
    QVERIFY(queue.tryEnqueue("a")); // a cleared queue re-admits everything
}

void TestLyricsFetchQueue::fifoOrderIsPreserved() {
    LyricsFetchQueue queue;
    QVERIFY(queue.tryEnqueue("a"));
    QVERIFY(queue.tryEnqueue("b"));
    QVERIFY(!queue.tryEnqueue("a")); // mid-queue duplicate
    QVERIFY(queue.tryEnqueue("c"));

    const auto first = queue.pop();
    const auto second = queue.pop();
    const auto third = queue.pop();
    QVERIFY2(first.has_value() && *first == "a", "FIFO head was not 'a'");
    QVERIFY2(second.has_value() && *second == "b", "FIFO middle was not 'b'");
    QVERIFY2(third.has_value() && *third == "c", "FIFO tail was not 'c'");
    QVERIFY(!queue.pop().has_value());
}

#include "test_LyricsFetchQueue.moc"

int runTestLyricsFetchQueue(int argc, char** argv) {
    TestLyricsFetchQueue tc;
    return QTest::qExec(&tc, argc, argv);
}
