# BIND 9.16.50 — privately maintained Windows x64 build

## What this tree is

ISC BIND 9.16.50 (final 9.16 release, April 2024, **EOL upstream**) with security
fixes backported on top, built for Windows x64 with Visual Studio 2019. BIND 9.16
is the **last branch with Windows support** — 9.18+ cannot be built on Windows, so
this fork is maintained by backporting patches. `git log --oneline` on this repo
tells the whole story: one commit per CVE on top of the "Pristine BIND 9.16.50"
baseline commit.

Binaries land in `Build\Release\` (named.exe, dig.exe, rndc.exe, all tools + DLLs,
with dependency DLLs staged alongside — they run in place).

## Directory layout (siblings matter — Configure auto-detects them)

```
...\websites\bind-9.16.50\          <- "sibling root"
    bind-9.16.50\                   <- THIS repo (git)
    libuv-v1.44.2\                  <- built: build\Release\uv.dll + uv.lib
    openssl-1.1.1w\                 <- built in-tree: libcrypto-1_1-x64.dll etc.
    patches\                        <- all downloaded/adapted patch files + RHEL bind.spec
```

## How to build

Everything needs a VS2019 x64 environment (`vcvars64.bat`) with Strawberry Perl.
Use a batch file like this (the `NoDefaultCurrentDirectoryInExePath` line is
**mandatory** — Claude Code's shell sets it, and it breaks the `gen.exe` post-build
step with exit 9009 "'gen' is not recognized"):

```bat
@echo off
set NoDefaultCurrentDirectoryInExePath=
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Strawberry\perl\bin;C:\Strawberry\c\bin;%PATH%
cd /d <repo>\win32utils
msbuild /m /t:Build /p:Configuration=Release /p:Platform=x64 bind9.sln
```

- Full rebuild ≈ 1–2 min. Expect exactly **one** failure: `BINDInstall.vcxproj`
  (MSB8041, needs MFC which isn't installed). Everything else must succeed.
- Only re-run Configure if project files/defines change:
  `perl Configure verbose with-libxml2=no with-python=no with-tools-version=15.0
  with-platform-toolset=v142 with-platform-version=10.0.19041.0 x64`
  (from `win32utils\`, inside the VS env). Note: `with-libxml2=no` is required —
  the "auto" detection *dies* if libxml2 source isn't found at sibling root.

## Security patch status (as of 2026-07-22)

Applied (see git log): CVE-2024-1975, CVE-2024-1737 (3 patches; adds
`max-records-per-type` / `max-types-per-name` options, defaults 100), CVE-2024-4076,
CVE-2024-11187, CVE-2025-40780, CVE-2025-40778 (+ its stale-CNAME regression fix),
CVE-2026-1519, CVE-2026-5946, CVE-2026-3592 (hand-ported from ISC 9.18 commit
5abfbc266302 — not in RHEL). Everything older is already upstream in 9.16.50.

Intentionally NOT applied — re-evaluate if circumstances change:
- **CVE-2026-3039** (GSS-API TKEY leak): this build compiles **without GSSAPI**
  (`named -V` shows "TKEY mode 3 support (GSS-API): no"). If anyone ever rebuilds
  `with-gssapi`, apply `patches\bind-9.16-CVE-2026-3039.patch` first.
- **CVE-2026-11721**: requires `synth-from-dnssec yes;` — 9.16 default is `no`
  (bin/named/config.c). **Never enable synth-from-dnssec** on this build.
- CVE-2025-40776 (ECS): paid Subscription Edition only. N/A.
- CVE-2026-5950: regression introduced in 9.18.36; 9.16 never affected.
- RHEL downstream-only patches (partial/configurable-additional-records,
  resume-qmin-cname, query-fname-relative, robust-key-rollovers): compat/robustness
  tweaks, not security fixes. Reviewed and skipped 2026-07.

## Workflow: backporting a new CVE fix

1. **Read the ISC advisory** (kb.isc.org/docs/cve-XXXX-NNNN). Check the affected
   ranges: does it list 9.16.x (usually "9.11.0 → 9.16.50")? Check whether the
   vulnerable feature is even compiled/enabled here (GSSAPI is off; statistics
   channel/libxml2 is off; check config defaults in the source, not the docs).
2. **Get a 9.16-adapted patch — don't port from scratch.** Best source is RHEL 9's
   bind (9.16.23 + patches), public via CentOS Stream dist-git:
   - List: `https://gitlab.com/api/v4/projects/redhat%2Fcentos-stream%2Frpms%2Fbind/repository/tree?ref=c9s&per_page=100`
   - Raw file: `https://gitlab.com/redhat/centos-stream/rpms/bind/-/raw/c9s/<name>.patch`
   - `bind.spec` documents apply order (patch numbers) and upstream commit URLs.
   - RHEL lags occasionally (they had no CVE-2026-3592). Fallback: take the ISC
     commit from the **9.18** branch (closest cousin) and adapt by hand. Find it via
     `.../repository/compare?from=v9.18.N-1&to=v9.18.N` on gitlab.isc.org's API
     (single-commit `.patch` URLs and `/diff` API were flaky; the compare API works,
     use `--http1.1`). Security commits are titled `[9.18] [CVE-...] sec: ...`.
