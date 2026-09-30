/**
 * @file SubtitleBurnIn.hpp
 * @brief Optional post-pass: burn a subtitle track into a finished recording.
 *
 * This is a *post-pass over a finished file*, not part of recording, and that is
 * the entire point: changing the font or re-rendering a 2-second segment costs
 * 2 seconds rather than a 3-minute render. Nothing here touches the encode path.
 *
 * ## The optional dependency
 *
 * libavfilter is probed by CMake into `CHADVIS_HAS_AVFILTER` and is deliberately
 * not in `CHADVIS_FFMPEG_COMPONENTS`, which FATAL_ERRORs on a missing component.
 * If avfilter were mandatory, every stripped distro and every FFmpeg-4-era
 * container would fail to build the *whole application* -- including the Suno
 * client, which has nothing to do with video. So:
 *
 *   - with libavfilter    this file is compiled, `CHADVIS_HAS_AVFILTER=1` is
 *                         defined, and the library is linked;
 *   - without it          this file is not in the build at all, and
 *                         `burnInAvailable()` below reports it unsupported with
 *                         a reason naming the CMake option.
 *
 * ## The second gate
 *
 * A libavfilter built without libass has no `ass` or `subtitles` filter at all.
 * That is a *runtime* fact, not a configure-time one, and it is the case a
 * distro shipping a minimal libavfilter is most likely to hit. So
 * `burnInAvailable()` also asks `avfilter_get_by_name`, and a build where the
 * library is present but the filter is not reports itself unsupported rather
 * than failing at the first graph.
 *
 * ## libass
 *
 * libass is a dependency of the *filter*, not of this project, and is not looked
 * for separately. Measured: `libavfilter` links libass, libfreetype and
 * libfontconfig; `libavcodec` links none of them. The only thing to find is
 * libavfilter.
 *
 * ## Never a link error
 *
 * Every entry point returns a `Result` and every failure is reported, so a
 * missing filter, a missing font, a bad path and a full disk are all visible
 * conditions rather than crashes. The output file's own writes are classified
 * with the same `WriteOutcome` rules the recorder uses, so a burned file that
 * came out truncated is never reported as a success.
 */

#pragma once

#include <cstdint>
#include <string>

#include "util/Result.hpp"
#include "util/Types.hpp"

namespace vc {

/// What to burn, and where.
struct BurnInOptions {
    /// The finished recording. Read only -- this pass never rewrites it.
    fs::path inputVideo;
    /// An Advanced SubStation Alpha document. `nullptr`-equivalent: an empty
    /// path is an error, because a burn-in with no subtitles is a no-op that
    /// would otherwise be indistinguishable from a successful render.
    fs::path subtitleFile;
    /// Where to write. The container and codec pairings come from the extension
    /// and from `videoCodec` below, the same way recording does.
    fs::path outputVideo;

    /// libavcodec encoder name for the video. Empty means "copy the input
    /// codec's name", which is usually what you want for a visual edit: it
    /// re-encodes with the same codec at the same settings.
    std::string videoCodec;
    /// CRF, forwarded to the encoder. Only meaningful for the x264/x265 family.
    int crf{18};

    /// Extra directory to search for fonts, mapped to the filter's `fontsdir`.
    /// Empty -- the default -- means the system font path only.
    ///
    /// There is deliberately no font-name or font-size option here: the `ass`
    /// filter has neither, because the face and the size come from the script's
    /// [V4+ Styles] row. That is where the karaoke writer put them, and it is
    /// why the documented workflow is "edit the document, re-run the pass" rather
    /// than "pass a font".
    fs::path fontsDir;

    /// Passed to the filter as `original_size`, which is what libass scales the
    /// script's PlayRes against. (0,0) means "read it from the input".
    u32 sourceWidth{0};
    u32 sourceHeight{0};
};

/// Is this build able to burn in at all?
///
/// False when the build has no libavfilter, or when the library is present but
/// carries no `ass` filter. Never a crash, never a link error.
bool burnInAvailable();

/// Why `burnInAvailable()` said no, or an empty string when it said yes.
///
/// Worth surfacing to the user verbatim: "this build has no burn-in" and "this
/// libavfilter has no `ass` filter" are different problems with different fixes.
std::string burnInUnavailableReason();

/// Burn `subtitleFile` into the pixels of `inputVideo`, writing `outputVideo`.
///
/// A pure function of (input file, subtitle document, options) -> output file.
/// Re-runnable, which is the reason it exists: change the font, run it again
/// over a 2-second segment. The input is opened read-only and never rewritten.
///
/// Never leaves a plausible-looking broken file reported as a success: the
/// output is written through a helper that applies the same `WriteOutcome`
/// classification as the recorder, so a muxer that refuses a packet makes this
/// return an error.
Result<void> burnInSubtitles(const BurnInOptions& options);

} // namespace vc
