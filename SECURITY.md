# Security policy

ChadVis is an **unofficial** Qt/C++ frontend for suno.com with no supported upstream API, and the
account credential it holds is long-lived by design (the `__client` refresh JWT is issued with a
one-year lifetime). A leak of that credential is not cosmetic, so it gets treated like a real one.

## Supported versions

| Version | Supported |
| --- | --- |
| tip of `main` | ✅ |
| newest tagged release (`1.2.x`) | ✅ |
| older tags | ❌ no backports — reproduce on the tip first |

`version.txt` is the version the current tree reports; `build/chadvis-projectm-qt --version` prints it
beside the Qt version it was built against.

## Reporting a vulnerability

**Use GitHub Security Advisories**, not a public issue:

<https://github.com/Nsomnia/chadvis-projectm-qt/security/advisories/new>

A private advisory is the only private channel this project has. Do not open an issue, a PR, or a
discussion for a security report, and do not paste a live credential anywhere public — see
[What counts as a secret](#what-counts-as-a-secret) for why that report would itself be the incident.

## What counts as a secret

This is the part worth reading before you file. A report is not allowed to contain any of it, in
plain or encoded form, and that includes a redacted-looking sample that still resolves:

- `__session` (bearer, `aud: suno-api`), `__client` / `__client_*` (one-year refresh JWT, `HttpOnly`),
  `__client_uat`, and any other `suno.com` or `auth.suno.com` cookie value.
- `Authorization:` / `Cookie:` header values, content keys, and decrypted media bytes.
- **Complete media URLs.** They are signed and account-linked; the query string *is* the credential.
  `url.path()` is the only part of a URL that may appear in a report.
- Account, clip, playlist, project and batch identifiers — the account-linked half of anything tied to
  a signed-in session.
- Raw capture corpora (HAR, Burp XML, a vendor session DB) and the browser **Safe Storage** secret or
  derived key used by the "import an existing session" path.
- Build output that contains any of the above: `--help`, `--version`, the runtime log, `--dump-config`,
  a crash backtrace with environment strings. Scrub before attaching.

The project's own rules are in [`AGENTS.md`](AGENTS.md) §Do not. They bind commits, pushes, `docs/`,
`TODO.md`, `STATUS/`, commit messages, release assets and issues. They do **not** bind a local working
transcript, which is why a credential may be visible while the maintainer works and still never reach
the repository.

## Credential handling you can verify

- Credentials live in `CredentialStore`: macOS login keychain, Windows credential manager, Linux Secret
  Service. The plaintext file backend (`Backend::File`) is gated behind `CHADVIS_NO_KEYCHAIN` and warns
  loudly.
- The config serializer is **structurally** incapable of writing `[suno] token` / `cookie` — the fields
  are not in the save path at all, so `config.toml` cannot come to contain them.
- A bearer or cookie is only ever stamped on a host in the captured allowlist
  (`src/suno/CapturedHosts.cpp`). A request that would carry one off that list fails closed.
- Redirects are never followed automatically and there is no TLS relaxation or proxy path in the tree.
- The runtime log is a durable artefact that gets attached to issues, so it logs `url.path()`, never a
  full URL with a query string.

A bug that breaks any of the last four is in scope even if it "only" logs or "only" redirects. A bug
that requires the attacker to already have the local keychain is not.

## Out of scope

- **Upstream.** Suno's service and API, projectM, FFmpeg, Qt, TagLib — report those to their projects.
- **The client being unofficial**, and the account terms that come with it. That posture is documented
  in [`README.md`](README.md), not hidden.
- **Documented gates that refuse to act** — for example the OAuth loopback lane and the library-mutation
  verbs are deliberately disabled and fail closed. A working feature that ships disabled is a feature
  request, not a vulnerability. Bypassing that gate *is* in scope.
- **Capture-derived uncertainty.** Routes and shapes marked `[LEAD]`/`[VERIFY]` in
  [`docs/suno_api/`](docs/suno_api/README.md) are known-unproven; "this undocumented endpoint might do
  something bad" needs a concrete failure in this client, not a guess about theirs.

## What to expect

One maintainer, best effort, no bounty programme. Concretely:

- Acknowledgement **within ~7 days**; if the report is going to sit longer than that, the reply says so
  rather than leaving silence.
- An assessment — affected versions, whether it is a real defect, and a rough fix window — **within
  ~30 days**. If a fix will not land in that window, the advisory stays open and says why.
- Credit in the published advisory under whatever name you ask for, or anonymously.
- If the issue is already public, say so in the report; a rushed embargo is worse than an honest one.
