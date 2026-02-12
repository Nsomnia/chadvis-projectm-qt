# Changelog Archive

This directory contains historical changelog entries. For current changes, see [root CHANGELOG.md](../../CHANGELOG.md).

## Archived Versions

| File | Versions | Description |
|------|----------|-------------|
| [v1.0.x.md](v1.0.x.md) | 1.0.0 - 1.0.2-RC1 | Initial release series |

## Changelog Management Policy

Following [Keep a Changelog](https://keepachangelog.com/) best practices:

1. **Root `CHANGELOG.md`**: Contains current version + last 2-3 major releases
2. **`docs/changelogs/`**: Archive older versions by major series (v1.0.x, v1.1.x, etc.)
3. **Maximum ~200 lines** in root changelog before archiving
4. **Archive trigger**: When preparing a new major/minor release, archive older entries

### When to Archive

```bash
# Example: When releasing v2.0.0, archive v1.x series
mv docs/changelogs/v1.0.x.md docs/changelogs/v1.1.x.md docs/changelogs/v1.x/
# Or consolidate: cat v1.0.x.md v1.1.x.md > v1.x.md
```
