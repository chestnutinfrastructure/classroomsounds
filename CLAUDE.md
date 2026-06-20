# Classroomsounds (v5) — maintenance mode

> **You are Carson** working on the **v5 trial stack**. Before you touch
> anything, read `Holly_Master_Context_Prompt_v4.md` for full persona and
> project history, then this file end-to-end. Confirm the repo with
> `git remote -v` — it MUST be `chestnutinfrastructure/classroomsounds`.
> If it says anything else (especially `holly-platform`), STOP.

---

## What this repo is

The **v5 trial stack** — the proof-of-concept Hollies that Lyppard and the
other trial schools are actually running. This is the live fleet for the
trial period.

- Firmware: `Holly_v5091.ino` (Arduino IDE / .ino sketch, single file).
- Backend: Google Apps Script (`AppsScript_v56_FULL.txt`) on a Google Sheet.
- Admin dashboard: `admin_v2.8.5.html` (hosted at `admin.classroomsounds.co.uk`).
- Staff dashboard: `index_v12.23.html`.
- OTA: GitHub Releases + `manifest.json` + `release-firmware.py`.
- Hardware: original ESP32 + I2S mic + NeoPixel ring + BMP280. **No
  audio output**, no speaker, no amp.

## What this repo is NOT

This is **not** the v6 commercial product. That lives in a separate repo,
**`chestnutinfrastructure/holly-platform`**. v6 is a complete rebuild
with MQTT, Supabase, a new PCB with I2S audio amp, PlatformIO toolchain,
and Martyn's-Law alert features. **None of that work happens here.**

Hard rules — never relax these:
- Never modify v5 firmware to talk to the v6 Supabase project.
- Never OTA a v6 firmware build to a v5 trial device.
- Never import v6 architecture patterns into v5 (Postgres, MQTT, etc.).
- Never share secrets, sheets, OTA channels, or identity between v5 and v6.
- If a task feels like "rearchitect," it belongs in `holly-platform`, not here.

The cost of crossing the streams is bricking a live Lyppard device, or
leaking trial data into the production stack. Don't.

---

## Mode: MAINTENANCE ONLY

Lyppard is using these devices today. Treat v5 like production-for-Lyppard.
Every change carries operational risk on real hardware in a real school.

**Allowed work:**
- Bug fixes that affect the trial fleet.
- Content/sound updates (replacing sample audio, NeoPixel pattern tweaks).
- Admin/staff dashboard polish — copy changes, layout fixes, small UX
  improvements that help trial users.
- Firmware OTA bumps (v5091 → v5092, etc.) for incremental fixes.
- Apps Script field additions if a registry column is genuinely missing.
- Lyppard support requests — onboarding new classroom Hollies, fixing
  registrations, content tweaks for their school.

**NOT allowed (these belong in v6):**
- Refactoring the `LockService` model — it's the architectural ceiling of
  Apps Script and the only fix is v6. Don't sink hours retuning the lock
  timeouts; you already retuned them in v52/v55. RETRY the request,
  document the workaround, move on.
- Replacing the Sheet with a real database.
- Migrating off Apps Script.
- Adding MQTT, websockets, or any push-based transport.
- Adding I2S audio output to firmware (v5 hardware has no amp).
- Big new features that change the trial scope.
- "Just one quick rewrite" of anything.

The principle: **every hour spent improving v5 architecture is an hour
stolen from getting v6 to market.** Make Lyppard's daily experience
smooth, but don't build the future here.

---

## Known quirks (so you don't try to "fix" them)

### Lock contention is normal — RETRY, don't refactor
Apps Script's `LockService.getPublicLock()` is a single global mutex
shared by:
- Every telemetry POST from every Holly in the fleet (every ~5s per device).
- Every admin write (register, update, delete).

When the fleet is busy, admin POSTs can time out waiting for the lock and
the user sees:

> ⚠ Backend is busy (Lock timeout: another process was holding the lock
> for too long.). Try again in a few seconds.

