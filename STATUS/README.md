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
