#include <QtTest>

#include "audio/AudioAnalyzer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cmath>
#include <numbers>
#include <numeric>
#include <span>
#include <thread>
#include <vector>

using vc::AudioAnalyzer;
using vc::AudioSpectrum;
using vc::f32;
using vc::FFT_SIZE;
using vc::SPECTRUM_SIZE;

namespace {

std::vector<f32> tone(std::size_t bin)
{
    std::vector<f32> samples(FFT_SIZE);
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const f32 phase = 2.0f * std::numbers::pi_v<f32> *
                          static_cast<f32>(bin * index) /
                          static_cast<f32>(FFT_SIZE);
        samples[index] = std::sin(phase);
    }
    return samples;
}

AudioSpectrum analyze(AudioAnalyzer& analyzer, const std::vector<f32>& samples)
{
    return analyzer.analyze(std::span<const f32>(samples), 48000, 1);
}

bool isFiniteSpectrum(const AudioSpectrum& spectrum)
{
    return std::all_of(spectrum.magnitudes.begin(), spectrum.magnitudes.end(),
                       [](f32 value) { return std::isfinite(value) && value >= 0.0f; });
}

std::size_t dominantBin(const AudioSpectrum& spectrum)
{
    return static_cast<std::size_t>(
        std::distance(spectrum.magnitudes.begin(),
                      std::max_element(spectrum.magnitudes.begin(),
                                       spectrum.magnitudes.end())));
}

} // namespace

class TestAudioAnalyzer : public QObject
{
    Q_OBJECT

private slots:
    void interiorToneProducesExpectedBinAndLevel()
    {
        AudioAnalyzer analyzer;
        const auto samples = tone(16);
        const AudioSpectrum spectrum = analyze(analyzer, samples);

        QVERIFY(isFiniteSpectrum(spectrum));
        QVERIFY(std::isfinite(spectrum.leftLevel));
        QVERIFY(std::isfinite(spectrum.rightLevel));
        QVERIFY(std::abs(spectrum.leftLevel - 0.70710678f) < 0.001f);
        QVERIFY(std::abs(spectrum.rightLevel - 0.70710678f) < 0.001f);
        QCOMPARE(dominantBin(spectrum), std::size_t{16});
        QVERIFY(spectrum.magnitudes[16] > 0.65f);
    }

    void zeroInputAndResetAreDeterministic()
    {
        AudioAnalyzer zeroAnalyzer;
        const std::vector<f32> silence(FFT_SIZE, 0.0f);
        const AudioSpectrum silenceSpectrum = analyze(zeroAnalyzer, silence);
        QVERIFY(isFiniteSpectrum(silenceSpectrum));
        for (const f32 magnitude : silenceSpectrum.magnitudes)
        {
            QVERIFY(std::abs(magnitude) < 0.000001f);
        }

        AudioAnalyzer analyzer;
        const auto samples = tone(31);
        const AudioSpectrum first = analyze(analyzer, samples);
        analyzer.reset();
        const AudioSpectrum second = analyze(analyzer, samples);

        QCOMPARE(dominantBin(first), std::size_t{31});
        QCOMPARE(dominantBin(second), std::size_t{31});
        for (std::size_t index = 0; index < SPECTRUM_SIZE; ++index)
        {
            QVERIFY(std::abs(first.magnitudes[index] - second.magnitudes[index]) <
                    0.000001f);
        }
    }

    void independentAnalyzersDoNotShareFftScratch()
    {
        constexpr std::size_t workerCount = 4;
        constexpr std::array<std::size_t, workerCount> bins{8, 31, 127, 300};
        std::array<AudioAnalyzer, workerCount> analyzers;
        std::array<std::vector<f32>, workerCount> samples{
            tone(bins[0]), tone(bins[1]), tone(bins[2]), tone(bins[3])};
        std::array<std::atomic_bool, workerCount> valid;
        for (auto& result : valid)
        {
            result.store(true, std::memory_order_relaxed);
        }
        std::barrier start(workerCount);
        std::vector<std::thread> workers;
        workers.reserve(workerCount);

        for (std::size_t worker = 0; worker < workerCount; ++worker)
        {
            workers.emplace_back([&, worker] {
                start.arrive_and_wait();
                for (int iteration = 0; iteration < 40; ++iteration)
                {
                    const AudioSpectrum spectrum = analyze(analyzers[worker], samples[worker]);
                    if (!isFiniteSpectrum(spectrum) || dominantBin(spectrum) != bins[worker] ||
                        spectrum.magnitudes[bins[worker]] < 0.6f)
                    {
                        valid[worker].store(false, std::memory_order_relaxed);
                        return;
                    }
                }
            });
        }

        for (auto& worker : workers)
        {
            worker.join();
        }
        for (std::size_t worker = 0; worker < workerCount; ++worker)
        {
            QVERIFY(valid[worker].load(std::memory_order_relaxed));
        }
    }

    void resetAnalyzeAndCopyCanRunConcurrently()
    {
        AudioAnalyzer analyzer;
        const auto samples = tone(64);
        std::atomic_bool valid{true};

        std::thread analyzerWorker([&] {
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                if (!isFiniteSpectrum(analyze(analyzer, samples)))
                {
                    valid.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
        std::thread resetWorker([&] {
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                analyzer.reset();
            }
        });
        std::thread copyWorker([&] {
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                const auto pcm = analyzer.pcmData();
                if (pcm.size() > FFT_SIZE || !std::all_of(pcm.begin(), pcm.end(), [](f32 value) {
                        return std::isfinite(value);
                    }))
                {
                    valid.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });

        analyzerWorker.join();
        resetWorker.join();
        copyWorker.join();
        QVERIFY(valid.load(std::memory_order_relaxed));
    }

    void invalidArgumentsReturnDeterministicSilence()
    {
        AudioAnalyzer analyzer;
        const std::vector<f32> samples(FFT_SIZE, 0.5f);
        const AudioSpectrum noChannels = analyzer.analyze(
            std::span<const f32>(samples), 48000, 0);
        const AudioSpectrum noRate = analyzer.analyze(
            std::span<const f32>(samples), 0, 1);

        QVERIFY(isFiniteSpectrum(noChannels));
        QVERIFY(isFiniteSpectrum(noRate));
        QCOMPARE(noChannels.leftLevel, 0.0f);
        QCOMPARE(noRate.leftLevel, 0.0f);
    }
};

int main(int argc, char** argv)
{
    TestAudioAnalyzer test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_AudioAnalyzer.moc"
