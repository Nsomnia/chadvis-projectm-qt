// test_RatingManager.cpp -- ratings are keyed on the preset file path, and the
// shared map is safe to touch from more than one thread (T0058).
//
// The key used to be the preset NAME, so two banks holding `same.milk` shared one
// rating. The singleton is process-wide and the GUI thread is not the only
// reader, so both halves are pinned here: identity, and concurrent access.

#include <QtTest>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "visualizer/RatingManager.hpp"

using namespace vc;

class TestRatingManager : public QObject {
    Q_OBJECT

private slots:
    void sameNameInDifferentBanksKeepsSeparateRatings();
    void unratedPathReadsZeroAndStarsClamp();
    void concurrentReadsAndWritesStayConsistent();
};

void TestRatingManager::sameNameInDifferentBanksKeepsSeparateRatings() {
    // Deliberately the same file name in two directories: the name is what a
    // name-keyed map would collapse.
    const std::string bankA = "/rating-test/bank-a/same.milk";
    const std::string bankB = "/rating-test/bank-b/same.milk";

    RatingManager& ratings = RatingManager::instance();
    ratings.setRating(bankA, 5);
    ratings.setRating(bankB, 2);

    QCOMPARE(ratings.getRating(bankA), 5);
    QCOMPARE(ratings.getRating(bankB), 2);

    // A later write to one must not follow the other.
    ratings.setRating(bankA, 1);
    QCOMPARE(ratings.getRating(bankA), 1);
    QCOMPARE(ratings.getRating(bankB), 2);
}

void TestRatingManager::unratedPathReadsZeroAndStarsClamp() {
    RatingManager& ratings = RatingManager::instance();

    QCOMPARE(ratings.getRating("/rating-test/never-rated.milk"), 0);

    ratings.setRating("/rating-test/clamped.milk", 9);
    QCOMPARE(ratings.getRating("/rating-test/clamped.milk"), 5);

    ratings.setRating("/rating-test/clamped.milk", -3);
    QCOMPARE(ratings.getRating("/rating-test/clamped.milk"), 0);
}

void TestRatingManager::concurrentReadsAndWritesStayConsistent() {
    RatingManager& ratings = RatingManager::instance();
    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;

    // Each writer owns its own keys, so the expected value of every key is known
    // exactly: any lost, torn or reordered write shows up as a mismatch.
    std::atomic<bool> stop{false};
    std::vector<std::thread> writers;
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&ratings, t] {
            for (int i = 0; i < kPerThread; ++i) {
                ratings.setRating("/rating-test/thread-" + std::to_string(t) + "/p" +
                                          std::to_string(i),
                                  (i % 5) + 1);
            }
        });
    }

    // A reader on a key the writers hammer: an unlocked std::map here is a
    // crash under TSan and a garbage read without it.
    std::thread reader([&ratings, &stop] {
        while (!stop.load(std::memory_order_relaxed)) {
            (void)ratings.getRating("/rating-test/thread-0/p0");
            (void)ratings.getRating("/rating-test/thread-3/p199");
        }
    });

    for (auto& writer : writers)
        writer.join();
    stop.store(true);
    reader.join();

    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kPerThread; ++i) {
            const std::string key =
                    "/rating-test/thread-" + std::to_string(t) + "/p" + std::to_string(i);
            QCOMPARE(ratings.getRating(key), (i % 5) + 1);
        }
    }
}

#include "test_RatingManager.moc"

int runTestRatingManager(int argc, char** argv) {
    TestRatingManager tc;
    return QTest::qExec(&tc, argc, argv);
}
