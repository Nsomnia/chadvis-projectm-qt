# AGENTS.md - Guide for AI Agents

> **For:** AI agents working on chadvis-projectm-qt  
> **Last Updated:** 2026-03-09

Welcome, agent. This document contains essential info for working effectively with this codebase.

---

## 🚀 Quick Start

### Build Commands
Very slow on the users system. You can wait until your done your loop/work and then the user will compile and report bugs. For lesser changes, or if needed to finish tasks, you are free to run the build.sh script or the commands it does piecemeal (optionally with a build system/accelerator).
```bash
bash build.sh --help
bash build.sh build
```
OR manually
```bash
# Debug build (for development)
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j$(nproc)

# Run tests
./build/tests/*
```

### Project Structure

**Modifying;**
- You are free to reorganize, add, remove, or otherwise change, the layout for optimal agentic workflows.

---

## 📝 Development Guidelines

### Code Style

**C++20 or 23 Features:**
- Use `std::optional`, `std::expected` (or our `Result<>` type)
- Use structured bindings: `auto [x, y] = getPoint();`
- Use designated initializers where appropriate
- Prefer `std::string_view` over `const std::string&` for parameters
- File and class names should be self explaining by name alone to aide in agentic codebase navigation.

**Naming Conventions:**
```cpp
// Classes: PascalCase
class AudioEngine { };

// Functions: camelCase
void processAudioBuffer();

// Member variables: trailing underscore
class Foo {
    int privateMember_;
};

// Constants/Enums: UPPER_SNAKE or kCamelCase
constexpr int MAX_BUFFER_SIZE = 4096;
enum class Color { Red, Green, Blue };

// Private impl: trailing underscore with Impl
class MyClass {
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

### Error Handling

**Use the Result<T> type for fallible operations:**
```cpp
// Good
Result<Data> loadData(const fs::path& path) {
    if (!fs::exists(path)) {
        return Result<Data>::err("File not found: " + path.string());
    }
    // ... load data
    return Result<Data>::ok(data);
}

// Usage
auto result = loadData("file.txt");
if (!result) {
    LOG_ERROR("Failed to load: {}", result.error().message);
    return;
}
auto data = std::move(*result);
```

**Never use raw exceptions for expected failures.** Only use exceptions for truly exceptional conditions (programmer errors, OOM, etc).

### Documentation

**File headers:**
```cpp
/**
 * @file FileName.hpp
 * @brief One-line description
 *
 * Detailed description if needed. Mention patterns used.
 *
 * @author Agent + Model (Optional)
 * @version X.X.X (Optional Date) INCREMENT ON EVRRY EDIT OF HEADER!
 */
```

**Function documentation:**
```cpp
/**
 * @brief Brief description
 * @param param1 Description
 * @param param2 Description
 * @return Description of return value
 * @throws Never (if applicable)
 */
```

---

## 🔄 Git Workflow

### Branch Naming
```
feat/short-description        # New features
fix/short-description         # Bug fixes  
refactor/short-description    # Code restructuring
docs/short-description        # Documentation
chore/short-description       # Maintenance
test/short-description        # Test changes
```

- When done your work for the session, merge any working branches you've created back with the branch you started on.

### Commit Messages
```
type(scope): concise description

- Additional context if needed
- Can span multiple lines
- Reference issues: fixes #123

Types: feat, fix, docs, style, refactor, test, chore.

Be as verbose as needed such that this in itself becomes the most detailed changelog held in the git server cloud and as a form of documentation.
```

---

## 🛡️ Security Protocols

### Sensitive Data Protection

**ALWAYS create backups before editing sensitive files:**
```bash
# Required before editing any of these:
# - Config files with credentials
# - API keys, tokens, secrets
# - OAuth files
# - .env files

