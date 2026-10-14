# Theme Engine Reimplementation Plan

> Status: **Plan — not yet implemented.** This document is the complete
> implementation plan for reimplementing the theme engine / subsystem.
> No code changes ship with this document.
>
> Branch: `dev/bleeding-edge`
> Base verified: `dev/bleeding-edge` HEAD `77a0b19215bd3e682922d3f6e16a1984537d2217`
> (verified 2026-10-14 via `get_file_contents` + `list_branches`)
> Audience: implementer + reviewer. Source wins over this plan: if a path,
> default, or behaviour below disagrees with `src/`, `src/` wins and this
> document is the bug.
>
> One-fact-one-document rule: this file owns the theme-engine plan. It does
> not duplicate `docs/dev/ARCHITECTURE.md`, `docs/user/CONFIG.md`, or
> `docs/dev/INVARIANTS.md` — it links to them and records only what the
> implementation must change.

## 1. Goal

Reimplement the theme engine so that:

1. A **theme is a definition** — a named, versioned, data-only set of
   tokens — not a handful of hardcoded colours in one QML file plus two
   persisted colour fields.
2. **Built-in themes ship by default**, at minimum:
   `material`, `gruvbox`, plus the preserved current look as `chadvis-dark`
   and the already-advertised `nord` (see §5). Adding another built-in is
   adding one definition file, not a code change.
3. **User-defined themes** can be added as data files, without rebuilding,
   and appear in the Settings window alongside the built-ins (§6).
4. The **Settings window** gets only light modifications: a theme selector
   on the existing Appearance page (§9). No new window, no new save path,
   no new settings-page rail entry.
5. The current look, persistence, and startup ordering keep working
   throughout. No transparent-window regression, no singleton shadowing,
   no config field silently dropped on save.

### Non-goals (Phase 1–3)

- No light themes in Phase 1. The schema carries `isDark` so a light
  variant is a later definition, not a later redesign (§5.5).
- No per-token custom editor in Phase 1. The existing accent/background
  swatches stay; a full custom-theme editor is Phase 4.
- No theming of the projectM visualizer surface, karaoke colours, or
  overlay colours in Phase 1. Those keep their existing config keys.
- No remote themes, no theme store, no code execution from a theme file.
  Theme files are data. An invalid file warns and falls back; it never
  crashes, never runs code, never fetches anything.
- No renaming of the QML `Theme` singleton and no C++ bridge named
  `Theme` (see §3).

## 2. Current state (verified from source on `dev/bleeding-edge`)

### 2.1 One owner, two persisted colours

From `docs/dev/ARCHITECTURE.md` and the tree:

- **Exactly one owner of the theme:** `src/qml/styles/Theme.qml`
  (tokens) + `SettingsBridge` (two persisted colours). A C++
  `ThemeBridge` existed, could never be registered under the name `Theme`
  without shadowing the QML singleton, and was **deleted, not repaired**
  (T0055). Do not reintroduce it.
- `Theme.qml` (v1.1.0, `pragma Singleton`, `QtObject`) holds:
  - **Mutable:** accent family + background family (see §4.1).
  - **Readonly:** text, semantic, border, metrics, typography, shadows,
    glass.
  - Helpers: `parseRgbaHex()` / `toRgbaHex()`, `lighten()` / `darken()` /
    `withAlpha()`, `formatTime*()`, `themeChanged()` signal.
  - Mutators: `applyAccent(baseColor)` and `applyBackground(baseColor)`
    recompute derived variants and write back through
    `SettingsBridge.accentColor` / `SettingsBridge.backgroundColor`.
- `src/core/ConfigData.hpp` — `UIConfig`:
  `theme{"dark"}`, `backgroundColor{Color::black()}`,
  `accentColor{Color::fromHex("#00FF88")}`, plus panel/sidebar state.
- `src/core/ConfigParsers.cpp` — `CHADVIS_UI_FIELDS` parses and
  serializes `theme`, `visualizer_background` → `backgroundColor`,
  `accent_color` → `accentColor`, etc. `serialize()` rebuilds the whole
  file from the structs, so **any field not in that table does not
  survive a save**.
- `config/default.toml` — `[ui]`: `theme = 'dark'`,
  `accent_color = '#00FF88FF'`, `visualizer_background = '#000000FF'`
  (the background colour is stored under the `visualizer_background`
  key — a historical name; the value is the UI background, not a
  projectM-only value).
- `src/qml_bridge/SettingsBridgeSettings.inc` (X-macro, included twice)
  exposes `SETTING_COLOR(accentColor, …)` and
  `SETTING_COLOR(backgroundColor, …)`. There is **no** `theme` property
  on `SettingsBridge` today.
- `src/qml/main.qml` — `applyTheme()` calls
  `Theme.applyAccent(SettingsBridge.accentColor)` then
  `Theme.applyBackground(SettingsBridge.backgroundColor)`, seeded from
  `Component.onCompleted` in `main.qml`. Seeding from `Theme.qml`'s own
  `onCompleted` does **not** work (T0047, §3).
- `src/qml/SettingsWindow.qml` — Appearance page is index 5
  (`{ title: "Appearance", eyebrow: "Color and contrast" }`).
  `saveSettings()`, `onClosing`, and the reset dialog all funnel through
  `appearancePage.apply()` + `SettingsBridge.save()` /
  `SettingsBridge.resetToDefaults()`. The window palette is already
  bound to `Theme.*` throughout, so a theme change that fires
  `themeChanged()` repaints the Settings window with no structural
  change to that file.
- `src/qml/settings/AppearancePage.qml` — header + description +
  `AppearanceSettings` + `apply()` delegating to it.
- `src/qml/panels/settings/AppearanceSettings.qml` — `apply()` calls
  `Theme.applyAccent(Theme.toRgbaHex(Theme.accent))` and the background
  equivalent. The panel currently shows only two read-only swatches
  (accent, background) with hex text. There is **no theme selector**.
- CLI: `src/core/CliArgs.inc` —
  `CLI_STRING("--theme", …, "UI theme (dark/gruvbox/nord)", "dark", …,
  ui().theme)`, applied in `src/core/Application.cpp` via
  `applyOverride(opts.theme, CONFIG.ui().theme, "ui.theme")`.