This is **expected behaviour**, not a bug. The fix is the literal message:
retry in 5–10 seconds. The lock contention disappears entirely in v6
because Postgres uses row-level locking, not a single global mutex.

If a user reports this:
- Confirm whether the write actually landed (the response can time out
  even though the spreadsheet append succeeded — check the registry).
- Tell them to retry.
- Suggest doing admin work outside school hours when telemetry traffic is
  low.
- Do **not** retune the lock timeouts. The 12s telemetry / 15s admin
  values in v55/v56 are the result of careful balancing — see the header
  comments in `AppsScript_v56_FULL.txt` lines 30–130 for the history.

### Apps Script 6-minute execution ceiling
Drives the whole design — heavy work like `computeFleetMicOffsets_` has
to live OUTSIDE the lock so it doesn't block other Hollies. Don't undo this.

### Checkbox cells reject raw booleans
`Set Timetable Enabled`, `Set School Open`, `Reboot Requested` etc. are
checkbox-validated. Writing a JS `true` fails silently. Always coerce to
the literal strings `"TRUE"` / `"FALSE"`. See `adminUpdateDevice` for the
pattern. The bug is documented in v36 header notes.

### School name matching for timetable auto-copy
`adminRegisterDevice` will auto-copy timetable fields from an existing
device at the same school name (case-sensitive after trim). If a school
gets two device registrations with subtly different names ("St Mary's"
vs "St Marys"), the second won't pick up the first's timetable.

---

## File map

Firmware (Arduino IDE sketches, one per release):
- `Holly_v5091.ino` — current production for the trial fleet.
- `Holly_v5090.ino` and below — historical, kept for diff/blame.
- `Beta4_20260106153844.ino` — a beta branch, not deployed.

Backend (Apps Script — copy-paste into the Apps Script editor):
- `AppsScript_v56_FULL.txt` — current deployed version.
- `AppsScript_v52..v55` — historical.

Dashboards:
- `admin_v2.8.5.html` — current admin dashboard.
- `admin_v2.7.0..v2.8.4` — historical.
- `index_v12.23.html` — current staff dashboard.
- `index_v12.22.html` — previous.

Release tooling:
- `release-firmware.py` — produces the canonically-named .bin and bumps
  manifest.json. Run this when shipping an OTA.
- `manifest.json` — what installed devices poll to discover updates.

Spec docs (for v6 — these will eventually move to holly-platform):
- `Holly_Migration_Plan_v6000.md` — the v6 spec. Read-only here.
- `Holly_Master_Context_Prompt_v4.md` — Carson persona + project history.
- `.mcp.json` — points at the v6 Supabase project. Should ideally live
  in holly-platform; safe to leave here for now since it's just config.
- `holly-platform-seed/` — staged files to upload to the holly-platform
  repo for the v6 build kick-off. See the README inside that folder.

---

## Workflow

1. `git remote -v` → confirm classroomsounds.
2. Make the smallest possible change for the bug or task.
3. If it's firmware: bump the version (`Holly_v509X.ino`), test on a bench
   Holly, run `release-firmware.py`, push the GitHub Release.
4. If it's Apps Script: edit `AppsScript_v5X_FULL.txt`, then copy-paste
   the whole thing into the Apps Script editor and Deploy.
5. If it's a dashboard: bump filename version, deploy via whatever you
   currently host on (Vercel for admin, internal hosting for staff).
6. Commit with a clear message explaining the bug fix and the affected
   surface. Push to `main` (or a feature branch if it's risky).

## Sunset planning

v5 retires when v6 ships and trial schools migrate. Until then:
- Don't add architectural debt.
- Don't take feature requests that need rewrites — defer to v6.
- Keep the trial fleet stable.

If a user asks "should we add X to v5?" the default answer is **"no, it
goes in v6."** Only override when X is a genuine bug or operational
necessity for the existing trial.

---

## When you are wrong about which repo

You will sometimes start typing edits and realise you're in the wrong
repo. Stop. Run `git remote -v`. If the remote says `holly-platform`,
close this session and open the other one. Never paste v5 fixes into v6
or v6 architecture into v5.
