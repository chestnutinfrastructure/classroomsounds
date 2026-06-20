# Holly Platform (v6)

The commercial product line for Classroom Sounds Ltd — the launch product
every paying school customer runs.

This repository is **wholly separate** from the v5 trial stack (which
lives in `chestnutinfrastructure/classroomsounds`). No shared data,
identity, firmware, or backend. v5 is proof-of-concept; v6 is the product.

## What v6 is

- **Device messaging:** MQTT over TLS to a self-hosted Mosquitto broker
  (instant commands, persistent connections, retained-message fail-safe).
- **Data plane:** Supabase — Postgres + Auth + Realtime + Edge Functions.
- **Bridge:** a small Deno service gluing MQTT to Postgres.
- **Dashboards:** admin v3.0.0 + staff v13.0.0 on
  `console.classroomsounds.co.uk`, live via Supabase Realtime (no polling).
- **Hardware:** redesigned ESP32 PCB with I2S MEMS mic + NeoPixel +
  **I2S audio amp + speaker** (new in v6) + BMP280 + microSD. Built and
  validated by an external engineer. See `HARDWARE_v6.md`.
- **Toolchain:** VS Code + PlatformIO; flash via ESP-Prog (JTAG). Not
  Arduino IDE.
- **Headline feature:** Martyn's Law / Protect Duty alerts — Lockdown,
  Invacuation, Evacuation, All Clear, plus Drill — publishable per-zone
  or fleet-wide.

## Authoritative docs

Read in this order:

1. **`Holly_Master_Context_Prompt_v4.md`** — Carson persona + project
   history.
2. **`Holly_Migration_Plan_v6000.md`** — single source of truth for v6
   architecture, schema, MQTT taxonomy, bridge, firmware, alert state
   machine, dashboards, rollout, security/compliance.
3. **`HARDWARE_v6.md`** — v6 PCB pinout (definitive), PlatformIO
   toolchain notes, I2S amp + audio file conventions, engineer comms log.
4. **`CLAUDE.md`** — working rules for any Claude Code session opened on
   this repo.
5. **`KICKOFF_PROMPT.md`** — paste this as the first message in a new
   Claude Code chat to kick off v6 work cleanly.

## Supabase

- Project ref: `dhqbdxfdjgqzwpylnctp`
- MCP wired in `.mcp.json` (read-only default + a write entry for
  migrations).
- On **web** Claude Code, the MCP appears but won't authenticate without
  `SUPABASE_PAT` set; apply schema via the Supabase SQL Editor until the
  MCP is set up from a desktop CLI (`export SUPABASE_PAT=...`).

## Current status

**Phase 0 — infrastructure standup.** Commercial gating (insurer quote,
MAT design partner, indemnity contract) is outstanding and must resolve
before Phase 1 firmware work begins. See §8 and §8a of the migration plan.

The redesigned PCB exists and is in Dan's hands. PlatformIO + ESP-Prog
workflow replaces Arduino IDE. I2S amp + speaker have been added to the
hardware; the audio playback subsystem is brand-new vs v5 and is part of
Phase 1 firmware work (gated).
