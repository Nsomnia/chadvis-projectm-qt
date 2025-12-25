# Coding Standards
## The Chad Developer's Code Guidelines
### File Organization
- ONE class/concept per file (Junior developer anti-pattern: kitchen-sink files)
- Files should be < 200 lines ideally, < 400 max
- If you're scrolling too much, split the file
### Naming
- Classes: PascalCase
- Functions: camelCase
- Variables: camelCase
- Constants: SCREAMING_SNAKE_CASE
- Files: PascalCase.cpp/.hpp
### Comments
```cpp
/**
 * @file FileName.hpp
 * @brief One-line description
 *
 * Longer description if needed.
 *
 * AGENT NOTE: Important info for future agents
 */
```
### Error Handling
- Use exceptions for exceptional cases
- Return std::optional or Result<T> for expected failures
- Log errors before propagating
### Memory
- Prefer unique_ptr for ownership
- Prefer references for non-owning
- Raw pointers only for optional non-owning (and document why)