3. **Apply in RHEL's patch-number order** when taking several (they stack).
   `git apply --check -p1 ..\..\patches\foo.patch` first; multi-part fixes (like
   1737) fail --check until their predecessor is applied. For hand-merges use
   `git apply --reject` and fix up the `.rej` hunks (delete `.rej` files after).
4. **Windows-only follow-ups the Linux patches never include:**
   - New **public function** in libdns → add to `lib\dns\win32\libdns.def` **and**
     `libdns.def.in` (else `LNK2019` when named/libns links). Same idea for other
     libs (`lib\isc\win32\libisc.def`, etc.).
   - New **cross-DLL global variable** → do NOT rely on the `.def` (the `DATA`
     entries there sit in a dead `@IF NOLONGER` block). Decorate the *definition*
     with `LIBDNS_EXTERNAL_DATA` (or `LIBISC_...` etc.) and declare the extern the
     same way at the consumer — the macro expands to dllexport/dllimport via
     `lib/isc/win32/include/isc/platform.h`.
   - New **source file** → must be added to the `.vcxproj.in` template *and* the
     generated `.vcxproj` (or re-run Configure).
   - Test-only files (`bin/tests/system/...`, `*.py`, `.j2`) aren't built here;
     hunks touching them can be dropped if they don't apply.
5. **Commit one CVE per commit** with the patch source in the message.
6. **Verify** (all three, every time):
   - Rebuild: only BINDInstall/MFC may fail.
   - `Build\Release\named.exe -V` runs and shows 9.16.50.
   - Live resolver smoke test: minimal named.conf (listen 127.0.0.1:5300,
     `recursion yes; dnssec-validation no;`), run `named -g -c <conf>`, then
     `dig @127.0.0.1 -p 5300 isc.org A` must return NOERROR with answers.
     (In PowerShell, quote the `"@1.1.1.1"` server argument — bare `@` is a splat.)

## Gotchas that cost time once already

- Build logs redirected from PowerShell are UTF-16; `grep` sees binary. Use
  `Get-Content | Select-String` instead.
- The nmake/OpenSSL and Configure steps need **Strawberry** perl (native Windows
  paths), not Git Bash's /usr/bin/perl.
- `git apply` path args: use forward slashes in Git Bash (`../patches/x.patch`).
- Release builds are `/O2 /MD` linking `VCRUNTIME140.dll` — check with
  `dumpbin /dependents` if in doubt (debug CRT would be `VCRUNTIME140D.dll`).

## Standing caveats

- This is a de-facto private fork of an EOL branch. Watch ISC advisories
  (https://kb.isc.org/docs/aa-00913) and re-run the workflow above per advisory.
- Prefer migrating to a supported BIND (9.18+/9.20 on Linux or container) if the
  Windows constraint ever lifts; that ends this maintenance loop.
