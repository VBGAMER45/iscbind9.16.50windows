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

## Security patch status (as of 2026-10-19)

Applied (see git log): CVE-2024-1975, CVE-2024-1737 (3 patches; adds
`max-records-per-type` / `max-types-per-name` options, defaults 100), CVE-2024-4076,
CVE-2024-11187, CVE-2025-40780, CVE-2025-40778 (+ its stale-CNAME regression fix),
CVE-2026-1519, CVE-2026-5946, CVE-2026-3592 (hand-ported from ISC 9.18 commit
5abfbc266302 — not in RHEL), CVE-2026-11331, CVE-2026-11622, CVE-2026-13204,
CVE-2026-13321 (applied 2026-08-16 from **Debian DLA-4725-1**, bind9
`1:9.16.50-1~deb11u6` — Debian 11 "bullseye" ships exactly 9.16.50, so zero
version drift; test-only hunks dropped), CVE-2026-19666, CVE-2026-19667, CVE-2026-80274 (applied
2026-10-19 from **RHEL/CentOS Stream 9 c9s** `bind-9.16-CVE-2026-*.patch`; the `*-test.patch`
companions are saved in `patches` but not applied - unit tests aren't built here).
Also 2026-10-19: built-in root trust anchor updated for **KSK-2024** (see below).
Everything older is already upstream in 9.16.50.

## ✓ 2026-10-19: ISC Aug 2026 batch applied (CVE-2026-19666 / -19667 / -80274) + root KSK-2024 anchor

Source: RHEL/CentOS Stream 9 dist-git (`c9s`) `bind-9.16-CVE-2026-*.patch` — all three
`git apply`'d cleanly on top of the Debian-sourced 13204 fix (80274 extends the same
addnoqname/getnoqname path, so 13204 must stay first). Debian bullseye had not
published these three yet. One commit per CVE; see git log.

| CVE | Severity | Issue | Files | ISC commits |
|---|---|---|---|---|
| CVE-2026-19666 | High 7.5 | DNS64 filter64 path leaves a stale `qctx->noqname` alias → use-after-free/assert (only if `dns64` is configured) | `lib/ns/query.c` (1 line) | 9ddfd2e4, 896a4baf |
| CVE-2026-19667 | High 7.5 | negative-cache record of exactly 64 KiB wraps `dns_rdata_t.length` to 0 → every later reader asserts | `lib/dns/ncache.c` | 8b20ed0a, ca8de501 |
| CVE-2026-80274 | High 7.5 | NOQNAME proof add/get paths could pick different NSEC vs NSEC3 → crash; `dns_rdataset_addnoqname()` gains a `type` arg | `rdataset.h/.c`, `rdatalist.c/_p.h`, `resolver.c`, `validator.c/.h` | 5c9ef531, c7ee1315 |

