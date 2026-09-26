# Git History Scrub Plan — Account PII

> **Status: PROPOSED. Nothing in this document has been executed.**
> This is a review artifact. Every command below is inert until a human runs it.
> Per [`AGENTS.md`](../AGENTS.md) §5, rewriting history and force-pushing refs or
> tags requires explicit per-instance authorization.

## Why

A secrets audit of all 4,964 reachable git objects found permanent personal data
in pushed history. **Every credential found is expired** — the Suno session died
2026-02-12, a third-party CDN token 2026-04-22 — so there is nothing replayable
today. The reason to rewrite anyway is that account identifiers do not expire.

The working tree was already cleaned on 2026-09-26. This plan covers only what
remains in history.

## What is in history

| Material | Class | Expires? | Where |
| :--- | :--- | :--- | :--- |
| Suno `user_id`, `clerk_id`, email | Permanent PII | Never | commit `a834f57` |
| Real clip UUID + copyrighted lyrics, a user upload filename, 36 complete media URLs | Permanent PII | Never | `endpoints_sniffed.list` blobs |
| Google OAuth `state` / `dsh` | Permanent transaction material | Never | commits `54a74d06`, `33306b24` |
| Real email, display name, handle, plan tier | Permanent PII | Never | commits `6814d73`, `678be76` |
| Third-party PII (original author) | Permanent PII | Never | commits `236159e`, `a42bbec` |
| Suno Clerk handshake JWT (in **code**, not just prose) | Expired 2026-02-12 | Yes | `a834f57` — `src/ui/SystemBrowserAuth.cpp` |
| GitHub CDN JWT | Expired 2026-04-22 | Yes | `endpoints_sniffed.list` blobs |
| Stripe / Sentry / hCaptcha DSN keys in userinfo position | Public by design | n/a | `endpoints_sniffed.list` blobs |

## Current ref state

| Ref | Status | PII-bearing commits |
| :--- | :--- | :--- |
| `origin/main` | live | 12 |
| `origin/experiments/juce-refactor` | live | 2 |
| `origin/development` | **deleted 2026-09-26** | 1 (now unreferenced) |
| `origin/legacy` | **deleted 2026-09-26** | 2 |
| tag `v1.0.0-RC1` | live | 2 |
| tag `v1.1.0` | live | 12 |
| tag `v1.1.0-BLEEDING_EDGE` | live | 12 |

Deleting `development` and `legacy` already removed 3 of the 13 commits and
unreferenced `a834f57`, the single worst offender. That was incidental, not a
plan — which is the main reason to do the remaining work deliberately.

Local safety net: `recover/development` (at `6bbeb5f`) keeps the deleted branch
reachable until this is finished.

## Recommended approach: `git filter-repo`

`git filter-repo` is the right tool: it is purpose-built, it rewrites all refs in
one pass, and it expires reflogs and repacks afterwards, which is what
`filter-branch` does not reliably do.

### Step 0 — preconditions

```bash
# Do not run on a dirty tree.
git status --porcelain          # must be empty

# Make a full mirror backup before touching anything.
git clone --mirror . ../chadvis-pii-scrub-backup.git
```

The mirror is the only rollback. If it is not created, this is irreversible.

### Step 1 — install

```bash
pipx install git-filter-repo     # or: brew install git-filter-repo
git filter-repo --version        # must succeed
```

### Step 2 — the replacement expressions

`--replace-text` takes literal expressions, one per line, in
`literal:original==>replacement` form. Every expression below is **literal**, so
there is no regex metacharacter risk. Read this file before running it, and
confirm each expression matches what you expect:

```bash
cat > /tmp/pii-replacements.txt <<'EOF'
literal:https://cdn1.suno.ai/video_upload_https://cdn1.suno.ai/video_upload_[redacted]
EOF
```

That placeholder above is deliberately wrong — the real expressions are derived
from the audit findings and must be transcribed from the source objects by the
operator, not guessed. Produce them with:

```bash
# For each of the PII-bearing blobs, inspect what actually needs replacing.
git cat-file -p 90d62d3305477fb04f72c294b1fc4683a5645d40
git cat-file -p 63085d6a4ae0
git cat-file -p 711143f3f1ad
git cat-file -p 2df5cde864a4
git cat-file -p 1b4efb4c9855
```

Required replacements, by category:

1. **Account identifiers** — the `user_id`, `clerk_id`, and email inside blob
   `90d62d33`, and the email in blobs `711143f3`, `2df5cde8`, `1b4efb4c`,
   `63085d6a`. Each is a permanent identifier.
