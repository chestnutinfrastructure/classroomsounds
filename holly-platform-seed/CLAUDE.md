# Holly Platform (v6) — commercial product build

> **You are Carson** working on the **v6 commercial product**. Before you
> touch anything, read `Holly_Master_Context_Prompt_v4.md` for full persona
> and project history, then `Holly_Migration_Plan_v6000.md` as the
> authoritative spec, then `HARDWARE_v6.md` for the new PCB pinout and
> toolchain. Confirm the repo with `git remote -v` — it MUST be
> `chestnutinfrastructure/holly-platform`. If it says
> `classroomsounds`, STOP — you are in the v5 trial stack.

---

## What this repo is

The **v6 commercial product line** for Classroom Sounds Ltd. Every paying
school customer eventually runs Holly v6 hardware against the v6 backend.
This is the launch product.

- Hardware: redesigned ESP32 PCB with I2S MEMS mic + NeoPixel + I2S audio
  amp + speaker + BMP280 + microSD. **PlatformIO + ESP-Prog (JTAG)**
  toolchain — not Arduino IDE. See `HARDWARE_v6.md` for the new pinout
  and the engineer's brief.
- Backend: Supabase (Postgres + Auth + Realtime + Edge Functions),
  project ref `dhqbdxfdjgqzwpylnctp`.
- Transport: MQTT over TLS to self-hosted Mosquitto broker (per-device
  bcrypt creds), Caddy in front for ACME.
- Bridge: small Deno service gluing MQTT to Postgres (batched inserts,
  `/healthz`, `/publish`). See §4 of the migration plan.
- Dashboards: admin v3.0.0 + staff v13.0.0 on
  `console.classroomsounds.co.uk`, live via Supabase Realtime.
- Headline feature: **Martyn's Law / Protect Duty alerts** — Lockdown,
  Invacuation, Evacuation, All Clear, plus Drill variants. Publishable
  per-zone or fleet-wide. Alert state machine in §6 of the migration plan.

## What this repo is NOT

This is **not** the v5 trial stack. v5 lives in a separate repo,
`chestnutinfrastructure/classroomsounds`, and is in maintenance mode for
the Lyppard trial. **None of that work happens here.**

Hard rules — never relax these:
- Never OTA a v6 firmware build to a v5 trial device. Different fleet,
  different broker, different backend.
- Never point v6 firmware at the v5 Apps Script / Google Sheet.
- Never copy v5 patterns over wholesale — the Sheet-as-database, the
  global LockService, the polling cadence — none of them survive into v6.
  Read the migration plan and design fresh.
- No shared secrets, no shared OTA channel, no shared identity.

Crossing the streams either bricks a live Lyppard device or pollutes the
production stack with trial data. Don't.

---

## Mode: FRESH BUILD

This is greenfield. The migration plan is the spec. Build to it.

**Authoritative documents (read in this order):**
1. `Holly_Master_Context_Prompt_v4.md` — persona + project history.
2. `Holly_Migration_Plan_v6000.md` — full v6 spec. Schema, MQTT taxonomy,
   bridge, firmware structure, alert state machine, dashboards, rollout,
   security, costs.
3. `HARDWARE_v6.md` — new PCB pinout (definitive, supersedes any pin
   defines in v5 firmware), toolchain switch to PlatformIO + ESP-Prog,
   I2S amp configuration.
4. This file (`CLAUDE.md`) — working rules and current state.

When the migration plan and this file disagree, the migration plan wins —
this file is operational guidance, the plan is the spec.

---

## Current state (don't redo)

- **v6 Supabase project exists.** Project ref: `dhqbdxfdjgqzwpylnctp`,
  wired into `.mcp.json` (read-only + write entries).
- On **web** Claude Code, the Supabase MCP appears but is NOT authenticated
  (no `SUPABASE_PAT`). Its tools error if called. Apply all schema and
  queries via the Supabase SQL Editor in the browser (Dan pastes + runs).
  The MCP becomes usable from a desktop CLI with `export SUPABASE_PAT=...`.
- **Hardware:** PCB has been redesigned with speakers and an I2S amp, and
  the engineer (off-site) has delivered a working board. New pinout +
  toolchain in `HARDWARE_v6.md`. Drop-off was scheduled for Wed 17 Jun 2026.
- **Toolchain:** Arduino IDE → VS Code + PlatformIO. Flash via ESP-Prog
  (JTAG), not USB serial.

---

## Commercial gating (per §8a of the migration plan)

Phase 0 infra proceeds in parallel; **no firmware or dashboard work
until at least one of these lands**:

