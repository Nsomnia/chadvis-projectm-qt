# AGENTS.md - chadvis
> **Version:** 1.0
> **Last Updated:** 2026-03-27


### Build Commands (may take several minutes):
```bash
bash build.sh build

# Run tests
./build/tests/unit/unit_tests
./build/tests/integration/integration_tests
```

### Documentation and standards

**C++23** | std::ranges::to, std::is_constant_evaluated, std::bit_cast, std::from_chars (float), std::span::alignas, std::jthread, improved std::expected, new <expected>, expanded pattern matching support,
 shortened constexpr features, many deprecations, and more. |
**Modern most recent version of C++ (c++23):** If you find anything that can be improved using modern standards then refactor it when found.

**File headers (standard linux kernel style):**
```cpp
/**
 * @file FileName.hpp
 * @brief One-line description
 *
 * Detailed description if needed. Mention patterns used.
 *
 * @author Name (optional)
 * @version X.X.X
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

Keep all files versioned with a date-time stamp and when editing more than a small amount of tokens, increment them. 

---

## Securtiy

**Always ensure no secrets are commited!**

### Never Commit Secrets
- Use `.gitignore` for local config
- Use environment variables for runtime secrets
- Never log sensitive data (even in debug mode)

---

## File Deletion / Destructive Operations Protocol

### NEVER use `rm`, `rm -rf`, or any destructive deletion commands

**Instead, always move files to `.backup_graveyard/` with a datetime stamp:**

```bash
# Create backup graveyard if it doesn't exist
mkdir -p .backup_graveyard

# For files and directories: Append timestamp to filename
mv --no-clobber "/path/to/file.txt" ".backup_graveyard/file.txt.$(date +%Y%m%d_%H%M%S)"

# Example with variable paths
mv --no-clobber "${SOURCE_PATH}" ".backup_graveyard/$(basename "${SOURCE_PATH}").$(date +%Y%m%d_%H%M%S)"
```
### The ONLY Exception
Temporary build artifacts in `build/`, `dist/`, or similar temporary directories may be cleaned with standard commands, but ONLY when the agent is 100% sure.

---

## Ralph-loops and freedoms
- You may continue working on any and all TODO items either directly listed or inferred from documents in any file in .agent/ or AGENTS.md, so long as the user is not needed. If the user needs to make i.e a visual inspection or do a sanity check that their ideas are coming together in the vision they have , then you may stop output and inform them of the next step.
- Keep the CHANGELOG.md updated. If its gets unmanagable in length then move it into `docs/` with a time-date string appended to it.

*"Maximum effort. No compromises. Ship it like it's Arch Linux."* 🚀
