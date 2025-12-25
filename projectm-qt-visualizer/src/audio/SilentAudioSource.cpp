/**
 * @file SilentAudioSource.cpp
 * @brief Implementation of silent audio source.
 */
#include "SilentAudioSource.hpp"
SilentAudioSource::SilentAudioSource(size_t bufferSize)
: m_buffer(bufferSize, 0.0f)
{
// Buffer is pre-filled with zeros (silence)
// That's it. It's literally just zeros.
// Sometimes the Chad solution is the simple solution.
}
