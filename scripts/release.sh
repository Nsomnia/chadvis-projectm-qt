#!/usr/bin/env bash
# ===========================================================================
# release.sh -- the one command that ships executables.
#
# WHAT THIS IS FOR
#   An agent that has just landed something worth having in people's hands
#   should not have to remember a tag scheme, a workflow name, three input
#   names, and which artifact proves what. It runs this. The script refuses
#   the cases that produce a bad release, and does the waiting.
#
# WHY IT DOES NOT TAG LOCALLY
#   It dispatches release.yml with `ref=<the verified SHA>`, and the workflow
#   creates the tag at the commit it actually built and tested. A local tag
#   push would be a second source of truth, and the distance between "what I
#   tagged" and "what CI built" is exactly where a mislabelled release lives.
#
# WHAT IT REFUSES (and why each one is load-bearing)
#   * a dirty tree or a HEAD that is not on origin/main -- publishing
#     something that is not on the branch people build from;
#   * a HEAD whose `build` or `windows` gate run is not green -- "it compiled
#     on my machine" is not the claim a download page makes;
#   * an empty [Unreleased] section -- a release whose notes say nothing is a
#     release nobody can decide to install;
#   * a tag that disagrees with version.txt -- release.yml checks this too,
#     and the two checks existing in different places is deliberate: one fails
#     in a second, the other fails after a 20-minute build;
#   * a second release while one is already running.
#
# USAGE
#   ./scripts/release.sh                 # pre-release of HEAD, waits, verifies
#   ./scripts/release.sh --dry-run       # check everything, dispatch nothing
#   ./scripts/release.sh --tag v1.2.0    # a real release, marked latest
#   ./scripts/release.sh --no-wait       # dispatch and return the run URL
#   ./scripts/release.sh --verify        # re-check the last release's assets
#
# TAGS
#   v<version.txt>             real release, GitHub "Latest"
#   v<version.txt>-build.<sha> pre-release, never "Latest"  (the default)
#   The hyphen after the version is the only signal, in release.yml too.
# ===========================================================================
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

DRY_RUN=0
WAIT=1
TAG=""
REQUIRE_CHANGELOG=1
GATE_TIMEOUT=3600        # seconds to wait for a gate that is still running
RELEASE_TIMEOUT=7200     # seconds to wait for the release workflow
BRANCH="main"

die() { echo "release: $*" >&2; exit 1; }
note() { echo "release: $*"; }

usage() { sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY_RUN=1 ;;
    --no-wait) WAIT=0 ;;
    --tag) shift; [ $# -gt 0 ] || die "--tag needs a value"; TAG="$1" ;;
    --tag=*) TAG="${1#--tag=}" ;;
    --allow-empty-changelog) REQUIRE_CHANGELOG=0 ;;
    --gate-timeout) shift; GATE_TIMEOUT="${1:?}" ;;
    --release-timeout) shift; RELEASE_TIMEOUT="${1:?}" ;;
    --branch) shift; BRANCH="${1:?}" ;;
    --verify) WAIT=0; VERIFY_ONLY=1 ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option '$1' (try --help)" ;;
  esac
  shift
done
: "${VERIFY_ONLY:=0}"

command -v gh > /dev/null || die "gh (GitHub CLI) is not installed"
gh auth status > /dev/null 2>&1 || die "gh is not authenticated"

REPO="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"

# ---------------------------------------------------------------------------
# --verify: re-check an existing release's asset set without dispatching.
# ---------------------------------------------------------------------------
verify_assets() {
  local tag="$1" names
  names="$(gh release view "$tag" --json assets --jq '.assets[].name' 2>/dev/null || true)"
  if [ -z "$names" ]; then
    die "release $tag has no assets (or does not exist)"
  fi
  printf '%s\n' "$names"
  # A string, not an array: bash 3.2 (macOS /bin/bash) treats an empty array
  # as unbound under `set -u`, which would abort exactly when nothing is wrong.
  local missing=""
  printf '%s\n' "$names" | grep -q '\.tar\.gz$' || missing="$missing Linux-.tar.gz"
  printf '%s\n' "$names" | grep -q '\.dmg$'     || missing="$missing macOS-.dmg"
  printf '%s\n' "$names" | grep -qi 'windows.*\.zip$' || missing="$missing Windows-.zip"
  if [ -n "$missing" ]; then
    echo "release: $tag is missing:$missing" >&2
    echo "release: re-dispatch the same tag -- the upload is idempotent:" >&2
    echo "         ./scripts/release.sh --tag $tag --no-wait" >&2
    return 2
  fi
  note "all three platform artifacts present in $tag"
  gh release view "$tag" --json url --jq .url
}