### 2.2 What is actually broken / aspirational today

1. **`ui.theme` is stored but never consumed.** Nothing in QML reads
   `ui().theme`, and `SettingsBridge` does not expose it. The CLI help
   advertises `dark/gruvbox/nord`, but selecting `gruvbox` or `nord`
   changes nothing on screen. The only thing that visibly changes today
   is the two colours.
2. **Two sources of truth disagree about the defaults.** `Theme.qml`
   hardcodes accent `#00bcd4` (cyan) and background `#1a1a1a`; the
   config default is accent `#00FF88FF` (green) and background
   `#000000FF` (black). What the user actually sees after startup is
   the config-seeded green-on-black, because `main.qml` overwrites the
   QML literals. Any reimplementation must make the *definition* the
   single source of truth and delete the disagreement, not pick one
   literal over the other by accident.
3. **Derived values are recomputed, but readonly tokens are not
   themeable.** `textPrimary`, `success`, `error`, `border`, etc. are
   `readonly` literals in `Theme.qml`. A gruvbox theme cannot just
   change accent + background and look like gruvbox — its text,
   semantic, and border colours are part of the theme.
4. **Glass / glow colours hardcode cyan.** `glassBorder`,
   `glassHighlight`, and `glowAccent` use `Qt.rgba(0, 0.733, 0.831, …)`
   — the cyan accent as a literal. Under any non-cyan theme they stay
   cyan. They must be derived from the active accent.
5. **63 files reference `Theme.`** (code search, `Theme.` in
   `repo:Nsomnia/chadvis-projectm-qt`). That is the migration surface:
   the plan must keep the `Theme.<token>` names stable so those 63 files
   need no edits. A rename or a token removal is a 63-file migration;
   a definition swap behind stable names is not.
6. **No user-defined themes.** There is no discovery, no file format,
   no validation, and no fallback for an unknown theme id.

## 3. Design principles / invariants (must not break)

These are load-bearing. A PR that violates one is wrong even if it
compiles.

1. **One token owner.** `Theme.qml` stays the sole QML token owner.
   No C++ bridge named `Theme`. A new C++ helper (if any) gets a
   distinct name such as `ThemeCatalog` (see §8) and never shadows the
   QML singleton. (ARCHITECTURE.md, T0055.)
2. **Seeding order (T0047).** Theme state is seeded from `main.qml`
   `Component.onCompleted`, after `SettingsBridge` construction. Never
   seed from `Theme.qml`'s own `onCompleted` — a lazy singleton's
   completion is not ordered after the bridge's construction, and the
   symptom is a stale accent surviving a restart.
3. **Hex byte order.** Config / `vc::Color` hex is `#RRGGBBAA`.
   Qt parses 8-digit hex as `#AARRGGBB`. All conversion goes through
   the existing `Theme.parseRgbaHex()` / `Theme.toRgbaHex()` pair.
   Never use `Qt.color(x).toString()` as a write-back string — it
   round-trips alpha into the red channel and the corruption survives
   save + reload. (Theme.qml, T0047 comments.)
4. **Serialize rebuilds the file.** `ConfigParsers::serialize()`
   rebuilds the whole TOML from the structs. Any new persisted field
   must be added to the `CHADVIS_UI_FIELDS` table (parse + serialize)
   or it is silently dropped on the first save — the same class of bug
   as the old `gop_size` / `b_frames` and empty-encoder-table drops.
5. **X-macro double inclusion.** `SettingsBridgeSettings.inc` is
   included twice in `SettingsBridge.cpp`. A new setting needs one line
   there **plus** the `Q_PROPERTY` + getter/setter + signal in
   `SettingsBridge.hpp`, **plus** the emit-only stub in the
   `resetToDefaults()` redefinition. Forgetting the stub compiles as a
   function definition inside a function.
6. **Debounced save.** `SettingsBridge` setters auto-save after a 2 s
   debounce; `save()` is the explicit immediate path. Theme selection
   follows the same rule — no bespoke save timer, no direct
   `Config::save()` call from QML.
7. **Fusion style.** `Application.cpp` pins `QQuickStyle::setStyle("Fusion")`
   before the engine exists because the macOS native style discards
   custom control chrome. Theme work must not depend on the native
   style rendering custom backgrounds.
8. **No hardcoded hex in views.** After the reimplementation, no QML
   view/panel/component may carry a literal colour that duplicates a
   theme token. Audit with a `#RRGGBB` grep over `src/qml/`; every hit
   is either a theme definition or a bug.
9. **Fail closed on bad input.** Unknown theme id, malformed theme
   file, missing token, out-of-range value → log a warning, fall back
   to the default theme (§6.3), keep the window opaque and usable.
   Never a transparent window, never a crash, never a half-applied
   theme (see §11).

## 4. Theme model

### 4.1 Token inventory (from `Theme.qml`, verified)

| Family | Tokens | Today | After reimplementation |
| :--- | :--- | :--- | :--- |
| Accent (mutable) | `accent`, `accentHover`, `accentPressed`, `accentLight`, `accentDark` | Base + derived via `Qt.lighter` / `Qt.darker` in `applyAccent()` | Same derivation. Definition supplies `accent` (base); the rest are derived unless the definition explicitly overrides one. |
| Background (mutable) | `background`, `backgroundAlt`, `surface`, `surfaceRaised`, `surfaceOverlay` | Base + derived in `applyBackground()` (`1.05 / 1.15 / 1.25 / 1.35` lighter factors) | Same derivation. Definition supplies `background` (base); the rest are derived unless overridden. |
| Glass (derived) | `glassBackground`, `glassBorder`, `glassHighlight` | Literals, cyan-hardcoded | **Derived** from active `background` + `accent` via `withAlpha()` / `Qt.rgba()` (§2.2.4). No literals. |
| Text | `textPrimary`, `textPrimaryVariant`, `textSecondary`, `textDisabled`, `textOnAccent`, `textLink` | `readonly` literals; `textLink = accent` | Themeable per definition. `textLink` defaults to `accent`; a definition may override it. `textOnAccent` must be checked for contrast against `accent` (§11). |
| Semantic | `success`, `successDim`, `warning`, `warningDim`, `error`, `errorDim`, `info`, `recording`, `recordingPulse` | `readonly` literals; `info = accent` | Themeable per definition. `info` defaults to `accent`. `*Dim` default to `Qt.darker(base, 1.2)` when not specified. |
| Border | `border`, `borderLight`, `borderFocus` | `readonly` literals; `borderFocus = accent` | Themeable per definition. `borderFocus` defaults to `accent`. |
| Metrics | `spacing*`, `radius*`, `icon*`, `buttonHeight*`, `inputHeight`, `sidebar*`, `navRail*`, `cardTileMinimum`, `panelHeaderHeight`, `transportHeight`, `topBarHeight`, `statusBarHeight`, `duration*` | `readonly` literals | **Shared in Phase 1.** Not per-theme. Schema reserves `metrics` / `typography` / `shadows` objects for Phase 4; parsers ignore unknown sub-objects with a warning until then. |
| Typography | `fontFamily`, `fontFamilies`, `fontTiny` … `fontDisplay` | `readonly` literals | Shared in Phase 1. Same reservation as metrics. |
| Shadows | `shadowSmall/Medium/Large`, `glowAccent` | `readonly` literals; `glowAccent` cyan-hardcoded | Shadows shared in Phase 1. `glowAccent` becomes **derived** from `accent` (same fix as glass). |

