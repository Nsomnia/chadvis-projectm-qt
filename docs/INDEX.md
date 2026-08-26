# 🗺️ The ChadVis Documentation Hub

Welcome to the central brain of ChadVis — the canonical table of contents for all project documentation. We've split it into bite-sized pieces so you don't get lost in a monolith of text. Pick your path based on your clearance level.

---

## 🧑‍💻 User Space
*Everything you need to get the app running and looking dope.*

*   [**Installation Guide**](user/INSTALL.md) - How to build and install (Arch BTW).
*   [**Configuration Pro-Tips**](user/CONFIG.md) - Tweak your `config.toml` like a pro.
*   [**Usage & Hotkeys**](user/USAGE.md) - Master the UI and the shortcuts.
*   [**Testing Overview**](dev/TESTING.md) - Unit test map and manual GUI checks.

---

## 🛠️ Developer Sanctum
*For those who want to see the C++23 gears turning.*

*   [**System Architecture**](dev/ARCHITECTURE.md) - The Singleton-Engine-Controller lore, plus the QML/UI layer.
*   [**Contributing Standards**](dev/CONTRIBUTING.md) - Don't use `new`, use `std::unique_ptr`.
*   [**Testing Overview**](dev/TESTING.md) - Unit/integration/manual test inventory.

---

## 🤖 Suno API Reference
*Reverse-engineered documentation of the remote Suno service — the heart of the frontend.*

*   [**API Index & Disclaimer**](suno_api/README.md) - Base URLs, auth flow, model versions.
*   [**Endpoint Inventory**](suno_api/ENDPOINT-INVENTORY.md) - Complete catalog of 150+ endpoints.
*   [**Recon Archive**](suno_api/RECON-ARCHIVE.md) - Consolidated early recon notes and probe results.
*   [**Raw Scan Data**](suno_api/raw/endpoints_sniffed.list) - Unfiltered endpoint sniff dump (see [provenance](suno_api/raw/README.md)).

---

## 📊 Integration Specs
*Technical details for the integrations.*

*   [**Integration Hub**](integration/INDEX.md) - How we talk to Suno and projectM.
*   [**Suno AI Integration**](integration/SUNO.md) - Authentication, DB, and Karaoke.
*   [**projectM v4 Bridge**](integration/PROJECTM.md) - OpenGL, Presets, and FBOs.

---

## 📜 The Sacred Lore
*The philosophy, the bickering, and the "why".*

*   [**The Manifesto**](lore/MANIFESTO.md) - Linus vs Richard vs The Senior Dev.
*   [**Project History**](lore/HISTORY.md) - From a dream to a visual powerhouse.

---

## 🔄 Project Updates
*   [**Changelog**](../CHANGELOG_CURRENT.md) - What's new and what's broken.

---

> "Documentation is like a UI: if you have to explain it, it's not good enough. But we wrote it anyway because some of you are still using Ubuntu." — *Management*
