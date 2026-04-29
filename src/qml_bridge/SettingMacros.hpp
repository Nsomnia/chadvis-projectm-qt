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
//   SETTING_INT(audioBufferSize, AudioBufferSize, audio().bufferSize, vc::u32)
//   SETTING_BOOL(visualizerShufflePresets, VisualizerShufflePresets, visualizer().shufflePresets)
//   SETTING_FLOAT(visualizerBeatSensitivity, VisualizerBeatSensitivity, visualizer().beatSensitivity, vc::f32)
//   SETTING_STRING(recorderPreset, RecorderPreset, recording().video.preset)
//   SETTING_RO_STRING(keyboardPlayPause, keyboard().playPause)
// ═══════════════════════════════════════════════════════════════

// ─── INT (int ↔ vc::u32/vc::i32) ─────────────────────────────
// prop:    property name (camelCase, e.g. audioBufferSize)
// Prop:    capitalized property name for setter (e.g. AudioBufferSize)
// accessor: Config accessor path (e.g. audio().bufferSize)
// CastTo:  C++ type to cast to on write (e.g. vc::u32)
#define SETTING_INT(prop, Prop, accessor, CastTo) \
    int SettingsBridge::prop() const { \
        return static_cast<int>(vc::Config::instance().accessor); \
    } \
    void SettingsBridge::set##Prop(int value) { \
        if (value != prop()) { \
            vc::Config::instance().accessor = static_cast<CastTo>(value); \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── BOOL (bool ↔ bool) ──────────────────────────────────────
#define SETTING_BOOL(prop, Prop, accessor) \
    bool SettingsBridge::prop() const { \
        return vc::Config::instance().accessor; \
    } \
    void SettingsBridge::set##Prop(bool value) { \
        if (value != prop()) { \
            vc::Config::instance().accessor = value; \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── FLOAT (double ↔ vc::f32) ────────────────────────────────
// Uses qAbs() > 0.001 for fuzzy comparison to avoid no-op updates
#define SETTING_FLOAT(prop, Prop, accessor, CastTo) \
    double SettingsBridge::prop() const { \
        return static_cast<double>(vc::Config::instance().accessor); \
    } \
    void SettingsBridge::set##Prop(double value) { \
        if (qAbs(value - prop()) > 0.001) { \
            vc::Config::instance().accessor = static_cast<CastTo>(value); \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── STRING (QString ↔ std::string) ──────────────────────────
#define SETTING_STRING(prop, Prop, accessor) \
    QString SettingsBridge::prop() const { \
        return QString::fromStdString(vc::Config::instance().accessor); \
    } \
    void SettingsBridge::set##Prop(const QString& value) { \
        if (value != prop()) { \
            vc::Config::instance().accessor = value.toStdString(); \
            emit prop##Changed(); \
            scheduleAutoSave(); \
        } \
    }

// ─── READ-ONLY STRING ────────────────────────────────────────
#define SETTING_RO_STRING(prop, accessor) \
    QString SettingsBridge::prop() const { \
        return QString::fromStdString(vc::Config::instance().accessor); \
    }