Rule: **a definition supplies the base colours + the themeable
literals; derivation fills the rest.** A definition that lists every
derived value is accepted but unnecessary; a definition that lists only
`accent` + `background` is valid and gets the full derived set plus
the fallback theme's text/semantic/border values (see §6.3).

### 4.2 Definition schema (versioned, data-only)

One definition = one JSON object. Schema version `1`. Field names are
`camelCase` in JSON (QML convention), mapped to the `Theme.*` names
1:1 so the mapping is auditable.

```jsonc
{
  "schemaVersion": 1,
  "id": "gruvbox",                    // [a-z0-9][a-z0-9-]*, unique, stable
  "displayName": "Gruvbox",
  "description": "Gruvbox dark, warm retro palette.",
  "isDark": true,
  "author": "ChadVis (built-in)",       // optional
  "source": "builtin",               // "builtin" | "user" — set by loader
  "colors": {
    "accent": "#458588FF",
    "background": "#282828FF",
    // Optional overrides. Absent => derived / fallback (see §4.1).
    "surface": "#3C3836FF",
    "surfaceRaised": "#504945FF",
    "textPrimary": "#EBDBB2FF",
    "textSecondary": "#A89984FF",
    "textDisabled": "#7C6F64FF",
    "textOnAccent": "#282828FF",
    "success": "#B8BB26FF",
    "warning": "#FABD2FFF",
    "error": "#FB4934FF",
    "border": "#504945FF",
    "borderLight": "#665C54FF"
    // "textLink", "info", "borderFocus" default to accent.
    // "*Dim" default to darker(base, 1.2). "recording" defaults to #FF0000FF.
  }
  // "metrics", "typography", "shadows": reserved for Phase 4. Ignored with warning in v1.
}
```

Validation rules (§11): `id` matches the pattern and is unique;
`displayName` non-empty; `isDark` is a bool; every present colour
parses via the `#RRGGBB` / `#RRGGBBAA` rule (§3.3); unknown keys are
ignored with a warning, never an error.

### 4.3 What a theme is *not*

- Not a Qt Quick Controls style (Fusion stays pinned).
- Not a projectM preset, not a karaoke style, not an overlay style.
- Not code. A theme file never contains JS, QML, or a file path to
  execute. The loader parses JSON and assigns colours.

## 5. Built-in themes

Built-ins are definitions in the §4.2 schema, shipped as data files
(see §8 for storage), not as branches in QML `if` statements.
All hex values below are **proposed** — they are the starting point
for review, not values read from existing code, except where marked
“current”. Each built-in must pass the contrast + visual checks in
§12 before it ships.

The tables list the *definition* values. Derived values
(`accentHover`, `surface`, …) are computed by the §4.1 rules unless
a table row explicitly overrides them.

### 5.1 `material` — default (proposed, Material 3 dark baseline)

| Token | Value | Note |
| :--- | :--- | :--- |
| `accent` | `#BB86FCFF` | Material 3 dark primary (purple) |
| `background` | `#121212FF` | Material 3 dark background |
| `surface` (override) | `#1E1E1EFF` | Material 3 dark surface |
| `surfaceRaised` (override) | `#2A2A2AFF` | One step up from surface |
| `textPrimary` | `#E1E1E5FF` | Near-white, Material on-surface |
| `textSecondary` | `#9A9AA0FF` | Muted on-surface |
| `textDisabled` | `#606060FF` | Same as current disabled |
| `textOnAccent` | `#000000FF` | Black on light purple |
| `success` | `#4CAF50FF` | Material green 500 |
| `warning` | `#FFB300FF` | Keep current warning |
| `error` | `#CF6679FF` | Material 3 dark error |
| `border` | `#3A3A3AFF` | Same as current border |
| `borderLight` | `#4A4A4AFF` | Same as current borderLight |

Default choice rationale: the user asked for defaults including
material; Material 3 dark is the most widely recognised dark
baseline, and it exercises the full token set (light accent on dark
surface, coloured error, black-on-accent text) rather than a
near-monochrome theme that would hide missing tokens.

### 5.2 `gruvbox` — dark (proposed, upstream gruvbox palette)

| Token | Value | Note |
| :--- | :--- | :--- |
| `accent` | `#458588FF` | Gruvbox blue (dark) |
| `background` | `#282828FF` | Gruvbox `bg0` |
| `surface` (override) | `#3C3836FF` | Gruvbox `bg1` |
| `surfaceRaised` (override) | `#504945FF` | Gruvbox `bg2` |
| `textPrimary` | `#EBDBB2FF` | Gruvbox `fg0` |
| `textSecondary` | `#A89984FF` | Gruvbox `gray` |
| `textDisabled` | `#7C6F64FF` | Gruvbox `bg4`-ish grey |
| `textOnAccent` | `#282828FF` | Dark on blue accent |
| `success` | `#B8BB26FF` | Gruvbox green (bright) |
| `warning` | `#FABD2FFF` | Gruvbox yellow (bright) |
| `error` | `#FB4934FF` | Gruvbox red (bright) |
| `border` | `#504945FF` | Gruvbox `bg2` |
| `borderLight` | `#665C54FF` | Gruvbox `bg3` |