cp --no-clobber "/path/to/sensitive-file" "/path/to/sensitive-file.backup.$(date +%Y%m%d_%H%M%S)"
```

**Sensitive locations:**
- Any files containing `token`, `secret`, `password`, `api_key`, or similar forms of this data types.

### Never Commit Secrets
- Use `.gitignore` for local config
- Use environment variables for runtime secrets
- Never log sensitive data to files that are git controlled.

---

## 🐛 Debugging

### Debug Logging
```cpp
LOG_TRACE("Very detailed");  // Function entry/exit
LOG_DEBUG("Debug info");     // State changes
LOG_INFO("Normal events");   // User actions
LOG_WARN("Recoverable issues");
LOG_ERROR("Critical errors");
```

---

## 🧪 Testing

### Unit Tests
```cpp
// tests/unit/test_something.cpp
TEST_CASE("Feature description") {
    REQUIRE(condition);
    REQUIRE_EQ(expected, actual);
}
```

---

## 📚 Resources

### Documentation Files
- `docs/ARCHITECTURE.md` - System design
- `docs/VIDEO_RECORDING.md` - Recording subsystem
- `docs/SUNO_INTEGRATION.md` - Suno API details
- `docs/CPM_INTEGRATION.md` - Dependency management
- `docs/CHANGELOG.md` - Version history

---

## 💡 Tips for Agents

1. **Read before writing** - Check existing patterns in similar files (opencode forces this)
2. **Build frequently** - C++ compile errors are easier to fix immediately and this allows agents to compile in reasonable timeframes.
3. **Test edge cases** - Empty files, invalid paths, network failures
4. **Document as you go** - Write nestsd file and directory structure for all I lortwnr logic specific to agentic work in the future or the end user.
5. **Keep classes specific** - No monoliths. Single responsibility principles. Nested heavily directory structure with many files is free. Wrangling monoliths is not for agents.
6. **Keep functions small** - Single responsibility principle
7. **Use the Result<T> type** - Don't ignore error cases
8. **Check for existing utilities** - Look in `util/` before writing new code. You are also free to search the web for existing solutions or libraries as well as `gh search repos --sort stars SIMPLE SEARCH TERM`.
9. **keep a TODO.md file updated** - .agent/TODO.md should only be cleared by the user and be a full Todo list of current and future needs of your own findings or the users additions. Refactor freely so long as no actual steps are removed.

---

## 🆘 Emergency Contacts


**Never:**
- Commit to `main` or `master` directly
- Push secrets (even if "just for testing")
- Delete `.agent/TODO.md` entries (mark complete instead)

---

## File Deletion / Destructive Operations Protocol

### NEVER use `rm`, `rm -rf`, or any destructive deletion commands

**Instead, always move files to `.backup_graveyard/` with a datetime stamp:**

```bash
# Create backup graveyard if it doesn't exist
mkdir -p .backup_graveyard

# For files: Append timestamp to filename
mv --no-clobber "/path/to/file.txt" ".backup_graveyard/file.txt.$(date +%Y%m%d_%H%M%S)"

# For directories: Append timestamp to directory name
mv --no-clobber "/path/to/directory" ".backup_graveyard/directory.$(date +%Y%m%d_%H%M%S)"

# Example with variable paths
mv --no-clobber "${SOURCE_PATH}" ".backup_graveyard/$(basename "${SOURCE_PATH}").$(date +%Y%m%d_%H%M%S)"

# Commit as backup 
git add .graveyard_backup/
```

### Applies To
- File deletion (`rm`, `unlink`)
- Directory removal (`rm -rf`, `rmdir`)
- Overwriting files without backup
- Any destructive filesystem operations

### The ONLY Exception
Temporary build artifacts in `build/`, `dist/`, or similar temporary directories may be cleaned with standard commands, but ONLY within those designated temporary directories.

---

## Ralph-loops and freedoms
- You may continue working on any and all TODO items either directly listed or inferred from documents in any file in .agent/ or AGENTS.md
- Keep the CHANGELOG.md updated. When too long move to a versioned history file within docs/ and start a new one.

*"Maximum effort. No compromises. Ship it like it's Arch Linux."* 🚀