if [ "$VERIFY_ONLY" = "1" ]; then
  if [ -z "$TAG" ]; then
    TAG="$(gh release list --limit 1 --json tagName --jq '.[0].tagName')"
    [ -n "$TAG" ] || die "no releases exist yet"
  fi
  verify_assets "$TAG"
  exit 0
fi

# ---------------------------------------------------------------------------
# Local state. Every check here is about not publishing what nobody else has.
# ---------------------------------------------------------------------------
if [ -n "$(git status --porcelain)" ]; then
  git status --short
  die "working tree is dirty. Commit or stash before shipping -- a release must be reproducible from the tag."
fi

git fetch --quiet origin "$BRANCH" --tags

SHA="$(git rev-parse HEAD)"
TIP="$(git rev-parse "origin/$BRANCH")"
if [ "$SHA" != "$TIP" ]; then
  die "HEAD ($(git rev-parse --short HEAD)) is not origin/$BRANCH ($(git rev-parse --short "$TIP")). Push first; the gate runs on what was pushed."
fi

VERSION="$(tr -d '[:space:]' < version.txt)"
[ -n "$VERSION" ] || die "version.txt is empty"

if [ -z "$TAG" ]; then
  TAG="v${VERSION}-build.$(git rev-parse --short HEAD)"
fi

case "$TAG" in
  v[0-9]*.[0-9]*.[0-9]*) ;;
  *) die "'$TAG' is not a version tag. Expected v$VERSION or v$VERSION-build.<sha>." ;;
esac
BASE="${TAG#v}"; BASE="${BASE%%-*}"
[ "$BASE" = "$VERSION" ] || die "tag $TAG implies version $BASE but version.txt says $VERSION"

if git rev-parse -q --verify "refs/tags/$TAG" > /dev/null; then
  note "tag $TAG already exists; re-dispatching uploads over it (idempotent)"
fi

# ---------------------------------------------------------------------------
# The changelog gate. A release with no notes is a release nobody installs.
# ---------------------------------------------------------------------------
if [ "$REQUIRE_CHANGELOG" = "1" ]; then
  UNRELEASED="$(awk '
    /^## \[Unreleased\]/ { f=1; next }
    /^## \[/ { f=0 }
    f { print }
  ' CHANGELOG.md | sed -e 's/^[[:space:]]*//' -e '/^$/d' | grep -v '^###' | head -1 || true)"
  [ -n "$UNRELEASED" ] || die "CHANGELOG.md [Unreleased] is empty. Write what changed, or pass --allow-empty-changelog to ship a build with no notes."
  note "changelog has unreleased entries"
fi

# ---------------------------------------------------------------------------
# The gate. This is the answer to "did the last run actually compile".
# ---------------------------------------------------------------------------
run_for_sha() {
  # $1 = workflow file. Prints "<id> <status> <conclusion>" for the newest run
  # of that workflow at HEAD, or nothing if no run exists yet.
  gh api "repos/$REPO/actions/workflows/$1/runs?head_sha=$SHA&per_page=1" \
    --jq '(.workflow_runs[0] // empty) | "\(.id) \(.status) \(.conclusion // "none")"' 2>/dev/null || true
}