### 5.3 `chadvis-dark` — preserved current look (from current behaviour)

This is the migration theme. Values are the **effective runtime**
values today (config-seeded, §2.2.2), not the `Theme.qml` literals.

| Token | Value | Note |
| :--- | :--- | :--- |
| `accent` | `#00FF88FF` | Config default accent (green) — what users actually see |
| `background` | `#000000FF` | Config default background (black) — what users actually see |
| `surface` etc. | *(derived)* | Via the existing `1.15 / 1.25 / 1.35` lighter factors |
| `textPrimary` | `#E0E0E0FF` | Current `Theme.qml` literal |
| `textSecondary` | `#888888FF` | Current literal |
| `textDisabled` | `#606060FF` | Current literal |
| `textOnAccent` | `#1A1A1AFF` | Current literal |
| `success` / `warning` / `error` | `#00FF88FF` / `#FFB300FF` / `#FF4444FF` | Current literals |
| `border` / `borderLight` | `#3A3A3AFF` / `#4A4A4AFF` | Current literals |

`chadvis-dark` is the fallback for any pre-existing user whose stored
`theme` is `'dark'` (see §7): `'dark'` maps to `chadvis-dark` on load,
so nobody's look changes silently when the engine ships.

### 5.4 `nord` — dark (proposed, already advertised in `--theme` help)

| Token | Value | Note |
| :--- | :--- | :--- |
| `accent` | `#88C0D0FF` | Nord frost blue |
| `background` | `#2E3440FF` | Nord `nord0` |
| `surface` (override) | `#3B4252FF` | Nord `nord1` |
| `surfaceRaised` (override) | `#434C5EFF` | Nord `nord2` |
| `textPrimary` | `#ECEFF4FF` | Nord `nord6` |
| `textSecondary` | `#D8DEE9FF` | Nord `nord4` |
| `textDisabled` | `#4C566AFF` | Nord `nord3` (bright) |
| `textOnAccent` | `#2E3440FF` | Dark on frost |
| `success` | `#A3BE8CFF` | Nord green |
| `warning` | `#EBCB8BFF` | Nord yellow |
| `error` | `#BF616AFF` | Nord red |
| `border` | `#4C566AFF` | Nord `nord3` |
| `borderLight` | `#5E6B85FF` | One step up |

### 5.5 The “and such” rule

Any further built-in (e.g. `gruvbox-light`, `solarized-dark`,
`dracula`, `catppuccin-mocha`) is a new definition file + a row in
the Settings selector + a test that loads it. It must not require a
change to `Theme.qml`, `SettingsBridge`, `ConfigParsers`, or the
Settings window. If adding a theme requires a code change, the engine
is not finished. Light themes are deferred only because they need a
`isDark == false` visual pass (selection, focus, and disabled states
read differently on light surfaces), not because the schema cannot
hold them.

## 6. Sources, discovery, precedence, fallback

### 6.1 Sources

| Source | Location (proposed) | Lifetime |
| :--- | :--- | :--- |
| Built-ins | `resources/themes/*.json`, shipped via the Qt resource system (same `qrc` mechanism as `resources/chadvis-projectm-qt.qrc`) | Read-only, versioned with the app |
| User themes | `<configDir>/themes/*.json` — same directory that holds `config.toml` (`QStandardPaths::GenericConfigLocation` + `chadvis-projectm-qt`, see `docs/user/CONFIG.md`) | User-owned, never overwritten by upgrades |

Built-ins and user themes share one parser and one validator (§11).
A user theme whose `id` collides with a built-in id is rejected with
a warning (built-ins win; the user file is ignored, not overwritten).
A user theme whose `id` is `custom` is reserved (§7).

### 6.2 Precedence (highest wins)

1. **CLI `--theme <id>`** — session override (see §10 for the
   persist-or-not rule).
2. **Persisted `[ui] theme`** in `config.toml` — the user's last
   selection from Settings (or a hand edit).
3. **Default** — `material`.

There is no environment-variable override in Phase 1. If one is
added later it sits between CLI and config, and it must be documented
in `docs/user/CONFIG.md` in the same PR.

### 6.3 Fallback chain

`requested id` → if unknown/invalid, log a warning and use
`material` → if `material` itself fails to load (corrupt resource),
use `chadvis-dark` compiled-in literal values as a last resort →
if even that fails, keep the current `Theme.qml` literals and log an
error. At every step the window stays opaque and usable. A fallback
**never writes the fallback id back to `config.toml`**
unprompted — the user's stored selection is theirs; the next launch
retries it (it may have been a user file that was temporarily
unreadable). The only write is the explicit Settings save.

## 7. Persistence & migration

### 7.1 What is stored

`config.toml` `[ui]` gains **no new table** in Phase 1. The existing
`theme` key is repurposed from stored-but-unused to the selected
theme id:

```toml
[ui]
theme = 'material'          # was 'dark'; now a theme id (§5, §7.2)
accent_color = '#BB86FCFF'   # derived from the active theme; kept for compat
visualizer_background = '#121212FF'  # derived; kept for compat (historical key name)
```

`accent_color` / `visualizer_background` stay in the file and stay
in `CHADVIS_UI_FIELDS` (removing them would break existing installs
and the `roundTripPreservesEveryField` test). After the
reimplementation they are **derived values**, recomputed on every
theme apply — the same way `Theme.applyAccent()` already writes them
today. Hand-editing them without changing `theme` is overwritten on
the next theme apply; hand-editing them *with* `theme = 'custom'`
is the custom-theme path below.

### 7.2 Migration (first run after upgrade)

| Stored `theme` before upgrade | After upgrade |
| :--- | :--- |
| `'dark'` (the old default, or any value that was never consumed) | `chadvis-dark`, **unless** the stored accent/background differ from the old config defaults — see next row |
| `'dark'` + stored accent/background differ from `#00FF88FF` / `#000000FF` | `custom` (see §7.3), preserving the user's actual colours |
| `'gruvbox'` / `'nord'` (someone believed the CLI help) | The real `gruvbox` / `nord` theme — this is the fix for §2.2.1 |
| Any other string | Warning + `material` fallback for display; stored value untouched (§6.3) |

