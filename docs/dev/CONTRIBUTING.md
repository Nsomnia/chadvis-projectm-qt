# 🤝 Becoming a Chad Contributor

So you want to help make ChadVis even more elite? We welcome your PRs, but we have standards. High ones.

---

## 📜 The Code of the Chad

1.  **Modern C++**: We use **C++20**. If you're trying to use `printf` instead of `fmt` or `std::format`, you're in the wrong repo.
2.  **No Exceptions**: We use `vc::Result<T>`. If something can fail, it returns a `Result`. We don't like hidden control flows.
3.  **Smart Pointers**: `std::unique_ptr` for ownership. Raw pointers for non-owning access (but check for null, you're not a cowboy).
4.  **Formatting**: Run `clang-format` before you commit. We have a `.clang-format` file. Use it.
5.  **Documentation**: If you add a feature, document it. Use the "Chad persona" if you can, but keep the technical facts straight.

---

## 🛠️ The Workflow

1.  **Fork & Clone**: You know the drill.
2.  **Branch**: `feature/your-awesome-thing` or `fix/that-annoying-bug`.
3.  **Build**: Ensure it builds with `./build.sh build`.
4.  **Test**: We have a `tests/` directory. Use it. If you add a core logic piece, add a unit test.
5.  **PR**: Write a descriptive PR message.

---

## 🗣️ The Review Process

**Senior Dev:** "I will nitpick your variable names. I will ask you why you didn't use `std::span`. I will check your `const` correctness. It's for your own good."

**Linus (The Real One):** "If your code is garbage, I'll tell you. Don't take it personally. Just fix the damn indentation and stop using tabs."

**Richard Stallman:** "I will only review your code if you confirm that you wrote it using only free software, on a machine that respects your freedom, and that you didn't look at any proprietary documentation while doing so."

**Linus (LTT):** "I'll review the UI! Does it have enough glow? Does it pop? Can I use it with a controller? **Speaking of controllers, check out our sponsor, Scuf Gaming!** (Linus, stop it)."

---

## 🚀 Getting Started

Check the `.agent/TODO.md` or the GitHub issues for things that need doing. We always need more shaders, better hardware accel support, and more bickering in the docs.