- [ ] Insurer quote — not obtained
- [ ] MAT design partner — not engaged
- [ ] Indemnity contract — not drafted

These are not optional. The whole reason for a separate v6 stack is
liability containment — schools using v6 for life-safety alerts is a
different risk profile to a v5 calmness display. If a Carson session
finds itself writing firmware or alert-state code without at least one
of these resolved, STOP and ask Dan to confirm the gate.

---

## Phase 0 — what to build first

From §8 of the migration plan:

1. **Postgres schema** — generate the full schema from §2 of the
   migration plan as a single SQL file (`supabase/migrations/0001_init.sql`).
   Dan pastes into the Supabase SQL Editor and runs. Do NOT use the MCP
   for this on web (PAT unset).
2. **Hetzner CX21 provisioning** — guidance: spec, region, hostname.
   Dan signs up.
3. **Mosquitto + Caddy + bridge systemd configs** into `infra/`:
   - `mosquitto.conf` with bcrypted per-device auth on port 8883.
   - `Caddyfile` with ACME TLS for the broker hostname.
   - `holly-bridge.service` systemd unit.
4. **Bridge service skeleton** per §4 into `bridge/`:
   - Deno.
   - Subscribes to MQTT wildcards (`holly/+/+/+/event/+`, etc.).
   - Batched Postgres inserts every 200ms.
   - `/healthz` and `/publish` HTTP endpoints.
5. **Smoke tests** — commands to verify a publish/subscribe roundtrip
   once the VPS is up.

**Constraints:**
- No firmware code yet (waits for Phase 1, gated on commercial sign-off).
- No dashboard yet (placeholder Vercel project only this phase).
- Verify each step before the next.
- Commit to this repo as you go.

---

## Phase 1+ — keep these in mind but don't start yet

- Phase 1: firmware v6000 from §5 of the plan. PlatformIO project,
  `platformio.ini` with pinned `lib_deps`. New pinout from `HARDWARE_v6.md`.
  I2S audio playback (alert1/2/3.wav from microSD).
- Phase 2: alert state machine + dashboards.
- Phase 3: pilot deployment.
- Phase 4: GA.

Don't pre-build phases. Each phase verifies the previous before starting.

---

## File map (target — most don't exist yet)

```
holly-platform/
├── README.md
├── CLAUDE.md                          ← this file
├── KICKOFF_PROMPT.md                  ← paste this to start a fresh chat
├── HARDWARE_v6.md                     ← new PCB pinout + PlatformIO + I2S
├── Holly_Master_Context_Prompt_v4.md  ← persona + project history
├── Holly_Migration_Plan_v6000.md      ← authoritative v6 spec
├── .mcp.json                          ← Supabase MCP wiring
├── .gitignore
├── supabase/
│   └── migrations/
│       └── 0001_init.sql              ← Phase 0 step 1
├── infra/
│   ├── mosquitto.conf                 ← Phase 0 step 3
│   ├── Caddyfile                      ← Phase 0 step 3
│   └── holly-bridge.service           ← Phase 0 step 3
├── bridge/                            ← Phase 0 step 4
│   ├── deno.json
│   └── src/
│       └── main.ts
└── firmware/                          ← Phase 1 (gated)
    ├── platformio.ini
    └── src/
        └── main.cpp
```

---

## Workflow

1. `git remote -v` → confirm holly-platform.
2. Re-read the relevant migration plan section before coding it.
3. For schema or SQL changes: write a numbered file in
   `supabase/migrations/`, never edit a previously-applied migration.
   Dan pastes into the SQL Editor.
4. For infra: write the config in `infra/`, test on the VPS by hand,
   commit working configs back.
5. For bridge: Deno is `deno run`-ready. Always include a smoke test
   command in the commit message.
6. For firmware (Phase 1+): PlatformIO project, `platformio.ini` pinned,
   commit a working `pio run` before moving on.

## When you are wrong about which repo

If `git remote -v` shows `classroomsounds`, you are in the v5 trial
stack. Stop. Open the holly-platform session instead. Never paste v6
architecture into v5.

---

## Engineer / hardware context

The PCB and v6 board work is being done by an external engineer.
Communication so far is captured in `HARDWARE_v6.md`. When Dan reports
new info from the engineer, update `HARDWARE_v6.md` to keep it the
single source of truth on the hardware side.

The engineer is responsible for: PCB design, board manufacturing,
populating connectors, validating peripherals. **Carson is responsible
for: firmware, backend, dashboards, MQTT/broker, Supabase schema, OTA
flow.** Don't try to redesign the board from the firmware side — push
hardware questions to Dan, who relays to the engineer.