Migration runs once, at theme load, in memory. It does not rewrite
`config.toml` by itself — the new id is persisted on the next
Settings save, exactly like any other setting change.

### 7.3 `custom` — the preserved-colours theme

`custom` is a synthetic theme, not a file:

- Its `accent` / `background` are the stored `accent_color` /
  `visualizer_background` values.
- All other tokens come from `chadvis-dark` (§5.3).
- Selecting `custom` in Settings shows the existing accent/background
  swatches; editing them edits the `custom` theme (Phase 1 keeps the
  current swatch-only display; a colour picker is Phase 4).
- `custom` is never listed as a built-in and never written as a file.
  If the user wants a durable named theme, they save the values as a
  user theme file (§6.1) — Phase 3 may add an “Export as theme”
  button; Phase 1 does not need one.

## 8. Architecture

### 8.1 Components

| Component | File(s) (proposed) | Owns |
| :--- | :--- | :--- |
| Token owner | `src/qml/styles/Theme.qml` (existing, extended) | All `Theme.*` tokens, derivation, `themeChanged()`, `currentThemeId`, `applyThemeById(id)` |
| Definitions (data) | `resources/themes/*.json` (new) + `src/qml/styles/ThemeDefinitions.qml` **or** embedded JSON — see decision below | Built-in definitions. Pure data, no logic. |
| Catalog / loader | `src/qml_bridge/ThemeCatalog.{hpp,cpp}` (new, C++) + registration in `src/qml_bridge/BridgeRegistration.cpp` | Discovery (built-in + user dir), JSON parse, validation (§11), `availableThemes` list, `themeDefinition(id)` lookup. Named `ThemeCatalog`, **never** `Theme`. |
| Selection persistence | `SettingsBridge` (existing, +1 property) + `Config` / `ConfigParsers` (existing field, + validation) | `themeName` property, debounced save, reset-to-defaults |
| Selector UI | `src/qml/panels/settings/AppearanceSettings.qml` (existing, extended) | Theme ComboBox + preview swatches |
| Seeding | `src/qml/main.qml` `applyTheme()` (existing, extended) | Startup apply, T0047 ordering |

**Decision the implementer must confirm in the PR:** whether
built-in definitions live as JSON in the resource system (preferred —
one parser for built-ins and user files) or as a QML data file.
The preferred option is JSON in `resources/themes/`, because it
keeps one parser, one validator, and one schema for both sources.
If the resource-system plumbing proves awkward, the fallback is a
QML data file for built-ins + JSON for user files, with the C++
catalog owning validation in both cases. Either way, `Theme.qml`
never parses files itself — it asks the catalog for a resolved
definition and applies it.

### 8.2 Data flow

```
Startup:
  Config loads config.toml ([ui] theme, accent, background)
    -> Application applies CLI --theme override in memory (§10)
    -> main.qml Component.onCompleted calls applyTheme()
      -> Theme.applyThemeById(resolvedId)
        -> ThemeCatalog.themeDefinition(id)   // validated, fallback chain §6.3
        -> Theme sets every token, derives the rest, fires themeChanged()
        -> SettingsBridge.themeName reflects the applied id (not the stored id, when CLI overrode it)
        -> (no config write at startup)

Settings change:
  User picks a theme in AppearanceSettings ComboBox
    -> Theme.applyThemeById(id)               // live preview, same path as startup
    -> appearancePage.apply() on Save / Close // commits: SettingsBridge.themeName = id
    -> SettingsBridge.save()                  // debounced / explicit, existing path
    -> next launch seeds from the persisted id

Reset:
  SettingsBridge.resetToDefaults() restores themeName = 'material'
    -> appearancePage.apply() reapplies it (existing reset flow)
```

Key property: **startup, Settings preview, Settings apply, and
reset all funnel through one function** — `Theme.applyThemeById()`.
There is no second “apply theme” path in C++ or in the Settings
panel. If a second path appears in review, it is a bug.

### 8.3 What does *not* change

- `Theme.<token>` names. All 63 referencing files keep working.
- `SettingsWindow.qml` structure, page rail, save/close/reset flow.
- `Config` singleton shape, `ConfigParsers` table-driven pattern.
- `SettingsBridge` debounce (2 s) and `save()` semantics.
- Fusion style pinning, `QQuickWindow` palette bindings.
- `formatTime*()`, `lighten()` / `darken()` / `withAlpha()` helpers.

## 9. Settings window — light modifications

“Light” means: one selector + one preview row in the existing
Appearance panel, one new bridge property, no new page, no new
window, no new save path.

### 9.1 `src/qml/panels/settings/AppearanceSettings.qml` (the main change)

1. Add a **Theme** row above the existing swatches:
   - `AppComboBox` (the existing `src/qml/components/AppComboBox.qml`
     control — do not invent a new combo) with
     `model: ThemeCatalog.availableThemes` (list of
     `{ id, displayName, description }`).
   - `currentIndex` bound to the catalog entry matching
     `Theme.currentThemeId`.
   - On activation: `Theme.applyThemeById(id)` — **live preview**,
     same function as startup (§8.2). The panel does not hold its
     own copy of the selection; `Theme` is the source of truth until
     `apply()` commits it.
   - Below the combo: one line of `description` text for the selected
     theme (`color: Theme.textSecondary`, `font: Theme.fontCaption`).
2. Add a **preview row**: four small swatches showing the selected
   theme's `accent`, `background`, `surface`, and `textPrimary`
   (20×20, `radius: 4`, `border.color: Theme.border` — same swatch
   pattern the panel already uses). The preview reads from the
   *selected* definition, not from the live `Theme` tokens, so the
   user can compare before the live preview lands — or, simpler and
   acceptable: the preview reads the live tokens, since selection
   already previews live. The implementer picks one and documents
   the choice in the PR; do not implement both.
3. `apply()` changes from:
   ```qml
   Theme.applyAccent(Theme.toRgbaHex(Theme.accent))
   Theme.applyBackground(Theme.toRgbaHex(Theme.background))
   ```
   to:
   ```qml
   Theme.applyThemeById(Theme.currentThemeId)   // re-applies + commits
   SettingsBridge.themeName = Theme.currentThemeId
   ```
   The old two-line body is deleted — keeping it would re-derive
   accent/background from stale tokens and undo the theme's
   text/semantic/border values. The `toRgbaHex` round-trip stays
   inside `applyThemeById()` (see §9.3).
