#pragma once

// ═══════════════════════════════════════════════════════════════
// Setting implementation macros for SettingsBridge.cpp
//
// These generate getter/setter bodies only. The .hpp declarations
// (Q_PROPERTY, function signatures, signals) remain manual because:
//   1. Qt moc has edge cases with macro-generated Q_PROPERTY
//   2. The header is the API contract — it should be explicit
//   3. IDE navigation works naturally with explicit declarations
//
// Usage:
//   SETTING_INT(audioBufferSize, AudioBufferSize, audio().bufferSize,
//               audioMut().bufferSize, vc::u32)
//   SETTING_BOOL(visualizerShufflePresets, VisualizerShufflePresets,
//                visualizer().shufflePresets, visualizerMut().shufflePresets)
//   SETTING_FLOAT(visualizerBeatSensitivity, VisualizerBeatSensitivity,
//                 visualizer().beatSensitivity, visualizerMut().beatSensitivity, vc::f32)
//   SETTING_STRING(recorderPreset, RecorderPreset, recording().video.preset,
//                  recordingMut().video.preset)
//   SETTING_RO_STRING(keyboardPlayPause, keyboard().playPause)
// ═══════════════════════════════════════════════════════════════

// ─── INT (int ↔ vc::u32/vc::i32) ─────────────────────────────
// prop:    property name (camelCase, e.g. audioBufferSize)
// Prop:    capitalized property name for setter (e.g. AudioBufferSize)
// accessor: Config READ accessor path (e.g. audio().bufferSize) — const
//           overload, never marks dirty (T0022)
// writeAccessor: the WRITE spelling of the same path (e.g. audioMut().bufferSize)
// CastTo:  C++ type to cast to on write (e.g. vc::u32)
#define SETTING_INT(prop, Prop, accessor, writeAccessor, CastTo) \
    int SettingsBridge::prop() const { \
        return static_cast<int>(vc::Config::instance().accessor); \
    } \
    void SettingsBridge::set##Prop(int value) { \
        if (value != prop()) { \
            vc::Config::instance().writeAccessor = static_cast<CastTo>(value); \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── BOOL (bool ↔ bool) ──────────────────────────────────────
#define SETTING_BOOL(prop, Prop, accessor, writeAccessor) \
    bool SettingsBridge::prop() const { \
        return vc::Config::instance().accessor; \
    } \
    void SettingsBridge::set##Prop(bool value) { \
        if (value != prop()) { \
            vc::Config::instance().writeAccessor = value; \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── FLOAT (double ↔ vc::f32) ────────────────────────────────
// Uses qAbs() > 0.001 for fuzzy comparison to avoid no-op updates
#define SETTING_FLOAT(prop, Prop, accessor, writeAccessor, CastTo) \
    double SettingsBridge::prop() const { \
        return static_cast<double>(vc::Config::instance().accessor); \
    } \
    void SettingsBridge::set##Prop(double value) { \
        if (qAbs(value - prop()) > 0.001) { \
            vc::Config::instance().writeAccessor = static_cast<CastTo>(value); \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── STRING (QString ↔ std::string) ──────────────────────────
#define SETTING_STRING(prop, Prop, accessor, writeAccessor) \
    QString SettingsBridge::prop() const { \
        return QString::fromStdString(vc::Config::instance().accessor); \
    } \
    void SettingsBridge::set##Prop(const QString& value) { \
        if (value != prop()) { \
            vc::Config::instance().writeAccessor = value.toStdString(); \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── COLOR (QString ↔ vc::Color) ──────────────────────────────
// Stored as a hex string in `vc::Color`; QML sees it as the same "#rrggbb"
// text the colour pickers already hold, so no QColor conversion is needed on
// the QML side and a round-trip through the config file is lossless.
//
// T0047: the appearance colours were the one setting family with a parsed,
// persisted config field (`accent_color` / `visualizer_background`, see
// ConfigParsers.cpp) and no bridge that could write them. Theme.qml's
// applyAccent/applyBackground mutated only the in-memory singleton, so the UI
// said "auto-saved as you tweak" and saved nothing. Wiring them through this
// bridge is what makes that claim true.
#define SETTING_COLOR(prop, Prop, accessor, writeAccessor) \
    QString SettingsBridge::prop() const { \
        return QString::fromStdString(vc::Config::instance().accessor.toHex()); \
    } \
    void SettingsBridge::set##Prop(const QString& value) { \
        if (value != prop()) { \
            vc::Config::instance().writeAccessor = vc::Color::fromHex(value.toStdString()); \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── READ-ONLY STRING ────────────────────────────────────────
#define SETTING_RO_STRING(prop, accessor) \
    QString SettingsBridge::prop() const { \
        return QString::fromStdString(vc::Config::instance().accessor); \
    }
