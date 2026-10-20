---
name: chadvis-audio-recorder
description: >-
  Works on ChadVis's audio capture and FFmpeg recording pipeline: PCM conversion,
  device formats, encoding, muxing, resampling, and subtitle burn-in. Use whenever a
  task touches src/audio/, src/recorder/, EncoderSettings, FFmpeg return codes, or
  produces a file a user will play. Encodes the total-conversion contract, the
  split input/output format-context ownership, and the sanitizer gate.
---

# Audio and recording pipeline

Two subsystems, one rule each. Both rules exist because the naive version shipped a
silent bug rather than a crash.

## Audio: conversion is total

`src/audio/PcmFormat.hpp` owns conversion of interleaved PCM into `f32`, and it is a
**pure, device-free** free-function layer — no Qt object, no device, no logging, no
dependency upward into the QML bridge. That is deliberate: it is what makes the
unsupported-format contract testable headlessly.

The contract:

- `convertInterleaved()` either writes **exactly** as many floats as the byte count
  implies, or writes nothing and returns the reason.
- It never partially fills, never truncates, and **never falls back to whatever the
  destination already held**.
- `kMaxInterleavedSamples` (16384) is a property of the conversion, not of the
  caller's bookkeeping, so it can be pinned by a test.

The bug this replaced: an `if Float / else if Int16` with **no else** inside
`AudioEngine::processAudioBuffer`. Any other sample format fell through and the
engine pushed the *previous* scratch-buffer contents into the visualizer and
recorder queues — an Int32 sink produced stale audio and no diagnostic. If you find
yourself adding a format branch anywhere but `PcmFormat`, stop.

`AudioEngine` is GUI-thread-affine and `QMediaPlayer` is GUI-thread-driven: the
backend decodes on its own threads but delivers every signal to the object's
thread. Do not treat its internal threading as licence to touch `AudioEngine` state
elsewhere.

## Recording: two format contexts, not one

`src/recorder/FFmpegUtils.hpp` splits what used to be a single
`AVFormatContextPtr` into `AVFormatContextInPtr` and `AVFormatContextOutPtr`, so
handing a reader to the writer is a **compile error** rather than a segfault. Do not
"simplify" them back into one alias.

The ownership rules:

- **Input:** `avformat_close_input()` runs the demuxer's `read_close`, frees the
  context, and only then closes the `AVIOContext` it opened. Calling
  `avformat_free_context()` on a demuxer instead leaks that `AVIOContext`.
- **Output:** `avformat_free_context()` does not touch `pb` at all — libavformat
  documents the caller as its owner for muxing — so the output deleter closes it
  explicitly.
- **Never close `pb` before freeing the context.** A deleter that closed it first
  would double-close on every file read.
- The opt-out guard is **`AVFMT_FLAG_CUSTOM_IO` on `c->flags`**, not
  `oformat->flags`. `oformat` is null for a demuxer, which is exactly the class of
  bug that used to segfault a read.

FFmpeg return codes are a genuine exceptional path — check them and map them to
`std::expected` / `vc::Result<T>` rather than throwing.

## Timestamps

PTS derives from the frame's own **capture time**, never from a counter, and
monotonicity is guaranteed twice (`lastVideoPts_ + 1` floor plus a re-anchored
no-timestamp path). The origin is normalised to PTS 0 so a `steady_clock` reading
(µs since boot) never inflates the reported duration via the MP4 edit list.
`av_rescale_q` rather than a hand-rolled division: truncation biases every frame
one tick early *and accumulates*.

Stated limitation, not a bug: duration tracks frame count, so when the renderer
outruns the nominal record fps consecutive frames collapse to one tick. Fixing it
is frame-rate conversion — a different feature. Do not report it as a regression.

## Gates — sanitizers are mandatory here

```bash
./build.sh --fast --tests --safe
./build.sh --tsan --tests && ./build.sh --tsan --safe
./build.sh --asan --tests && ./build.sh --asan --safe
```

Raw memory and FFmpeg return codes are precisely the cases where a green normal
build means nothing. Run TSan and ASan **before** merging anything in
`src/recorder/` or touching session/threading behaviour.

Also verify the output actually plays. A file that muxes without error and is
upside down, silent, or has a wrong duration is the failure mode this subsystem
specialises in.

## Layout

`src/audio/`: `AudioEngine`, `AudioQueue`, `PcmFormat`, `LoudnessAnalysis`,
`Playlist`, `analysis/`. `src/recorder/`: `VideoRecorderCore`, `RenderQueue`,
`RenderExecutor`, `RenderJob`, `FrameGrabber`, `AudioFileDecoder`,
`ResamplerEngine`, `EncoderSettings`, `SubtitleBurnIn`, `OffscreenRenderSpike`.
`docs/dev/INVARIANTS.md` owns the *reasons*; this skill owns the workflow.