4. When `currentThemeId == "custom"`, the existing accent/background
   swatch rows remain visible (they are the `custom` editor in
   Phase 1). For any named theme they may stay visible as read-only
   info or be hidden — pick one, document it, keep it consistent
   with the preview row.
5. Copy: the Appearance page description (“Choose the accent and
   background palette…”, `AppearancePage.qml`) becomes “Choose the
   theme used throughout ChadVis.” — one string change in
   `AppearancePage.qml`, no layout change.

### 9.2 `src/qml/settings/AppearancePage.qml`

- Description string only (§9.1.5). No layout, no new imports.
- `apply()` stays a delegation to `appearanceSettings.apply()`.

### 9.3 `src/qml/styles/Theme.qml`

- Add `property string currentThemeId: "material"`.
- Add `function applyThemeById(id)`:
  1. Resolve via `ThemeCatalog.themeDefinition(id)` (fallback §6.3).
  2. Set `currentThemeId` to the *resolved* id (not the requested
     one, when fallback fired).
  3. Assign every themeable token from the definition (explicit
     values) or from derivation / fallback (absent values).
  4. Recompute `glass*` and `glowAccent` from the new accent.
  5. Write the derived `accentColor` / `backgroundColor` back
     through `SettingsBridge` via `toRgbaHex()` (same write-back as
     today — keeps `config.toml` compat, §7.1).
  6. Emit `themeChanged()` **once**, after all assignments, so the
     UI repaints once per theme change, not once per token.
- Keep `applyAccent()` / `applyBackground()` as thin wrappers that
  apply a single-colour delta on top of the current theme (used by
  the `custom` path and by any legacy caller). They must go through
  the same derivation + single-`themeChanged()` path — no duplicate
  derivation code.
- Change the currently-`readonly` themeable tokens (§4.1) to
  mutable `property` so `applyThemeById()` can assign them. Keep
  metrics / typography / shadows `readonly` in Phase 1.
- Delete the hardcoded cyan literals in `glass*` / `glowAccent`.
  Delete the hardcoded cyan/grey accent/background literals as
  defaults — the initial values come from the `material` definition
  via the catalog, with the `chadvis-dark` literals as the
  last-resort fallback (§6.3), not as QML literals that disagree
  with config.
- Keep `parseRgbaHex()` / `toRgbaHex()` exactly as they are. They
  are the T0047 fix; do not “simplify” them.

### 9.4 `SettingsBridge` (+1 property)

- `SettingsBridgeSettings.inc`: add
  `SETTING_STRING(themeName, ThemeName, ui().theme)`.
- `SettingsBridge.hpp`: add `Q_PROPERTY(QString themeName READ
  themeName WRITE setThemeName NOTIFY themeNameChanged)` + getter /
  setter / signal declarations.
- `SettingsBridge.cpp`: add the getter/setter (X-macro) **and** the
  emit-only stub in `resetToDefaults()` (§3.5). Reset restores
  `themeName = "material"`.
- Naming: `themeName`, not `theme` — `Theme` is taken by the QML
  singleton, and `themeName` reads unambiguously as “the selected
  theme's id” at every call site.
- `SettingsBridge` does **not** gain a theme-definition API. The
  catalog owns definitions; the bridge owns the one persisted
  selection string. Keeping those separate is what keeps the
  X-macro table small and the bridge reviewable.

### 9.5 `SettingsWindow.qml` — no structural change (verify, don't assume)

- Confirm in the PR that every colour in the file is already a
  `Theme.*` binding (it is, as of the base commit). If the audit
  finds a literal, fix it in the same PR — that is a theme bug, not
  a new feature.
- Save / close / reset flows already call `appearancePage.apply()`.
  No change needed once §9.1.3 lands. Verify with the §12 checklist.

## 10. Startup, seeding, CLI

1. `main.qml` `applyTheme()` becomes:
   ```qml
   function applyTheme() {
       Theme.applyThemeById(ThemeCatalog.resolveThemeId(
           SettingsBridge.themeName, /* cliOverride: */ AppThemeOverride))
   }
   ```
   The exact plumbing for the CLI override is the implementer's
   choice (a `SettingsBridge`-adjacent read-only property, or a
   context property set by `Application`), but the rule is fixed:
   **CLI override is a session override.** It changes what is
   displayed and what `Theme.currentThemeId` reports; it does not
   change `SettingsBridge.themeName` and is not written to
   `config.toml` unless the user opens Settings and saves. The
   current code (`applyOverride(opts.theme, CONFIG.ui().theme, …)`)
   writes the CLI value into the live `Config`, which *would* persist
   on the next `SettingsBridge.save()` — that is a trap the PR must
   close, not preserve.
2. Update the CLI help text: `"UI theme (material/gruvbox/chadvis-dark/nord)"`,
   default `"material"`. Unknown `--theme` value → warning on stderr
   + fallback chain (§6.3), exit code unchanged, window still opens.
3. `ConfigParsers::parseUI()` — validate `theme`: trim, lowercase,
   reject empty / whitespace / path separators / `..`. An invalid
   stored value is kept as stored (so the user can fix it by hand)
   but treated as unknown at resolve time (fallback, §6.3). Do not
   silently rewrite the file at load.
4. `config/default.toml` — `theme = 'material'`. The `[ui]` table
   keeps `accent_color` / `visualizer_background` with the
   `material` values (§5.1) so a fresh install's file matches the
   theme it will display.
5. `UIConfig::theme` default becomes `"material"`. The struct comment
   records that `'dark'` is a legacy alias for `chadvis-dark` (§7.2).

## 11. Validation

One validator, used for built-ins and user files alike (in
`ThemeCatalog`). It returns a resolved definition + a list of
warnings, or a failure that triggers the fallback chain.