Local addition on 80274: `event->noqnametype = dns_rdatatype_none` initialised in
`dns_validator_create()` (isc_event_allocate() doesn't zero the event). No `.def` or
`.vcxproj` changes were needed (parameter added to an already-exported function).

**Root KSK-2024 (key tag 38696) — the built-in trust anchor had gone stale.** The root
zone's DNSKEY RRset has been signed *only* by KSK-2024 since **2026-10-11**. Stock 9.16.50
`bind.keys`/`bind.keys.h` carry only KSK-2017 (20326), so a named started with
`dnssec-validation auto` and **no pre-existing `managed-keys.bind`** fails to validate the
root DNSKEY (log: `validating ./DNSKEY: no valid signature found (DS)`) and SERVFAILs
*every* query. Fixed 2026-10-19 by replacing `bind.keys` with ISC's current file from the
bind-9.18/9.20 branches (adds `. initial-ds 38696 8 2 "683D2D…2B16"`; DS checked against
https://data.iana.org/root-anchors/root-anchors.xml and recomputed with `dnssec-dsfromkey`),
regenerating `bind.keys.h` with `perl util/bindkeys.pl` (the win32 build does NOT
regenerate it — treat it as checked in), and rebuilding.
**Deployment note:** a production named that has been running with a writable
`managed-keys.bind` since before 2026-10-11 learned 38696 via RFC 5011
(`rndc managed-keys status` lists it) and keeps working on the old binary; anything
(re)started from scratch needs this rebuilt `named.exe`, or an explicit
`trust-anchors { . initial-ds 38696 8 2 "683D2D0ACB8C9B712A1948B27F741219298D0A450D612C483AF444A4C0FB2B16"; };`
in named.conf.

Verified 2026-10-19: rebuild clean (only BINDInstall/MFC), `named -V` = 9.16.50, live
smoke test with `dnssec-validation auto` from an empty directory: log shows both 20326
and 38696 trusted; isc.org A, isc.org SRV (NODATA), and NXDOMAIN names under NSEC
(isc.org) and NSEC3 (nic.cz, cz) zones all answered with the **`ad`** flag, on first
query and again from cache (exercises the new typed noqname add/get path and ncache);
unsigned google.com resolves without `ad`; no assertions logged.

## ✓ RESOLVED — the four outstanding CVEs are now patched (2026-08-16)

The four CVEs from ISC's May/June 2026 batch that were previously outstanding are
**now applied** from Debian DLA-4725-1 (see git log / patch status above). Debian
11 "bullseye" got its 9.16.50 fixes before RHEL did this cycle. Mapping (Debian
patch → our commit):

| CVE | Severity | Issue | Debian patch (in `1:9.16.50-1~deb11u6`) |
|---|---|---|---|
| CVE-2026-13321 | **High 8.6** | validation bypass via out-of-zone NSEC next field → cache poisoning | 0061 (`lib/dns/dnssec.c`) |
| CVE-2026-13204 | High 7.5 | NSEC+NSEC3 both present → validator assertion crash | 0060 (`rbtdb/rdatalist/resolver/query`) |
| CVE-2026-11622 | High 7.5 | validator memory growth under random-subdomain attack | 0055 (`lib/dns/rbtdb.c`, refcount slab headers) |
| CVE-2026-11331 | High 7.5 | RPZ wildcard-CNAME bypass/crash (only if RPZ wildcard CNAME policies used) | 0054 (`lib/ns/query.c`; Debian's 0052/0053 were test-only) |

Verified 2026-08-16: rebuild clean (only the usual BINDInstall/MFC failure),
`named -V` = 9.16.50, live smoke test resolved isc.org with the **`ad`** flag set
(DNSSEC validation exercised through the modified validator paths and still works).

Also from that batch, confirmed NOT affecting 9.16 open source: CVE-2026-10723,
CVE-2026-10822, CVE-2026-12617 (9.18.0+), CVE-2026-11605 (9.20.0+). Note re-checked
2026-08-16 against ISC KB ranges — bullseye's series *does* carry patches for
CVE-2026-5950 (0041/0042), CVE-2026-10723 (0059) and CVE-2026-11721 (0056–0058) on
9.16.50, but ISC's affected ranges exclude 9.16 for 5950 (≥9.18.36) and 10723
(≥9.18.0); 11721's range includes 9.16 but is gated on `synth-from-dnssec yes`
(off by default here, never enable). So those are Debian keeping the branch
code-aligned / defense-in-depth, not real gaps for us. Skipped deliberately.

CVE-2025-40777 (serve-stale assertion): checked 2026-07-23 — **N/A to 9.16**. ISC
range is 9.20.0+ (needs `stale-answer-client-timeout 0`, code not in 9.16). Debian
bullseye shows "vulnerable/postponed" but that's their default for an unassessed
EOL suite, not a real 9.16 exposure.

## Automated monitoring (set up 2026-07-23)

A scheduled cloud routine "BIND 9.16 security patch monitor" (id
`trig_01TSjuSztvaC3aoa7NvoFX7i`, https://claude.ai/code/routines/trig_01TSjuSztvaC3aoa7NvoFX7i)
runs **weekly, Mondays 14:00 UTC / 09:00 America/Chicago**. It checks both trackers
below for new 9.16-applicable patches and, when it finds something actionable,
drafts (does not send — Gmail connector can only draft) a summary email to
jonathan@sourceitmarketing.com. Every run's full report is in the routine's
dashboard log regardless. It carries the same applied/outstanding/N-A baseline as
this file; if you patch things here, update the routine's prompt too (via /schedule
or the dashboard) so it doesn't re-flag them.

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
2. **Get a 9.16-adapted patch — don't port from scratch.** Two downstream sources
   still maintain a 9.16.x branch; check both (a routine does this weekly — see
   "Automated monitoring" above):
   - **RHEL 9 / CentOS Stream 9** (bind 9.16.23 + patches):
     - List: `https://gitlab.com/api/v4/projects/redhat%2Fcentos-stream%2Frpms%2Fbind/repository/tree?ref=c9s&per_page=100`
     - Raw file: `https://gitlab.com/redhat/centos-stream/rpms/bind/-/raw/c9s/<name>.patch`
     - `bind.spec` documents apply order (patch numbers) and upstream commit URLs.
   - **Debian** — tracker: https://security-tracker.debian.org/tracker/source-package/bind9
     (JSON: https://security-tracker.debian.org/tracker/data/json). Debian 11
     **"bullseye" ships EXACTLY 9.16.50** — our version — so when bullseye shows a CVE
     "fixed", their patch applies to our tree with zero version drift (better than
     RHEL's 9.16.23 base). Get patches from the Debian source package
     (`debian/patches/`) once bullseye is fixed.
   - RHEL/Debian both lag occasionally (neither had CVE-2026-3592 in July 2026).
     Fallback: take the ISC fix commit from the closest still-maintained branch
     (**9.18** was EOL'd mid-2026; use **9.20** for the 2026 batch) and adapt by hand.
     Find it via `.../repository/compare?from=vX.Y.N-1&to=vX.Y.N` on gitlab.isc.org's
     API (single-commit `.patch` URLs and `/diff` API were flaky; the compare API
     works, use `--http1.1`). Security commits are titled `[CVE-...] sec: ...`.
     NOTE: 9.20 split 9.16's single `lib/dns/rbtdb.c` into qpcache/qpzone/rbt-cachedb
     — take the rbtdb.c hunks and translate the rest.
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
     Then a **validating** run too (`dnssec-validation auto;`, `directory` pointing at an
     empty dir so there is no `managed-keys.bind`): `dig … isc.org A +dnssec` must carry
     the `ad` flag and the log must say DNSKEY ID 38696 is trusted — proves the built-in
     trust anchor is still current and the validator paths still work.

## Gotchas that cost time once already

- Build logs redirected from PowerShell are UTF-16; `grep` sees binary. Use
  `Get-Content | Select-String` instead.
- The nmake/OpenSSL and Configure steps need **Strawberry** perl (native Windows
  paths), not Git Bash's /usr/bin/perl.
- `git apply` path args: use forward slashes in Git Bash (`../patches/x.patch`).
- Release builds are `/O2 /MD` linking `VCRUNTIME140.dll` — check with
  `dumpbin /dependents` if in doubt (debug CRT would be `VCRUNTIME140D.dll`).
- Running the build `.bat` from Git Bash: `cmd //c build.bat` fails with "'build.bat' is
  not recognized" (same `NoDefaultCurrentDirectoryInExePath` cause) — pass the absolute
  Windows path: `cmd //c "$(cygpath -w /path/to/build.bat)"`.
- `lib/dns/gen.exe` is tracked (baseline commit) but every build rewrites it — run
  `git checkout -- lib/dns/gen.exe` before committing; never commit it.
- `dnssec-dsfromkey -f file .` needs a TTL on the DNSKEY line (`. 3600 IN DNSKEY …`).

## Standing caveats

- This is a de-facto private fork of an EOL branch. Watch ISC advisories
  (https://kb.isc.org/docs/aa-00913) and re-run the workflow above per advisory.
- Prefer migrating to a supported BIND (9.18+/9.20 on Linux or container) if the
  Windows constraint ever lifts; that ends this maintenance loop.
