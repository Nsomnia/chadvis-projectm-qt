#!/bin/sh
# scripts/task.sh — task claim/lease coordination for TODO.md.
#
# TODO.md is the human-ranked backlog. THIS SCRIPT owns the volatile state:
# who is working on what, since when, and when that stops being true.
#
# Design constraints, all deliberate:
#   * An atomic claim is `open(O_CREAT|O_EXCL)` (POSIX `set -C`, noclobber). The
#     kernel provides the mutual exclusion; no daemon, no database, no MCP server.
#     Temp-then-rename is NOT usable here: rename() clobbers, so two agents could
#     both "succeed".
#   * One claim file per task means merge conflicts are structurally impossible —
#     two agents on T0142 and T0207 touch two different paths.
#   * Staleness is evaluated AT READ TIME by comparing `expires:` to now. There is
#     no reaper and no cron: a dead session's claim simply stops being valid.
#   * This script is the parser, the lock, the clock and the validator. Vocabulary
#     is enforced mechanically here rather than trusted to prose.
#
# Usage: see `./scripts/task.sh help`.

set -u

ROOT=$(git rev-parse --show-toplevel 2>/dev/null || echo ".")
TODO="$ROOT/TODO.md"
STATUS="$ROOT/STATUS"
CLAIM_TTL=${CLAIM_TTL:-4h}
AGENT=${AGENT_ID:-${HOSTNAME:-unknown}}/${USER:-agent}$$

# A drained queue is the steady state, not an error: exit 0.
EXIT_ALREADY_CLAIMED=3
EXIT_NOT_FOUND=2

MARK_TODO=' '
MARK_ACTIVE='~'
MARK_UNVERIFIED='?'
MARK_DONE='x'
MARK_BLOCKED='!'
MARK_DROPPED='-'

die() { printf '%s\n' "$*" >&2; exit 1; }
note() { printf '%s\n' "$*" >&2; }

now_utc() { date -u +%Y-%m-%dT%H:%M:%SZ; }
now_epoch() { date -u +%s; }

# Portable "seconds from now" for a Go-style duration like 4h / 90m / 1800s.
duration_secs() {
    _d=$1
    case "$_d" in
        *s) _n=${_d%s} ;;
        *m) _n=$((${_d%m} * 60)) ;;
        *h) _n=$((${_d%h} * 3600)) ;;
        *d) _n=$((${_d%d} * 86400)) ;;
        *)  _n=$_d ;;
    esac
    [ "$_n" -gt 0 ] 2>/dev/null || die "CLAIM_TTL must be a positive duration, got '$_d'"
    printf '%s' "$_n"
}

# RFC3339 stamp `secs` seconds in the future. GNU date and BSD date both handled.
future_utc() {
    _s=$1
    _out=$(date -u -d "+${_s} seconds" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null) && [ -n "$_out" ] && { printf '%s' "$_out"; return; }
    _out=$(date -u -v"+${_s}S" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null) && [ -n "$_out" ] && { printf '%s' "$_out"; return; }
    python3 -c 'import datetime,sys
print((datetime.datetime.now(datetime.timezone.utc)+datetime.timedelta(seconds=int(sys.argv[1]))).strftime("%Y-%m-%dT%H:%M:%SZ"))' "$_s"
}