| Check | Rule |
| :--- | :--- |
| JSON parses | If not, file rejected, warning logged with path + line. |
| `schemaVersion` | Must be `1`. Unknown version → rejected (never “best effort” on a future schema). |
| `id` | `^[a-z0-9][a-z0-9-]*$`, unique across built-ins + user files. Collision → user file ignored, warning. `custom` reserved → rejected. |
| `displayName` | Non-empty, ≤ 64 chars. |
| `isDark` | Must be a bool. Missing → default `true` + warning. |
| Colours | Each present colour parses as `#RRGGBB` or `#RRGGBBAA` via the §3.3 rule. Unparseable → whole file rejected (a half-parsed theme is worse than no theme). |
| Contrast (warning, not rejection) | `textPrimary` vs `background`, `textOnAccent` vs `accent`: WCAG contrast ratio < 4.5 → warning logged, theme still loads. The author fixes it; the loader does not second-guess it by adjusting colours. |
| Unknown keys | Ignored + warning (forward-compat for Phase 4 `metrics` / `typography`). |
| Size | File > 64 KiB → rejected (a theme is a palette, not a document). |
| No code | JSON only. Any non-JSON content is rejected by the parser. No `eval`, no QML, no file includes. |

Unit tests cover: valid minimal definition (accent + background
only), full definition, unknown id fallback, malformed JSON, bad hex,
duplicate id, reserved `custom`, unknown schema version, contrast
warning emission.

## 12. Testing & QA

### 12.1 Automated

| Test | Where (proposed) | Covers |
| :--- | :--- | :--- |
| Theme definition parse + validation | `tests/unit/qml_bridge/test_ThemeCatalog.cpp` (new) or `tests/unit/core/test_ThemeDefinition.cpp` | §11 table, one case per row |
| Theme derivation | Same file | Derived accent/background families match the §4.1 factors; glass/glow derived from accent, not cyan |
| Config round-trip | `tests/unit/core/test_ConfigLoader.cpp` — extend `roundTripPreservesEveryField` | `theme = 'gruvbox'` (and `'custom'`) survives parse → serialize → parse; `accent_color` / `visualizer_background` still round-trip |
| SettingsBridge property | `tests/unit/qml_bridge/` — extend the SettingsBridge tests | `themeName` getter/setter + `themeNameChanged` + reset-to-`material` |
| Migration | `tests/unit/core/test_ConfigLoader.cpp` or a new migration test | §7.2 table, one case per row |
| QML lint | `./scripts/lint-qml.sh` | New/changed QML passes the existing three-bucket lint (`docs/dev/QML-LINTING.md`). No new lint suppressions without a comment citing this plan. |
| Build | `./build.sh --fast --tests` then `./build.sh --fast --tests --safe` (AGENTS.md) | Green run. C++ parts get the sanitizer pass before merge (`--tsan`, `--asan`) if the PR touches `ThemeCatalog` / `ConfigParsers`. |

### 12.2 Manual QA (real GUI — a green headless run is not evidence)

Per theme (`material`, `gruvbox`, `chadvis-dark`, `nord`, one user
theme, `custom`):

1. Launch → theme applied, window opaque, no stale accent.
2. Open Settings → Appearance → selector lists all themes with
   correct display names + descriptions; selecting each repaints
   the whole shell + Settings window immediately.
3. Save → quit → relaunch → same theme. (Persistence.)
4. Reset (Settings → Reset) → `material`, saved.
5. `--theme gruvbox` → gruvbox for the session; quit without
   saving → next launch returns to the persisted theme.
   `--theme bogus` → warning + `material`, window still opens.
6. Hand-edit `config.toml` `theme = 'bogus'` → warning + `material`,
   stored value untouched.
7. Drop a malformed user theme into `<configDir>/themes/` →
   warning, theme ignored, app still launches.
8. Drop a valid user theme → appears in selector, applies, persists.
9. Contrast spot-check: body text, disabled text, accent button
   text, error text on each theme — readable, no white-on-yellow /
   black-on-black.
10. `docs/dev/manual-qa.md` (MT-001) — add a theme row; do not
    invent a second QA checklist.

## 13. Rollout — phases

Ship in order. Each phase merges only when its acceptance criteria
pass. A phase is not “done” because it compiles.

### Phase 0 — Plan + tests (this document + failing tests first)

- This plan lands on `dev/bleeding-edge`.
- Add the §12.1 tests for the schema/validation/derivation/migration
  against a stub catalog if needed; they fail until Phase 1 lands.

### Phase 1 — Theme model + built-ins + apply path

Files: `Theme.qml` (§9.3), `resources/themes/*.json` (4 files),
`ThemeCatalog` (§8.1), `main.qml` seeding (§10.1),
`ConfigParsers` validation + `UIConfig` default (§10.3/10.5),
`config/default.toml` (§10.4), CLI help (§10.2).

Acceptance: all four built-ins load; `Theme.applyThemeById()` swaps
every token with one `themeChanged()`; startup seeds correctly;
`--theme` works; unknown id falls back; tests green.

### Phase 2 — Settings selector (the “light modifications”)

Files: `AppearanceSettings.qml`, `AppearancePage.qml` (one string),
`SettingsBridge` + `.inc` (§9.4), `ThemeCatalog.availableThemes`.

Acceptance: §12.2 items 2–4 pass; save/close/reset all preserve
the theme; no new save path; no `SettingsWindow.qml` structural
change.

### Phase 3 — User-defined themes

Files: `ThemeCatalog` discovery (user dir), validation (§11),
docs (§14).

Acceptance: §12.2 items 7–8 pass; duplicate-id + malformed-file
handling verified; a user theme never overwrites a built-in.

### Phase 4 — Polish (separate PRs, not this plan's gate)

- `metrics` / `typography` / `shadows` overrides in the schema.
- Light themes (`isDark == false`) + a light visual pass.
- Colour picker / full `custom` editor in Appearance settings.
- “Export current colours as theme file” for `custom`.
- Import/export (copy a `.json` into `<configDir>/themes/` is
  already the manual path; UI import is convenience only).

## 14. File-by-file change list (implementer checklist)

