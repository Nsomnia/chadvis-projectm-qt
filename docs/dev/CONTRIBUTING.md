# 🤝 Becoming a Chad Contributor

So you want to help make ChadVis even more elite? We welcome your PRs, but we have standards. High ones.

---

## 📜 The Code of the Chad

1.  **Modern C++**: We use **C++23**, and it is not negotiable — the toolchain floor is enforced at configure time (`cmake/Compiler.cmake:7-8`). Older compilers fail deep inside standard headers with confusing errors, so we reject them up front rather than debugging the wreckage.
2.  **Expected failures return `vc::Result<T>`**: If something can fail, it returns a `Result` rather than throwing. That is the current house style for signatures. The honest version of the rule, though, is that `catch` is *reserved for genuinely exceptional paths* — the tree currently has 14 `catch` sites, five of them bare `catch (...)`, plus third-party exception types like `toml::parse_error` and `spdlog::spdlog_ex`. Expected, recoverable, and user-facing failures go through `Result`. Note that `AGENTS.md` tracks a long-term migration from `Result<T>` toward `std::expected`, so new code should not spread the pattern further than it must.
3.  **Smart Pointers**: `std::unique_ptr` for ownership. Raw pointers for non-owning access (but check for null, you're not a cowboy).
4.  **Formatting**: Run `clang-format` before you commit. We have a `.clang-format` file. Use it.
5.  **Documentation**: If you add a feature, document it. Use the "Chad persona" if you can, but keep the technical facts straight.

### On I/O and Logging

Do not restate the logging or console-I/O policy from memory. [`AGENTS.md`](../../AGENTS.md) is the single source of truth for it — see its **C++23 Agent Guidelines** section (`AGENTS.md:337-342`), which prefers `std::println` over `std::cout` and `printf`. The tree is nearly clean on this: there is exactly one `fmt` include left in the entire codebase (`src/recorder/VideoRecorderFFmpeg.cpp:8`), and it is a leftover, not a precedent. Match `AGENTS.md`, and if you find a rule restated *differently* in another document, that is a bug worth a one-line PR.

---

## 🛠️ The Workflow

1.  **Fork & Clone**: You know the drill.
2.  **Branch**: `feature/your-awesome-thing` or `fix/that-annoying-bug`.
3.  **Build**: Ensure it builds with `./build.sh` (use `./build.sh --rebuild` only for a clean rebuild). It is the same wrapper on macOS and Linux — see the [installation guide](../user/INSTALL.md).
4.  **Test**: The test tree lives in `tests/` and is wired through CMake into `build/tests`. Run it with:

    ```bash
    ctest --test-dir build/tests --output-on-failure
    ```

    **Use `build/tests`, not `build`.** CTest treats "zero tests discovered" as success, so `ctest --test-dir build` reports a confident green while running nothing at all. Read the test count, not the exit code. If you add a test source file, add the matching `add_test()` call in `tests/unit/CMakeLists.txt` — a binary CTest never registers is the same false green wearing a different hat. Full inventory and the manual GUI checklist: [`docs/dev/TESTING.md`](TESTING.md).
5.  **PR**: Write a descriptive PR message. Say what you changed, what you verified, and what you did *not* verify. Reference the backlog item you are closing.

A PR is not complete because it compiled. It is complete when a fresh configure and build succeed, the relevant tests pass, and the logs were actually read. Stale binaries, declaration-only changes, and "it compiled last Tuesday" are not evidence.

---

## 🗣️ The Review Process

Short version, because the long version already lives in [`README.md`](../../README.md) and [`docs/lore/MANIFESTO.md`](../lore/MANIFESTO.md): review here is **direct, technical, and about the code**. Expect nitpicks on naming, `const` correctness, and whether you reached for `std::span`. None of it is personal. Fix the indentation, argue the design in the PR thread if you disagree, and don't take the tone as a review of your worth.

**Richard Stallman** will ask whether you used only free software, on a machine that respects your freedom, without reading proprietary documentation. He is not joking, but he is also not the merge gate.

---

## 🚀 Getting Started

The real backlog is the root [`TODO.md`](../../TODO.md) — read it before picking work, and mark your item honestly. There is no `.agent/TODO.md`; that path does not exist. (`.agent/` holds only a codebase-exploration prompt and a dated capture directory — nothing that is a task list.) GitHub issues are fine too, and [`AGENTS.md`](../../AGENTS.md) carries the long-form engineering backlog. We always need more shaders, better hardware accel support, and more bickering in the docs.
