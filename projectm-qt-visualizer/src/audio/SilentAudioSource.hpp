/**
 * @file SilentAudioSource.hpp
 * @brief Provides silent PCM data to keep projectM alive when no audio plays.
 *
 * Junior developer note: "Why feed silence?"
 * Senior developer answer: "projectM's visualization is driven by audio data.
 * With no data, nothing happens. Silent data = idle animation."
 */
#ifndef SILENTAUDIOSOURCE_HPP
#define SILENTAUDIOSOURCE_HPP
#include <vector>
#include <cstddef>
class SilentAudioSource
{
public:
explicit SilentAudioSource(size_t bufferSize = 2048);
const float* getData() const { return m_buffer.data(); }
size_t getSampleCount() const { return m_buffer.size() / 2; } // Stereo
size_t getBufferSize() const { return m_buffer.size(); }
private:
std::vector<float> m_buffer;
};
#endif // SILENTAUDIOSOURCE_HPP