| File | Change |
| :--- | :--- |
| `src/qml/styles/Theme.qml` | `currentThemeId`, `applyThemeById()`, mutable themeable tokens, derived glass/glow, thin `applyAccent()` / `applyBackground()` wrappers, delete hardcoded literals (§9.3) |
| `resources/themes/material.json` | New — §5.1 |
| `resources/themes/gruvbox.json` | New — §5.2 |
| `resources/themes/chadvis-dark.json` | New — §5.3 |
| `resources/themes/nord.json` | New — §5.4 |
| `resources/chadvis-projectm-qt.qrc` | Add the four theme files (or the `resources/themes/` prefix) |
| `src/qml_bridge/ThemeCatalog.hpp` / `.cpp` | New — discovery, parse, validate, `availableThemes`, `themeDefinition(id)` |
| `src/qml_bridge/BridgeRegistration.cpp` | Register `ThemeCatalog` (distinct name; never `Theme`) |
| `CMakeLists.txt` / QML module lists | Add new QML files (if any) to `qt_add_qml_module`; add `ThemeCatalog` sources; add `resources/themes/` to the resource build |
| `src/core/ConfigData.hpp` | `UIConfig::theme` default `"material"` + legacy-alias comment |
| `src/core/ConfigParsers.cpp` | Validate/normalise `theme` in `parseUI()`; keep `CHADVIS_UI_FIELDS` covering `theme` (no new table needed) |
| `config/default.toml` | `theme = 'material'`, accent/background = material values |
| `src/qml_bridge/SettingsBridge.hpp` / `.cpp` / `SettingsBridgeSettings.inc` | `themeName` property (§9.4) |
| `src/qml/panels/settings/AppearanceSettings.qml` | Selector + preview + new `apply()` (§9.1) |
| `src/qml/settings/AppearancePage.qml` | Description string (§9.1.5) |
| `src/qml/main.qml` | `applyTheme()` → `applyThemeById()` + CLI override plumbing (§10.1) |
| `src/core/CliArgs.inc` | Help text: `material/gruvbox/chadvis-dark/nord`, default `material` |
| `src/core/Application.cpp` | CLI override = session-only (§10.1 trap) |
| `src/qml/SettingsWindow.qml` | Verify-only; fix any literal colour found (§9.5) |
| `tests/unit/qml_bridge/test_ThemeCatalog.cpp` (new) + `tests/unit/core/test_ConfigLoader.cpp` (extend) | §12.1 |
| `docs/README.md` | Add this plan to the Development table |
| `docs/dev/ARCHITECTURE.md` | Update the “one owner of the theme” section: catalog + definitions, still one token owner |
| `docs/dev/INVARIANTS.md` | Add the §3 invariants that are not already there (seeding order, hex order, serialize-rebuild, no C++ `Theme`) — link, don't duplicate the plan |
| `docs/user/CONFIG.md` | `[ui] theme` row: now a theme id; list built-ins; `custom` rule; user-theme directory |
| `docs/user/USAGE.md` | Settings → Appearance: theme selector |
| `TODO.md` | Track the phases (state lives here, not in this plan — see `docs/backlog/README.md` contract) |

## 15. Risks / traps (read before implementing)

- **T0047 again.** The seeding-order and hex-order bugs were both
  “it worked on my machine” bugs. Any change to `applyTheme()`,
  `applyThemeById()`, or the hex helpers must be reviewed against
  §3.2–3.3 explicitly, not just tested once.
- **The serialize trap.** Forgetting a field in `CHADVIS_UI_FIELDS`
  silently drops it on save. The `roundTripPreservesEveryField`
  test exists for exactly this; extend it, don't bypass it.
- **The X-macro trap.** Forgetting the emit-only stub in
  `resetToDefaults()` produces “function definition is not allowed
  here” — one line per entry, easy to miss in review.
- **The CLI-persists trap.** `applyOverride()` writes into live
  `Config`; the next `save()` persists it. A session-only CLI
  override needs explicit plumbing (§10.1), not a comment.
- **The 63-file surface.** `Theme.<token>` names are API. Do not
  rename a token “for clarity” without a 63-file migration plan in
  the same PR.
- **The cyan-glass trap.** Any literal `0.733 / 0.831` left in the
  tree after Phase 1 is a theme bug. Grep for it.
- **The `visualizer_background` name.** It stores the UI background.
  Do not “fix” the name in this PR — renaming a config key is a
  separate migration with its own compat story.
- **Contrast is a warning, not an auto-fix.** Auto-adjusting a
  theme author's colours produces a theme nobody asked for and
  nobody can reproduce from the file. Log, don't adjust.

## 16. Open decisions (resolve in the implementing PR, not after)

1. **Built-in storage:** JSON in the resource system (preferred,
   §8.1) vs QML data file. Confirm in the PR description.
2. **Preview semantics:** preview row reads the selected definition
   vs the live tokens (§9.1.2). Pick one, document it.
3. **`custom` swatches:** visible for named themes or hidden
   (§9.1.4). Pick one, keep it consistent.
4. **CLI plumbing:** context property vs bridge read-only property
   for the session override (§10.1). Pick one.
5. **Exact Material values:** §5.1 is a proposed baseline. The PR
   author confirms the final hex values with a screenshot + contrast
   numbers, or swaps in a reviewed Material 3 token set and updates
   this plan in the same PR.
6. **Default theme:** this plan defaults to `material` per the
   request (“defaults being material, gruvbox, and such”). If review
   prefers `chadvis-dark` as the default to avoid changing anyone's
   look on upgrade, that is a one-line change (`config/default.toml`
   + `UIConfig` + reset default) — but it must be a deliberate
   decision recorded in the PR, not a drive-by.

## Appendix A — Example user theme file

`<configDir>/themes/solarized-dark.json`:

```json
{
  "schemaVersion": 1,
  "id": "solarized-dark",
  "displayName": "Solarized Dark",
  "description": "Solarized dark, etched precision.",
  "isDark": true,
  "colors": {
    "accent": "#268BD2FF",
    "background": "#002B36FF",
    "textPrimary": "#839496FF",
    "textSecondary": "#586E75FF",
    "success": "#859900FF",
    "warning": "#B58900FF",
    "error": "#DC322FFF",
    "border": "#073642FF"
  }
}
```

## Appendix B — Example `[ui]` after selecting gruvbox

```toml
[ui]
theme = 'gruvbox'
accent_color = '#458588FF'         # derived from the theme
visualizer_background = '#282828FF'  # derived from the theme
show_playlist = true
# … rest of [ui] unchanged …
```

---

*Plan written from source on `dev/bleeding-edge` (see §2 for the
verified base). Implementation starts at §13 Phase 0.*
