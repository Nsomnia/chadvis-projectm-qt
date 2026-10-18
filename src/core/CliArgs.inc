// CliArgs.inc — Declarative CLI argument table (per-type X-macro)
//
// Each entry uses a TYPE-SPECIFIC macro name so that only the matching
// macro is ever invoked during expansion. This avoids the problem where
// a single CLI_ARG macro dispatches to all type macros and wrong-type
// expansions compile invalid code (e.g., std::stoi on a bool field).
//
// Macro signatures:
//   CLI_SECTION(name)
//   CLI_BOOL(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor)
//   CLI_INT(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor)
//   CLI_FLOAT(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor)
//   CLI_STRING(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor)
//   CLI_PATH(longName, shortName, helpText, helpDefault, negationName, optsField, configAccessor)
//
// longName:       --flag-name (string_view)
// shortName:      -x or "" (string_view)
// helpText:       Description for --help
// helpDefault:    Default value string for --help, or ""
// negationName:   --no-flag-name for BOOL flags, or ""
// optsField:      AppOptions member name (for parseArgs)
// configAccessor: CONFIG accessor path (for init overrides), or _ for none
//
// CLI_SECTION groups entries for generated output (T0207): --help sections,
// --help <topic> pages, and shell completion are all expanded from this
// table, so adding a flag here is sufficient — no hand-written list to
// drift. Every include site must define CLI_SECTION (possibly empty).
//
// ─── General ────────────────────────────────────────────────
CLI_SECTION("General Options")
CLI_BOOL("--debug",          "-d", "Enable debug logging",              "no",  "",                            debug,             _)
CLI_BOOL("--headless",       "",    "Run without GUI (batch mode)",     "no",  "",                            headless,           _)
// ─── Config ─────────────────────────────────────────────────
CLI_SECTION("Config")
CLI_PATH("--config",         "-c",  "Use custom config file",           "",    "",                            configFile,         _)
// ─── Audio ──────────────────────────────────────────────────
CLI_SECTION("Audio Options")
CLI_STRING("--audio-device", "",    "Audio output device",              "default","",                          audioDevice,        audio().device)
CLI_INT("--audio-buffer",    "",    "Buffer size in samples",           "2048","",                            audioBufferSize,    audio().bufferSize)
CLI_INT("--audio-rate",      "",    "Sample rate in Hz",                "44100","",                           audioSampleRate,    audio().sampleRate)
// ─── Visualizer ─────────────────────────────────────────────
CLI_SECTION("Visualizer Options")
CLI_STRING("--preset",       "-p",  "Start with specific preset",       "",    "",                            presetName,         _)
CLI_BOOL("--default-preset", "", "Use projectM's default (no preset)","no", "", useDefaultPreset, _)
CLI_INT("--visualizer-fps",  "",    "Target frame rate",                "60",  "",                            visualizerFps,      visualizer().fps)
CLI_INT("--visualizer-width","",    "Render width",                     "1920","",                            visualizerWidth,    visualizer().width)
CLI_INT("--visualizer-height","",   "Render height",                    "1080","",                            visualizerHeight,   visualizer().height)
CLI_BOOL("--visualizer-shuffle","", "Shuffle presets",                  "yes", "--no-visualizer-shuffle",     visualizerShuffle,  visualizer().shufflePresets)
// ─── Recording ──────────────────────────────────────────────
CLI_SECTION("Recording Options")
CLI_BOOL("--record",         "-r",  "Start recording immediately",      "no",  "",                            startRecording,     _)
CLI_PATH("--output",         "-o",  "Recording output file",            "",    "",                            outputFile,         _)
CLI_STRING("--recording-codec","",  "Video codec (libx264/nvenc/vaapi)", "libx264","",                         recordingCodec,     recording().video.codec)
CLI_INT("--recording-crf",   "",    "Quality 0-51 (lower=better)",      "18",  "",                            recordingCrf,       recording().video.crf)
CLI_STRING("--recording-preset","", "Encoding speed",                   "medium","",                           recordingPreset,    recording().video.preset)
// ─── Suno ───────────────────────────────────────────────────
CLI_SECTION("Suno Options")
CLI_STRING("--suno-id",      "",    "Fetch and play Suno song by ID",   "",    "",                            sunoId,             _)
CLI_PATH("--suno-download-path","", "Download directory",               "",    "",                            sunoDownloadPath,   suno().downloadPath)
CLI_BOOL("--suno-auto-download","", "Auto-download when playing",       "no",  "--no-suno-auto-download",     sunoAutoDownload,   suno().autoDownload)
// ─── Karaoke ────────────────────────────────────────────────
CLI_SECTION("Karaoke Options")
CLI_PATH("--test-lyrics",    "",    "Test with SRT/LRC file",           "",    "",                            testLyricsFile,     _)
CLI_BOOL("--karaoke-enabled","",    "Enable karaoke display",           "yes", "--no-karaoke",                karaokeEnabled,     karaoke().enabled)
CLI_STRING("--karaoke-font", "",    "Font family",                      "Arial","",                            karaokeFont,        karaoke().fontFamily)
CLI_INT("--karaoke-font-size","",   "Font size in pixels",              "32",  "",                            karaokeFontSize,    karaoke().fontSize)
CLI_FLOAT("--karaoke-y-position","", "Vertical position (0=top, 1=bottom)", "0.5","",                        karaokeYPosition,   karaoke().yPosition)
// ─── UI ─────────────────────────────────────────────────────
CLI_SECTION("UI Options")
// T0192. The default here is only what --help prints; an absent --theme is
// detected by the empty string, NOT by this default, because defaulting the
// parsed value to a theme name would make "user did not pass --theme" and
// "user passed --theme material" indistinguishable -- and the first must leave
// the saved theme alone. Session-only: consumed by SettingsBridge.sessionTheme,
// never applied to CONFIG (see the applyOverride site in Application.cpp).
CLI_STRING("--theme",        "",    "UI theme, this session only (material/chadvis-dark/gruvbox/nord)", "material", "",    theme,              ui().theme)