wait_for_gate() {
  local wf="$1" deadline=$(( $(date +%s) + GATE_TIMEOUT )) id status conclusion
  while :; do
    read -r id status conclusion <<< "$(run_for_sha "$wf")" || true
    if [ -z "${id:-}" ]; then
      [ "$(date +%s)" -lt "$deadline" ] || die "no $wf run for $SHA after ${GATE_TIMEOUT}s. Push triggers it; if the push predates this SHA, wait or re-run the workflow."
      note "waiting for a $wf run to appear for $(git rev-parse --short HEAD)"
      sleep 20; continue
    fi
    if [ "$status" = "completed" ]; then
      [ "$conclusion" = "success" ] || die "$wf run $id for $(git rev-parse --short HEAD) concluded '$conclusion'. Fix it; a release is not a way to skip the gate. https://github.com/$REPO/actions/runs/$id"
      # An aggregate conclusion can hide a continue-on-error job; the jobs are
      # the actual answer to "did it compile".
      local bad
      bad="$(gh api "repos/$REPO/actions/runs/$id/jobs" \
        --jq '[.jobs[] | select(.conclusion != "success" and .conclusion != "skipped" and .conclusion != "neutral") | "\(.name): \(.conclusion)"] | join(", ")')"
      [ -z "$bad" ] || die "$wf run $id has failing jobs: $bad. https://github.com/$REPO/actions/runs/$id"
      note "$wf green: $(git rev-parse --short HEAD)"
      return 0
    fi
    [ "$(date +%s)" -lt "$deadline" ] || die "$wf run $id still '$status' after ${GATE_TIMEOUT}s. https://github.com/$REPO/actions/runs/$id"
    note "$wf is '$status'; waiting"
    sleep 20
  done
}

wait_for_gate build.yml
wait_for_gate windows.yml

# ---------------------------------------------------------------------------
# One release at a time. Two runs would race to publish the same tag.
# ---------------------------------------------------------------------------
BUSY="$(gh run list --workflow release.yml --limit 10 \
  --json databaseId,status,displayTitle \
  --jq '[.[] | select(.status != "completed") | "\(.databaseId) \(.status)"] | join(", ")')"
[ -z "$BUSY" ] || die "a release run is already active: $BUSY. Wait for it to finish."

if [ "$DRY_RUN" = "1" ]; then
  note "--dry-run: would dispatch release.yml"
  note "  tag: $TAG"
  note "  ref: $SHA"
  note "  prerelease: $(case "$TAG" in v[0-9]*.[0-9]*.[0-9]*-*) echo yes ;; *) echo no ;; esac)"
  exit 0
fi

# ---------------------------------------------------------------------------
# Dispatch and follow it.
# ---------------------------------------------------------------------------
STARTED="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
note "dispatching release.yml for $TAG at $(git rev-parse --short HEAD)"
gh workflow run release.yml --ref "$BRANCH" -f "tag=$TAG" -f "ref=$SHA"

RUN_ID=""
for _ in $(seq 1 30); do
  # gh --jq takes an expression, not jq's own options: `--arg since` is
  # accepted by the jq BINARY and rejected here with
  #   unknown command "since" for "gh run list"
  # -- which is how this whole follow-and-verify tail failed on its first real
  # dispatch (2026-10-25, v1.2.0-build.c4984f0). The timestamp is therefore
  # interpolated into the expression; ISO-8601 UTC strings compare correctly as
  # strings, and gh hands back exactly that format.
  RUN_ID="$(gh run list --workflow release.yml --event workflow_dispatch --limit 10 \
    --json databaseId,createdAt \
    --jq "[.[] | select(.createdAt >= \"$STARTED\")][0].databaseId // empty")"
  [ -n "$RUN_ID" ] && break
  sleep 4
done
[ -n "$RUN_ID" ] || die "dispatched, but no run appeared within 2 minutes. Check https://github.com/$REPO/actions/workflows/release.yml"

URL="https://github.com/$REPO/actions/runs/$RUN_ID"
note "release run: $URL"

if [ "$WAIT" = "0" ]; then
  note "not waiting. When it finishes: ./scripts/release.sh --verify --tag $TAG"
  exit 0
fi

if ! gh run watch "$RUN_ID" --exit-status --interval 30 > /dev/null; then
  die "release run failed: $URL"
fi

verify_assets "$TAG"