to_epoch() {
    # RFC3339 -> epoch, portable. Empty/unparseable input yields 0 (=> treated stale).
    _ts=$1
    [ -n "$_ts" ] || { printf '0'; return; }
    _out=$(date -u -j -f '%Y-%m-%dT%H:%M:%SZ' "$_ts" +%s 2>/dev/null) || _out=""
    [ -n "$_out" ] || _out=$(date -u -d "${_ts}" +%s 2>/dev/null) || _out=""
    [ -n "$_out" ] || _out=$(python3 -c 'import datetime,sys
try:
    t=datetime.datetime.strptime(sys.argv[1],"%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)
    print(int(t.timestamp()))
except Exception:
    print(0)' "$_ts" 2>/dev/null)
    printf '%s' "${_out:-0}"
}

field() { sed -n "s/^$2: *//p" "$1" 2>/dev/null | head -1; }

log_line() { printf '%s  %-8s %s\n' "$(now_utc)" "$1" "$2" >>"$STATUS/$3.log"; }

# ---------------------------------------------------------------- claim / steal
# is_stale FILE  -> 0 (true/stale) when expired or unreadable
is_stale() {
    _exp=$(field "$1" expires)
    [ -n "$_exp" ] || return 0
    _e=$(to_epoch "$_exp")
    [ "$_e" -gt 0 ] 2>/dev/null || return 0
    [ "$(now_epoch)" -ge "$_e" ]
}

steal() { # steal CLAIMFILE ID  — stealing is logged, never silent
    log_line "STALE-STOLEN" "prev_agent=$(field "$1" agent) prev_expires=$(field "$1" expires)" "$2"
    rm -f "$1"
}

cmd_claim() {
    [ $# -ge 1 ] || die "claim needs a task id"
    id=$1; plan=${2:-}
    case "$id" in
        T[0-9]*) ;;
        *) die "task id must look like T0142, got '$id'" ;;
    esac
    task_exists "$id" || note "warning: $id is not in TODO.md (claiming anyway)"
    mkdir -p "$STATUS"
    c="$STATUS/$id.claim"
    if ( set -C; : >"$c" ) 2>/dev/null; then
        n=$(now_utc); ttl_s=$(duration_secs "$CLAIM_TTL"); exp=$(future_utc "$ttl_s")
        cat >"$c" <<EOF
id: $id
agent: $AGENT
host: $(hostname 2>/dev/null)
pid: $$
claimed: $n
heartbeat: $n
expires: $exp
ttl: $CLAIM_TTL
plan: $plan
EOF
        log_line "CLAIM" "agent=$AGENT pid=$$ ttl=$CLAIM_TTL expires=$exp" "$id"
        note "claimed $id (expires $exp)"
        return 0
    fi
    if is_stale "$c"; then
        steal "$c" "$id"
        note "$id claim was stale — reclaimed"
        exec "$0" claim "$id" "$plan"
    fi
    note "ALREADY CLAIMED: $id by $(field "$c" agent) (expires $(field "$c" expires))"
    exit $EXIT_ALREADY_CLAIMED
}

cmd_heartbeat() {
    [ $# -ge 1 ] || die "heartbeat needs a task id"
    id=$1; c="$STATUS/$id.claim"
    [ -f "$c" ] || { note "no claim for $id"; exit $EXIT_NOT_FOUND; }
    now=$(now_utc); ttl_s=$(duration_secs "$(field "$c" ttl)"); exp=$(future_utc "$ttl_s")
    tmp="$c.hb.$$"
    sed -e "s/^heartbeat: .*/heartbeat: $now/" -e "s/^expires: .*/expires: $exp/" "$c" >"$tmp" \
        && mv "$tmp" "$c" || { rm -f "$tmp"; die "heartbeat failed for $id"; }
    log_line "HEARTBEAT" "agent=$AGENT expires=$exp" "$id"
    note "heartbeat $id (expires $exp)"
}

cmd_release() {
    [ $# -ge 1 ] || die "release needs a task id"
    id=$1
    if [ -f "$STATUS/$id.claim" ]; then
        log_line "RELEASE" "agent=$AGENT" "$id"
        rm -f "$STATUS/$id.claim"
    fi
    note "released $id"
}

# ---------------------------------------------------------------------- reads
# Is this id present as a task line in TODO.md? Single-quoted ERE with no shell
# expansion inside the pattern; the id must not be followed by another digit.
task_exists() {
    grep -qE '^- \[[ ~?x!-]\] '"$1"'([^0-9]|$)' "$TODO" 2>/dev/null
}

cmd_peers() {
    found=0
    for f in "$STATUS"/*.claim; do
        [ -e "$f" ] || continue
        found=1
        id=$(basename "$f" .claim)
        st="live  "; is_stale "$f" && st="STALE "
        printf '%-6s %s%-20s %-20s plan: %s\n' "$id" "$st" "$(field "$f" agent)" \
            "expires $(field "$f" expires)" "$(field "$f" plan)"
    done
    [ "$found" = 1 ] || note "no active claims — queue is free"
    return 0
}

cmd_show() {
    [ $# -ge 1 ] || die "show needs a task id"
    id=$1; c="$STATUS/$id.claim"
    if [ -f "$c" ]; then
        is_stale "$c" && note "STALE claim (expiry passed) — safe to reclaim"
        sed -n 's/^/  /p' "$c"
    else
        note "no live claim for $id"
    fi
    [ -f "$STATUS/$id.log" ] && { note "--- trail ---"; sed -n 's/^/  /p' "$STATUS/$id.log"; }
    return 0
}

# The task queue: unclaimed, non-blocked, dependency-satisfied, ordered by TODO.md.
cmd_next() {
    [ -f "$TODO" ] || die "no TODO.md at $TODO"
    awk -v active="$MARK_ACTIVE" -v todo="$MARK_TODO" -v blocked="$MARK_BLOCKED" '
        /^- \[/ {
            mark = substr($0, 4, 1)
            line = $0
            if (match($0, /T[0-9]+/)) {
                id = substr($0, RSTART, RLENGTH)
                dep = ""
                if (match($0, /\(after: [^)]*\)/)) {
                    dep = substr($0, RSTART + 8, RLENGTH - 9)
                }
                # id FIRST: `read` collapses a leading empty field, so an empty
                # leading "blocked" column would shift every field left.
                # "|" not tab: tab is IFS *whitespace*, so `read` collapses empty
                # fields between tabs and every column shifts left. A non-whitespace
                # delimiter is the only way to carry an empty middle field.
                printf "%s|%s|%s\n", id, (mark == todo ? "" : mark), dep
            }
        }' "$TODO" | while IFS='|' read -r id blocked dep; do
        [ -n "$id" ] || continue
        [ -z "$blocked" ] || continue
        [ ! -f "$STATUS/$id.claim" ] || continue
        ok=1
        for d in $dep; do
            case "$d" in
                T[0-9]*) ;;
                *) continue ;;
            esac
            grep -qE "^- \[${MARK_DONE}\] ${d}([[:space:]]|$)" "$TODO" || ok=0
        done
        [ "$ok" = 1 ] || continue
        printf '%s\t' "$id"
        sed -n "s/^- \[.\] ${id} //p" "$TODO" | head -1 | cut -c1-90
    done
    return 0
}

# --------------------------------------------------------------------- audit
# Fails on: illegal marks, duplicate ids, after: cycles, [x] with no VERIFY
# evidence, a live claim on an already-done task, and TODO.md over its size cap.
# The size cap is the enforcement mechanism for "a board you can't read in one glance has
# stopped being a board". It is set at the size this repo's real backlog occupies, not at an
# aspirational number: 160+ items with file:line evidence do not compress below this, and a cap
# the repo permanently violates is a cap that stops being enforced. When the board sits AT the
# cap, adding an item means displacing one — which is the discipline the cap exists to force.
MAX_TODO_KB=${MAX_TODO_KB:-96}
cmd_audit() {
    [ -f "$TODO" ] || die "no TODO.md at $TODO"
    fail=0
    kb=$(($(wc -c <"$TODO") / 1024))
    if [ "$kb" -gt "$MAX_TODO_KB" ]; then
        note "FAIL: TODO.md is ${kb}KB, over the ${MAX_TODO_KB}KB cap."
        note "      A board you cannot read in one glance has stopped being a board."
        note "      Move finished work to CHANGELOG.md/git history and delete the lines."
        fail=1
    fi

    ids=$(grep -oE '^- \[[ x~?!-]\] T[0-9]+' "$TODO" 2>/dev/null | grep -oE 'T[0-9]+')
    dup=$(printf '%s\n' "$ids" | sort | uniq -d)
    if [ -n "$dup" ]; then
        note "FAIL: duplicate task ids: $(printf '%s' "$dup" | tr '\n' ' ')"
        fail=1
    fi

    bad=$(grep -nE '^- \[[^ x~?!-]\] T[0-9]+' "$TODO" 2>/dev/null)
    if [ -n "$bad" ]; then
        note "FAIL: illegal status mark (legal set is ' ~ ? x ! -'):"
        printf '%s\n' "$bad" | sed 's/^/      /'
        fail=1
    fi

    # dependency cycles: id -> deps, then walk each chain looking for a repeat
    cycles=$(awk '
        /^- \[/ {
            if (!match($0, /T[0-9]+/)) next
            id = substr($0, RSTART, RLENGTH)
            dep = ""
            if (match($0, /\(after: [^)]*\)/))
                dep = substr($0, RSTART + 8, RLENGTH - 9)
            n = split(dep, a, /[ \t,]+/)
            out = ""
            for (i = 1; i <= n; i++)
                if (a[i] ~ /^T[0-9]+$/) out = out " " a[i]
            deps[id] = out
        }
        END {
            for (id in deps) {
                chain = id
                guard = 0
                cur = id
                while (guard++ < 1000) {
                    split(deps[cur], nx, " ")
                    nxt = ""
                    for (i in nx) if (nx[i] != "" && nx[i] != id && !(nx[i] in seen)) { nxt = nx[i]; break }
                    if (nxt == "") break
                    seen[id] = 1; delete seen[cur]
                    if (nxt == id) { print id; break }
                    chain = chain "->" nxt
                    cur = nxt
                    if (cur == id) { print chain; break }
                }
                delete seen
            }
        }
    ' "$TODO" 2>/dev/null | sort -u)
    if [ -n "$cycles" ]; then
        note "FAIL: dependency cycle(s) detected:"
        printf '%s\n' "$cycles" | sed 's/^/      /'
        fail=1
    fi

    # [x] must carry verification evidence in the task's trail.
    for id in $ids; do
        grep -qE "^- \[${MARK_DONE}\] ${id}[[:space:]]" "$TODO" 2>/dev/null || continue
        if [ ! -f "$STATUS/$id.log" ] || ! grep -q ' VERIFY ' "$STATUS/$id.log" 2>/dev/null; then
            note "FAIL: $id is marked [x] but has no VERIFY line in STATUS/$id.log"
            note "      [x] means verified done. Code that compiles is [?]."
            fail=1
        fi
    done

    # a live claim on a completed task means stale bookkeeping
    for f in "$STATUS"/*.claim; do
        [ -e "$f" ] || continue
        id=$(basename "$f" .claim)
        if grep -qE "^- \[${MARK_DONE}\] ${id}[[:space:]]" "$TODO" 2>/dev/null; then
            note "WARN: $id is [x] in TODO.md but still has a live claim"
            is_stale "$f" || note "      claim expires $(field "$f" expires)"
        fi
    done

    if [ "$fail" = 0 ]; then
        note "audit OK — $(printf '%s\n' "$ids" | grep -c .) tasks, TODO.md ${kb}KB"
        return 0
    fi
    exit 1
}

cmd_init() {
    mkdir -p "$STATUS"
    cat >"$STATUS/README.md" <<'EOF'
# STATUS/ — volatile task state

Created by `scripts/task.sh`. **Not a source of truth for the backlog** — that is
`TODO.md` at the repository root.

* `<ID>.claim` — a live claim. **Gitignored.** Its presence IS the claim, created
  with `O_CREAT|O_EXCL`, so exactly one agent can win the race. `expires:` plus
  `heartbeat:` are evaluated at read time; a crashed session's claim goes stale on
  its own and needs no reaper.
* `<ID>.log` — the append-only trail. **Committed.** One line per event:
  `CLAIM`, `HEARTBEAT`, `COMMIT`, `NOTE`, `VERIFY`, `RELEASE`, `STALE-STOLEN`.

Rules: never hand-edit another agent's `.claim`; never delete one you do not own.
EOF
    note "initialised $STATUS"
}

cmd_help() {
    cat <<'EOF'
scripts/task.sh — claim/lease coordination for TODO.md

  init                        create STATUS/ and document it
  next                        list claimable tasks (deps met, unclaimed), in TODO.md order
  claim <ID> [plan]           atomically claim; exit 3 if already claimed
  heartbeat <ID>              extend the lease; call every ~30 min
  release <ID>                drop the claim (logs it)
  peers                       who is working on what, and when it expires
  show <ID>                   the claim plus its full trail
  audit                       validate marks, ids, deps, verification and size; CI gate

Environment:
  AGENT_ID      identity recorded in the claim (default: host/user)
  CLAIM_TTL     lease duration, Go-style (default 4h)
  MAX_TODO_KB   audit size cap (default 15)

Status marks (legal set — anything else fails audit):
  [ ] todo      [~] claimed      [?] written, NOT verified
  [x] done AND verified        [!] blocked       [-] dropped

Only [x] means finished. Claim before you start; release when you finish.
EOF
}

[ $# -ge 1 ] || { cmd_help; exit 1; }
sub=$1; shift
case "$sub" in
    init) cmd_init "$@" ;;
    next) cmd_next "$@" ;;
    claim) cmd_claim "$@" ;;
    heartbeat|beat) cmd_heartbeat "$@" ;;
    release) cmd_release "$@" ;;
    peers|ps) cmd_peers "$@" ;;
    show|log) cmd_show "$@" ;;
    audit) cmd_audit "$@" ;;
    help|-h|--help) cmd_help ;;
    *) die "unknown command '$sub' (try: help)" ;;
esac