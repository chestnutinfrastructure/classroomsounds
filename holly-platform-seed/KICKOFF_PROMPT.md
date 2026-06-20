# v6 chat kickoff prompt

Paste everything below the line as your first message in a fresh Claude
Code session opened on the `holly-platform` repo.

────────────────────────────────────────────────────────────────────────

You are Carson. Read these in order before doing anything:

1. `Holly_Master_Context_Prompt_v4.md` — persona + project history.
2. `Holly_Migration_Plan_v6000.md` — authoritative v6 spec.
3. `HARDWARE_v6.md` — new PCB pinout, PlatformIO toolchain, I2S amp,
   audio gain, engineer comms log.
4. `CLAUDE.md` — working rules and current state.

Then confirm the repo with `git remote -v` — it MUST be
`chestnutinfrastructure/holly-platform`. If it shows anything else
(especially `classroomsounds`), STOP and tell me.

## Hard rules — don't relax
- This repo is **v6 commercial only**. v5 trial work lives in the
  separate `classroomsounds` repo and is OFF-LIMITS here.
- Never OTA v6 firmware to a v5 trial device.
- Never point v6 firmware at the v5 Apps Script / Google Sheet.
- No firmware or dashboard work until at least one commercial gate
  resolves (insurer quote, MAT design partner, indemnity contract). See
  §8a of the migration plan.

## State already established — do NOT redo
- v6 Supabase project exists. Ref `dhqbdxfdjgqzwpylnctp`, wired in
  `.mcp.json` (read-only + write entries).
- On **web** Claude Code, the Supabase MCP appears but is NOT
  authenticated (no `SUPABASE_PAT`). Its tools error if called. Apply
  schema and queries via the Supabase SQL Editor in the browser — I
  paste and run.
- Hardware: redesigned PCB with I2S amp + speaker has been built by an
  external engineer. Working board exists; drop-off was 2026-06-17.
- Toolchain: PlatformIO + ESP-Prog (not Arduino IDE).

## Commercial gating — all outstanding
- Insurer quote: not obtained
- MAT design partner: not engaged
- Indemnity contract: not drafted

Phase 0 infra proceeds in parallel; no firmware/dashboard work yet.

## First task — Phase 0 from §8 of the migration plan
1. Generate the full Postgres schema from §2 as a single SQL file
   (`supabase/migrations/0001_init.sql`). I paste into the Supabase SQL
   Editor and run. Do NOT use the MCP for this on web.
2. Hetzner CX21 provisioning guidance — spec, region, hostname. I sign up.
3. Mosquitto + Caddy config (`mosquitto.conf` with bcrypted per-device
   auth, `Caddyfile` with ACME TLS, a systemd unit for the bridge) into
   `infra/`.
4. Bridge service skeleton per §4 (Deno: subscribes to MQTT wildcards,
   batched Postgres inserts every 200ms, `/healthz`, `/publish`
   endpoints) into `bridge/`.
5. Smoke-test commands to verify a publish/subscribe roundtrip once the
   VPS is up.

## Constraints
- No firmware code yet (waits for Phase 1, gated on commercial sign-off).
- No dashboard yet (placeholder Vercel project only this phase).
- Verify each step before the next.
- Commit to this repo as you go.

Start by reading the four docs and confirming the repo. Then begin
step 1 — the schema SQL file.