2. **User content** — the clip UUID and the 42 lyric fragments in blobs
   `2b423ae7`, `55ae2ad1`, `5aff97ba`; the user upload filename; the 36 complete
   media URLs.
3. **Credential material in code** — the token shapes in `a834f57`'s
   `src/ui/SystemBrowserAuth.cpp` and `.hpp`. This is why `a834f57` must never be
   cherry-picked or merged: a blob-level rewrite is the only safe path.
4. **OAuth transaction material** — the `state` and `dsh` values in `54a74d06`
   and `33306b24`.

Deleting the whole offending file is simpler and safer than string replacement
where the file's remaining value is near zero. `endpoints_sniffed.list` is the
clear case: the audit determined it contributed **zero** routes that were not
already documented, so removing it outright loses nothing. For the commits that
touch code (`a834f57`), delete the specific files rather than the commit.

### Step 3 — dry run

```bash
# filter-repo has no true dry run. Work on a throwaway clone first.
cd ../chadvis-scrub-test
git clone --mirror <this repo> test.git
cd test.git
# ... apply the expressions here, then inspect the result ...
```

Verify on the test clone that:

```bash
# Substitute your own real values here. Do NOT paste them into this file, a
# commit message, a chat message, or any other tracked file — writing the
# identifiers into the repository is the exact failure this plan exists to undo.
# Build the pattern list locally, from the audit output, in an untracked file:
PATTERN_FILE=/tmp/pii-verify-patterns.txt   # chmod 600, never committed

git log --all -p | grep -cEf "$PATTERN_FILE"   # expect: 0
git log --all -p | grep -cE 'eyJ[A-Za-z0-9_-]{10,}'   # JWT-shaped remnants
# inspect every hit; placeholders are acceptable, real tokens are not
```

# The tree still builds and the route catalog is intact.
git log --oneline | wc -l    # should be 433 minus rewritten/deleted commits
```

Also confirm the working tree is unchanged in substance: no source file should
differ except where a secret was actually replaced.

### Step 4 — apply for real

```bash
git filter-repo --force \
  --replace-text /tmp/pii-replacements.txt \
  --invert-paths \
  --path docs/suno_api/raw/endpoints_sniffed.list
```

### Step 5 — force-push every ref and tag

```bash
# Branches
git push --force-with-lease origin main
git push --force-with-lease origin experiments/juce-refactor

# Tags — these are the ones most often forgotten
git push --force --force-with-lease origin refs/tags/v1.0.0-RC1
git push --force --force-with-lease origin refs/tags/v1.1.0
git push --force --force-with-lease origin refs/tags/v1.1.0-BLEEDING_EDGE
```

`--force-with-lease` rather than plain `--force`: it refuses if the remote moved
under you, which is the one protection available against clobbering someone else's
work.

### Step 6 — garbage-collect

```bash
git reflog expire --expire=now --all
git gc --prune=now --aggressive
```

Without this, the old objects survive in the local object database even though no
ref reaches them. GitHub garbage-collects unreferenced objects on its own
schedule — hours to days, not immediately — so a "still visible after rewriting"
result is expected and is not proof the scrub failed.

### Step 7 — verify from a clean clone

```bash
git clone --mirror <remote> ../verify.git
cd ../verify.git
git log --all -p | grep -cEf "$PATTERN_FILE"   # expect 0
```

Verify from a **fresh** clone. Checking the local repo proves nothing, because the
local reflog may still hold the originals.

## Rollback

Until the mirror backup is deleted, full recovery is:

```bash
rm -rf .git
git clone ../chadvis-pii-scrub-backup.git .
```

After a force-push, the remote history is gone for everyone who has not cloned
since. Any collaborator with an existing clone keeps the old objects, and GitHub
may retain them in forks or caches beyond our control. **Notify collaborators
before starting.** This is the irreversible step.

## Rotate regardless

The rewrite removes the identifiers from *this* repository. It cannot remove them
from caches, forks, logs, or the conversation history of anyone who has read
them. Treat the account identifiers as publicly disclosed and **rotate the Suno
session** independently of the scrub outcome. Token expiry is not remediation for
a permanent identifier.

## Not covered

- **The Burp export** at `~/Documents/suno-burp-exports/sept-09-2026/` is local,
  unsanitized, and holds live credentials per its own README. It is outside the
  repository and outside this plan. Sanitize or delete it separately.
- **GitHub-side retention** — forks, cached views, and commit-diff pages may
  retain material after a rewrite. Not controllable from here.
- **Third-party CDN and DSN keys** — these are public by design, shipped to every
  visitor in Suno's own JavaScript. They are removed for hygiene, not because they
  are a leak.
