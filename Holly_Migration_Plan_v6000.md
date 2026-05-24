# Holly v6000 — Migration Plan
**Classroom Sounds Ltd · Chestnut Holdings**
Author: Daniell Lee (with Carson)
Status: Draft architecture, side-by-side model confirmed, pending commercial gate
Date: 2026-05-08

---

## 0. Framing — two separate products

Classroom Sounds runs **two parallel product lines** that share a brand and a developer, and nothing else:

**v5 = Trial / Proof-of-Concept.**
Existing hardware in Lyppard and other early-access schools. Firmware v5091, Apps Script v56, admin v2.8.5, Google Sheets backend. **Not a commercial product. No paying customers.** Validates the core classroom-sounds idea: dB monitoring, calibration, LED feedback, behaviour-management value. Stays running until trial schools naturally sunset their devices. Receives bug-fix maintenance only — no new features. No migration path to v6 is planned; if a trial school later becomes a paying customer, they receive fresh v6 hardware and onboard cleanly into the new stack.

**v6 = Launch Product.**
The commercial product. Every paying school customer, from the first sale onwards, runs v6. New hardware, MQTT messaging over self-hosted Mosquitto, Supabase data plane, admin v3.0.0 on a separate URL, integrated Martyn's Law alerts (Lockdown / Invacuation / Evacuation / All Clear + Drill, publishable per-zone or fleet-wide). Designed from day one as compliance-grade infrastructure, not a behaviour-management novelty.

The two stacks never communicate. No shared data, no shared identity, no shared firmware, no shared backend. A v5 device is a different SKU from a v6 device. The development work documented in this plan is purely about building v6 — v5 is mentioned only to clarify what's deliberately out of scope.

## 0a. Why the v5 architecture won't carry the commercial product

The trial stack (polling devices, Apps Script + Sheets backend, token-bolted auth) has three structural ceilings that make it unsuitable as the foundation for a Protect-Duty-grade commercial product:

1. **Commands to the device are slow.** Config changes, room tests, reboots all wait for the next 60s POST cycle. A lockdown signal would average ~30 seconds late — unacceptable for any safety-critical application.
2. **Apps Script + Google Sheets won't scale past ~100 devices.** Lock contention, the 6-minute execution limit, and CacheService's 100KB cap are already pressing constraints at trial scale.
3. **No native auth or row-level security.** Token-based auth bolted onto Apps Script works for a controlled trial; it doesn't pass a school IT department's procurement review and it doesn't satisfy the audit trail expectations of safety-critical buyers.

v6 is therefore a clean-architecture build rather than an evolution:

- **Self-hosted Mosquitto** as the device messaging broker (TLS, persistent connections, retained messages — solves the slow-command and reliability-during-incident problems).
- **Supabase** as the data plane (Postgres + Auth + Realtime + Edge Functions — solves the scale, RLS, and procurement-acceptability problems).
- **A small bridge service** gluing the two together.
- **Firmware v6000** rewritten to use MQTT for both telemetry-out and commands-in.


Plus a new safety-critical feature: **Martyn's Law alerts** (Lockdown / Invacuation / Evacuation / All Clear + Drill), publishable per-zone or fleet-wide.

---

## 1. Architecture overview

```
┌─────────────────┐                     ┌───────────────────────────────┐
│   Holly device  │  MQTT/TLS (8883)    │      Mosquitto broker         │
│   (firmware     │ ──────────────────► │      (self-hosted, Hetzner)   │
│    v6000)       │ ◄────────────────── │                               │
└─────────────────┘    retained msgs    └─────────────┬─────────────────┘
        ▲                                             │
        │                                             │ MQTT subscribe (wildcards)
        │ retained alert state                        ▼
        │ on reconnect                  ┌─────────────────────────────┐
        │                               │   Bridge service (Deno)     │
        │                               │   - MQTT ↔ Postgres write   │
        │                               │   - Same VPS as broker      │
        │                               └─────────────┬───────────────┘
        │                                             │
        │                                             │ Postgres writes
        │                                             ▼
        │                               ┌─────────────────────────────┐
        │                               │   Supabase                  │
        │                               │   ┌─────────────────────┐   │
        │                               │   │ Postgres + RLS      │   │
        │                               │   └─────────────────────┘   │
        │                               │   ┌─────────────────────┐   │
        │ ◄─────────────────────────────┼─► │ Edge Functions      │   │
        │     publish (alerts, config)  │   │  - alert_publish    │   │
        │     via Edge Function →       │   │  - device_register  │   │
        │     Bridge → MQTT             │   └─────────────────────┘   │
        │                               │   ┌─────────────────────┐   │
        │                               │   │ Realtime (WSS)      │ ──┼──► Browser dashboards
        │                               │   └─────────────────────┘   │     (admin + staff)
        │                               │   ┌─────────────────────┐   │     instant updates,
        │                               │   │ Auth (passwordless, │   │     no polling
        │                               │   │ magic link)         │   │
        │                               │   └─────────────────────┘   │
        │                               └─────────────────────────────┘
        │
        └─── Captive portal first-time setup (HTTPS to Supabase Edge Function,
             same as today's flow — only the data plane changed)
```

**Why each piece:**

- **Mosquitto on a VPS** — handles N persistent connections cheaply, ~5MB RAM per 1000 connections. Battle-tested IoT broker. Has retained-message semantics that solve the "device just rebooted, what's the current alert state?" problem cleanly. We control the upgrade cadence; no third-party SLA surprises.
- **Supabase** — saves us writing auth, an admin UI for tables, RLS policies, realtime change streams, and a postgres ops layer. Replaces about 80% of what Apps Script does, with a real database underneath.
- **Bridge service** — Supabase doesn't speak MQTT natively. A tiny Deno service (~200 lines) subscribes to MQTT topics with wildcards and INSERTs into Postgres. Lives on the same VPS as Mosquitto to keep latency near zero.
- **Edge Functions for command publication** — when a dashboard needs to publish (admin clicks "Lockdown"), the browser hits a Supabase Edge Function. The function validates auth + RLS, writes an audit row, then HTTP-POSTs to the bridge service which publishes to MQTT. Single trust boundary; browsers never connect to MQTT directly.

---

## 2. Postgres schema

All tables live in the `holly` schema. RLS enabled on everything customer-facing.

```sql
-- ═══ Schools & zones ═══
create table holly.schools (
  id            uuid primary key default gen_random_uuid(),
  name          text not null unique,
  org_type      text,           -- 'primary', 'secondary', 'sen', etc.
  urn           text,           -- DfE URN if applicable
  address       text,
  town          text,
  county        text,
  postcode      text,
  country       text default 'GB',
  timezone      text default 'Europe/London',
  protect_duty_two_factor  boolean default true,  -- v6000 setting
  created_at    timestamptz not null default now()
);

create table holly.zones (
  id            uuid primary key default gen_random_uuid(),
  school_id     uuid not null references holly.schools(id) on delete cascade,
  name          text not null,                    -- 'Year 3 Corridor', 'Hall', etc.
  colour        text default '#5AB4F0',           -- for dashboard
  created_at    timestamptz not null default now(),
  unique (school_id, name)
);

-- Every school has a default 'Unassigned' zone created on insert (trigger).

-- ═══ Devices ═══
create table holly.devices (
  id            text primary key,                 -- the 6-char MAC suffix (EF3E90), preserved from v5xxx
  school_id     uuid not null references holly.schools(id),
  zone_id       uuid not null references holly.zones(id),
  holly_name    text,
  classroom     text,
  year_group    text,
  year_band     text,
  teacher_name  text,
  send_count    int default 0,
  notes         text,
  mac_address   text,
  ip_address    text,
  firmware_version text,
  -- Active config (what the device is actually using)
  active_green   numeric,
  active_amber   numeric,
  active_red     numeric,
  active_brightness int default 100,
  active_school_open boolean default true,
  active_device_phase text,                       -- 'monitoring', 'dormant', etc.
  active_window_state text,                       -- 'lesson', 'break', etc.
  -- Health
  last_seen_at    timestamptz,
  last_telemetry_at timestamptz,
  last_heartbeat_at timestamptz,
  wifi_rssi       int,
  uptime_minutes  int,
  last_reset_reason text,
  last_error      text,
  -- Calibration
  mic_floor       numeric,
  mic_floor_valid boolean default false,
  mic_floor_date  timestamptz,
  mic_offset_correction numeric default 0,
  cal_status      text default 'disabled',        -- 'learning', 'complete', 'disabled'
  cal_samples     int default 0,
  cal_green       numeric,
  cal_amber       numeric,
  cal_red         numeric,
  -- Sensitivity
  sensitivity_profile text default 'standard',
  -- Auth (the bcrypted shared secret for this device's MQTT login)
  mqtt_username   text not null,                  -- always device id
  mqtt_password_hash text not null,               -- bcrypt
  created_at      timestamptz not null default now()
);

create index on holly.devices (school_id);
create index on holly.devices (zone_id);

-- ═══ Telemetry (the big table) ═══
create table holly.telemetry (
  id            bigserial primary key,
  device_id     text not null references holly.devices(id),
  school_id     uuid not null,                    -- denormalised for RLS
  zone_id       uuid not null,                    -- denormalised for RLS
  at            timestamptz not null default now(),
  avg_db        numeric,
  led_db        numeric,
  led_zone      smallint,                         -- 0=green, 1=amber, 2=red
  device_phase  text,
  window_state  text,
  voice_pct     numeric,
  raw           jsonb                             -- everything else, future-proof
);

create index on holly.telemetry (device_id, at desc);
create index on holly.telemetry (school_id, at desc);

-- Partitioned by month for retention/cleanup once we hit ~10M rows.

-- ═══ Device health snapshots (1 row per device, overwritten) ═══
-- Already covered by holly.devices.last_* columns. No separate table.

-- ═══ Events (boot, ota, lockdown, room test, etc.) ═══
create table holly.events (
  id            bigserial primary key,
  device_id     text references holly.devices(id),
  school_id     uuid not null,
  zone_id       uuid,
  at            timestamptz not null default now(),
  type          text not null,                    -- 'boot', 'ota_completed', 'reward', 'room_test_started', 'alert_received', 'alert_acked', ...
  payload       jsonb
);

create index on holly.events (device_id, at desc);
create index on holly.events (school_id, at desc, type);

-- ═══ Alerts (Martyn's Law — safety-critical, separate audit-grade table) ═══
create type holly.alert_kind as enum ('lockdown', 'invacuation', 'evacuation', 'all_clear', 'drill_lockdown', 'drill_invacuation', 'drill_evacuation');
create type holly.alert_scope as enum ('school', 'zone', 'device');

create table holly.alert_events (
  id            uuid primary key default gen_random_uuid(),
  school_id     uuid not null references holly.schools(id),
  scope         holly.alert_scope not null,
  scope_target  uuid,                             -- zone_id or device_id (as uuid-coerced for devices), null for school-wide
  kind          holly.alert_kind not null,
  initiated_by  uuid not null,                    -- references auth.users
  initiated_at  timestamptz not null default now(),
  initiated_ip  inet,
  initiated_user_agent text,
  resolved_at   timestamptz,                      -- null while active
  resolved_by   uuid,
  reason        text,                             -- free text from initiator
  superseded_by uuid references holly.alert_events(id),  -- e.g. lockdown superseded by evacuation
  notes         text                              -- post-incident notes
);

create index on holly.alert_events (school_id, initiated_at desc);

create table holly.alert_acknowledgements (
  alert_id      uuid not null references holly.alert_events(id) on delete cascade,
  device_id     text not null references holly.devices(id),
  acked_at      timestamptz not null default now(),
  primary key (alert_id, device_id)
);

-- ═══ Audit log (everything-else admin actions) ═══
create table holly.audit_log (
  id            bigserial primary key,
  at            timestamptz not null default now(),
  actor_id      uuid,                             -- references auth.users, nullable for system actions
  actor_email   text,                             -- denormalised, survives user deletion
  action        text not null,                    -- 'device_register', 'config_change', 'start_learning', etc.
  target_type   text,                             -- 'device', 'school', 'zone'
  target_id     text,
  before        jsonb,
  after         jsonb,
  ip            inet,
  user_agent    text
);

create index on holly.audit_log (at desc);
create index on holly.audit_log (target_type, target_id, at desc);

-- ═══ RLS policies (summary) ═══
-- schools: admins see all; school users see their own school only.
-- devices/zones/telemetry/events: bound to school_id of authenticated user.
-- alert_events: bound to school_id; only school_admins can insert.
-- audit_log: read-only for schools (their own); admins see all.
```

---

## 3. MQTT topic taxonomy

All topics under the `holly/` namespace. Devices authenticate with username = device_id and a bcrypted password set at registration.

| Topic | Direction | QoS | Retained | Purpose |
|---|---|---|---|---|
| `holly/<school>/<zone>/<device>/telemetry` | device → broker | 0 | no | Per-second-ish telemetry stream. Fire-and-forget. |
| `holly/<school>/<zone>/<device>/heartbeat` | device → broker | 1 | no | Every 30s health beat. QoS 1 = guaranteed delivery. |
| `holly/<school>/<zone>/<device>/event` | device → broker | 1 | no | Boot, OTA completion, reward, room test results, etc. |
| `holly/<school>/<zone>/<device>/will` | broker → bridge (on device disconnect) | 1 | no | MQTT Last-Will-and-Testament. Bridge marks device offline. |
| `holly/<school>/+/+/cmd/<command>` | broker → device | 1 | yes (for alerts), no (for transient cmds) | Commands to devices. |
| `holly/<school>/<zone>/<device>/ack/<command>` | device → broker | 1 | no | Per-device ACK back to bridge. |

**Wildcard publishing:**
- Fleet-wide alert: publish to `holly/<school>/+/+/cmd/alert` with payload identifying the alert kind.
- Zone-wide alert: publish to `holly/<school>/<zone>/+/cmd/alert`.
- Single device command: publish to `holly/<school>/<zone>/<device>/cmd/<cmd>`.

**Retained messages = the fail-safe.** The `cmd/alert` topic uses retained=true. So:
- A device that reboots mid-lockdown reconnects, subscribes, and *immediately* receives the retained lockdown message. No race window.
- All Clear replaces the retained message with `{"state":"clear"}` (or by publishing an empty payload + retained=true, which deletes retention).
- Newly added devices that join a school during an active alert get the alert state on first subscribe.

**Command payload shape:**

```json
{
  "cmd": "alert",
  "kind": "lockdown",
  "drill": false,
  "scope": "school",
  "correlation_id": "9c2f3d8e-…",
  "issued_at": "2026-05-08T14:23:11Z",
  "ttl_seconds": null
}
```

**Non-alert commands** (transient, not retained):
- `cmd/reboot` — restart device
- `cmd/locate` — flash for 60s to physically find it
- `cmd/room_test` — start mic floor calibration
- `cmd/start_learning` / `cmd/cancel_learning`
- `cmd/set_config` — push new thresholds, year band, sensitivity, etc.
- `cmd/set_mic_offset`

---

## 4. Bridge service

Tiny Deno application on the same VPS as Mosquitto.

**Responsibilities:**
1. Connect to Mosquitto as an admin user; subscribe to `holly/+/+/+/telemetry`, `.../heartbeat`, `.../event`, `.../ack/+`, `.../will`.
2. For each incoming message: validate device_id is registered, then INSERT into the appropriate Postgres table (`telemetry`, `events`, `alert_acknowledgements`). Batch INSERTs every 200ms for throughput.
3. On `.../will` (LWT): set `devices.last_seen_at` to whatever it was, don't bump; emit an event of type `device_disconnect`.
4. Expose a minimal HTTP endpoint `POST /publish` (mTLS + token auth) that Supabase Edge Functions call when they need to publish to MQTT (e.g., when admin triggers an alert).
5. Publish device-config-pull responses: when a device asks for its config via a `cmd/get_config` request, bridge looks up the row in Postgres and publishes the response on the per-device topic.

**Skeleton:**

```typescript
// bridge.ts
import { connect as mqttConnect } from "npm:mqtt";
import { createClient } from "npm:@supabase/supabase-js";

const mqtt = mqttConnect("mqtts://localhost:8883", {
  username: "bridge",
  password: Deno.env.get("BRIDGE_MQTT_PASSWORD"),
});

const sb = createClient(
  Deno.env.get("SUPABASE_URL")!,
  Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
);

const telemetryBuffer: TelemetryRow[] = [];

mqtt.on("connect", () => {
  mqtt.subscribe("holly/+/+/+/telemetry", { qos: 0 });
  mqtt.subscribe("holly/+/+/+/heartbeat", { qos: 1 });
  mqtt.subscribe("holly/+/+/+/event",     { qos: 1 });
  mqtt.subscribe("holly/+/+/+/ack/+",     { qos: 1 });
});

mqtt.on("message", (topic, payload) => {
  const [, school, zone, device, kind, ...rest] = topic.split("/");
  if (kind === "telemetry") {
    telemetryBuffer.push({
      device_id: device,
      school_id: schoolCache.get(school),
      zone_id:   zoneCache.get(`${school}/${zone}`),
      at: new Date(),
      ...JSON.parse(payload.toString()),
    });
  }
  // ... heartbeat / event / ack handlers
});

setInterval(async () => {
  if (telemetryBuffer.length === 0) return;
  const batch = telemetryBuffer.splice(0, telemetryBuffer.length);
  await sb.from("telemetry").insert(batch);
}, 200);

// HTTP endpoint for Edge Functions to publish via us
Deno.serve({ port: 8443 }, async (req) => {
  // verify bearer token
  const { topic, payload, retain, qos } = await req.json();
  mqtt.publish(topic, JSON.stringify(payload), { retain, qos });
  return new Response("ok");
});
```

**Deployment:** systemd unit, restart on failure, journald logs. Bridge process is stateless aside from `schoolCache`/`zoneCache` lookups which it rebuilds on startup.

---

## 5. Firmware v6000 structure

**What stays from v5091:**
- Captive portal first-time setup (`captive_portal.h/.cpp`) — same UX, different POST target.
- I2S mic loop, FFT voice detection, NeoPixel render path.
- Three-threshold zone logic, hysteresis, sleep state machine, room calibration (Welford), mic floor test.
- OTA mechanism (manifest.json + chunked write) — switched to a Supabase Storage URL.
- Watchdog setup.

**What gets removed:**
- All `postToSheets()` / `postEvent()` / `postHeartbeat()` plumbing built on `HTTPClient` POSTs to Apps Script.
- The 60s telemetry / 3-min heartbeat polling cadence logic.
- `processRemoteConfig()` that parsed Apps Script JSON responses.
- The two-tier `compareVersionTokens()` OTA logic — replaced by an MQTT command + Edge Function flow that the device can trust.

**What's new:**
- MQTT client (PubSubClient — simple, ~10KB flash). Persistent connection over TLS to broker on port 8883.
- TLS root cert bundle for broker pinning (Let's Encrypt → root CA pinned in firmware).
- New NVS keys:
  - `mqtt_pass` — bcrypted shared secret, written at first-time-setup
  - `zone_id` — string, the device's zone
  - `school_slug` — string, the school's slug for topic prefix
  - `alert_state_held` — current retained alert payload (so we survive a reboot mid-alert without re-fetching from broker)
- New loop logic:
  - `mqttClient.loop()` called every iteration (replaces the timed POST scheduler).
  - On message receipt: parse JSON, dispatch by `cmd` field, queue a deferred-action flag for the main loop (same pattern as `processRemoteConfig` in v5xxx — TLS handler stays clean).
  - Heartbeats every 30s (down from 3 min — broker connections are cheap, lets us detect offline within 60s).
  - Telemetry: publish on zone-state-change OR every 5s, whichever first. Was 60s in v5xxx but MQTT is so much cheaper that we get higher resolution for free.
- New alert state machine (see §6).
- LWT (Last Will and Testament) registered on connect: payload `{"event":"disconnect"}` on `holly/<school>/<zone>/<device>/will`, retained=false, QoS 1. Broker publishes this automatically when the device's keepalive lapses (~45s). Bridge picks it up and marks device offline.

**Connection lifecycle:**
1. Boot → WiFi → NTP → MQTT connect with TLS + credentials from NVS.
2. Subscribe: `holly/<school>/+/+/cmd/alert` (school-wide alerts), `holly/<school>/<zone>/+/cmd/alert` (zone-wide), `holly/<school>/<zone>/<device>/cmd/+` (device-specific).
3. Publish initial `event/boot` with firmware version, RSSI, NVS config version.
4. Enter normal loop.

**Reconnect strategy:**
- WiFi drop: existing v5031 behaviour (8s grace, then nudge), unchanged.
- MQTT drop while WiFi up: exponential backoff (1s, 2s, 4s, 8s, max 30s). On reconnect, resubscribe to all topics — retained messages on `cmd/alert` will fire immediately.
- Backend unreachable for > 5 min: keep retrying silently. LEDs continue rendering the last known alert state — the **fail-safe hold** is non-negotiable.

---

## 6. Alert state machine

```
   normal_monitoring
         │
         │  receive cmd/alert kind=lockdown
         ▼
      lockdown ◄────┐
         │          │  receive cmd/alert kind=invacuation
         │          │  (lower priority — ignored, lockdown holds)
         │
         │  receive cmd/alert kind=evacuation
         ▼
     evacuation ◄────┐
         │           │  any other alert kind ignored (evacuation is top priority)
         │
         │  receive cmd/alert kind=all_clear
         ▼
   normal_monitoring
```

**Priority order (high → low):**
1. evacuation
2. lockdown
3. invacuation
4. all_clear (returns to normal)
5. (drill variants slot in at same priority as their live equivalent)

**State persistence:**
- On entering an alert state, the device writes the alert payload to NVS (`alert_state_held`).
- On boot, if `alert_state_held` is non-empty, the device enters that alert state IMMEDIATELY, before MQTT connects. This handles the case where mains power blips during an active lockdown — devices come back lockdown-on, not "monitoring".
- The retained MQTT message is the authoritative state. On MQTT reconnect, whatever the broker has wins — if All Clear has been published since the device went offline, the device will receive that and clear the held state.

**ACK protocol:**
- On entering an alert state, the device publishes `holly/<school>/<zone>/<device>/ack/alert` with `{correlation_id, kind, state:"received"}`.
- Dashboard tracks `alert_acknowledgements` and shows "12/14 devices acknowledged" — surfaces devices that haven't responded so staff can physically check.

**Drill mode:**
- Same render priorities, same state machine, but `drill: true` flag.
- LEDs render at 40% brightness with a marker pixel at 12 o'clock.
- Database event is type `drill_<kind>` not `<kind>`, so school inspections can filter drills vs incidents.

**LED render (matches §"Proposed LED patterns" from chat):**
- Lockdown: solid red, 2Hz pulse, full strip.
- Invacuation: solid amber, chase pattern (one revolution per 2s, clockwise).
- Evacuation: white + red alternating halves, 4Hz strobe.
- All Clear: green sweep around the ring once (~1.5s), then return to normal monitoring.
- Drill: same colour/pattern, 40% brightness, dim-white marker pixel at top.

---

## 7. Dashboard changes

v6 dashboards are separate codebases from the v5 trial dashboards. v5's `admin_v2.8.5.html` and the existing staff dashboard remain deployed at `admin.classroomsounds.co.uk` for trial-fleet support. v6 dashboards ship on a new domain — `console.classroomsounds.co.uk` — chosen to reflect the SaaS-product positioning of the commercial line.

**Admin v3.0.0 (`console.classroomsounds.co.uk`):**
- Auth: Supabase magic-link (passwordless) for school admins; named fleet-admin role for Classroom Sounds internal staff.
- Data: Supabase JS client with `realtime.subscribe()` on `holly.devices`, `holly.telemetry`, `holly.alert_events`. No polling. Updates appear within ~100ms of the underlying Postgres write.
- New screens:
  - **Zones** — CRUD on `holly.zones`, drag-drop devices between zones.
  - **Alerts** — big buttons for the four states, scope selector (school / zone / device), drill toggle, reason field, two-factor confirmation modal (if school has it enabled).
  - **Active Incident** — when an alert is live, this screen takes over: shows scope, time since initiation, per-device ACK status, an All Clear button with confirmation.
  - **Incident history** — read-only audit view of all `alert_events` for the school, filterable by date / kind / scope.
- Existing screens conceptually carried over from v2.8.x (Sites, Calibration, Global Analytics, Dispatch) reimplemented against Supabase — not ported file-for-file, since the underlying data shape changes.

**Staff v13.0.0 (`console.classroomsounds.co.uk/staff` or subdomain — TBD with design partner):**
- Same Supabase Realtime architecture, no polling.
- Adds a non-dismissible alert banner at the top of the page when an alert is live in the staff member's school.
- Per-device behaviour, leaderboards, etc. conceptually preserved from v12.x.

---

## 8. Rollout

There is **no cutover**, no dual-stack firmware, no data migration. The trial (v5) stack stays running independently and indefinitely; the v6 commercial stack is built greenfield alongside it. Three phases gate the v6 build.

**Phase 0 — Standup (≤1 week of work, can run in parallel with the commercial gating below):**
- Provision Hetzner CX21 (£5.83/mo, 2 vCPU, 4GB RAM). Install Docker, Mosquitto, Caddy (reverse proxy for ACME TLS).
- Provision Supabase project (Pro tier from day one — £19/mo equivalent, gives us scheduled backups).
- Apply database schema (§2) via Supabase migrations.
- Stand up bridge service skeleton. Smoke-test publish/subscribe roundtrip.
- Domain `console.classroomsounds.co.uk` reserved and pointed at a placeholder Vercel project.

Phase 0 is cheap and low-risk — it stands up infrastructure that costs ~£25/mo to keep idle. Doing it early proves the architecture works and gives the design partner something tangible to be shown.

**Phase 1 — Bench-test on net-new hardware (1–2 weeks after Phase 0):**
- Flash one or two SPARE Holly units (not from the trial fleet) with firmware v6000. They register against the new Supabase project, talk to the new Mosquitto broker, surface in admin v3.0.0.
- Trial fleet completely unaffected. v5091 OTA manifest unchanged; v6000 uses a separate manifest URL at a separate path.
- Verify the safety-critical paths: alert publishing, retained-message delivery to a freshly-rebooted device, NVS-held alert state surviving a hard power cycle, ACK protocol, two-factor confirmation, drill flagging.
- Stress-test: 50 simulated devices via a load-generator publishing telemetry and heartbeats, confirm bridge throughput and Supabase write rate.

**Phase 2 — First commercial school (timed to the design-partner MAT's readiness):**
- Onboard one paying or paying-intent school onto v6. Direct hardware delivery, captive-portal first-time setup using the new MQTT credential flow, manual zone configuration in admin v3.0.0.
- Monitor closely for 2–4 weeks. Both technical metrics (broker uptime, message delivery, dashboard responsiveness) and operational metrics (admin task completion times, alert drill walk-throughs, support ticket volume).
- If clean, open up to additional commercial customers. v6 becomes the live commercial product from this point onwards.

**There is no Phase 3.** v5 trial stack continues unaffected. It receives bug-fix maintenance only — no v5 customer ever migrates to v6. When the trial naturally winds down (timing TBD), the Apps Script project gets archived and the Google Sheet becomes a read-only historical record. That's months or years away and isn't on the v6 critical path.

**Realistic timeline.** Phase 0 can begin the day after the commercial gating decisions below resolve. Phase 1 can start a week after that. Phase 2 timing is bounded by the design partner's procurement cycle, not by engineering — expect 4–12 weeks between Phase 1 completion and first commercial install.

## 8a. Commercial gating — must resolve BEFORE Phase 0 budget commitment

The architecture and engineering work in this document is robust enough to commit to. The commercial assumptions underlying it are not yet validated. Three workstreams need to resolve before significant engineering or infrastructure spend goes ahead:

1. **Product liability insurance quote.** Talk to a broker who's placed cover for safety-critical tech-in-schools. Without a realistic premium estimate, the unit economics of v6 are unknown. **Single highest-leverage commercial action.** 2–3 weeks of process.
2. **First MAT design partner identified and engaged.** Pitch the dual-purpose (behaviour + alerts) vision to a multi-academy trust's central IT or safeguarding lead; secure non-binding commitment as design partner. They define what lockdown actually needs to do in their context, validate the price point, and become the first commercial reference. Parallel with the insurer work; 2–4 weeks.
3. **Indemnity contract template.** Solicitor with schools-tech experience drafts the customer agreement explicitly stating: school retains primary responsibility for executing their Protect Duty plan; Holly is a communication aid; school carries out their own training and drills; Holly's sole obligation is to transmit alerts when triggered by an authorised user. Without this, the liability exposure is open-ended. £2–5k of legal time, 1–2 weeks once briefed.

Phase 0 infrastructure work is cheap enough to start in parallel (£25/mo running cost while idle, half a day of ops effort). Phase 1 firmware and bridge engineering should not begin until all three commercial gates have at least one positive datapoint each — otherwise we risk building product against assumptions that don't hold.

---

## 9. Security & compliance

**Protect Duty implications:**
- This is now arguably a "publicly accessible location safety system" component once Protect Duty becomes law. You are part of the chain of custody of a school's emergency response. Two things follow.
- **Liability**: speak to your insurer before pilot rollout. You're not certifying the system as a primary safety device (it's an awareness aid alongside formal procedures), but the conversation should be on record. A formal indemnity in the school contract is essential — they bear primary responsibility for executing their Protect Duty plan; Holly is one of several mechanisms by which the plan is communicated.
- **Certification**: there's no formal certification scheme today for Protect Duty awareness devices. If/when one emerges, design for that pathway (audit logs, fail-safe defaults, documented test procedures).

**DPIA addendum:**
- The system handles **identifiable adults** (initiator name/email in `alert_events`) and **incident metadata** that could be linked to identifiable pupils through context. Schools need a DPIA addendum covering this. We provide a template; they own the assessment.
- Telemetry data (dB levels, zone state) is not personal data on its own. Combined with classroom + timestamp, it's borderline — your DPIA should treat it as such.
- Retention: telemetry 13 months (school year + 1); alert_events 7 years (matches schools' incident retention norms); audit_log 7 years.

**Fail-safe defaults:**
- MQTT broker unreachable → device holds last alert state from NVS. **It does not "return to monitoring"** on backend failure. Alert state is sticky until explicitly cleared.
- Bridge service down → MQTT broker still functions; devices still receive commands published by other bridge instances or directly via the Mosquitto admin. (We may stand up a hot-standby bridge in v6001 — single point of failure today.)
- Supabase down → devices keep operating on last-known config. Dashboards are unavailable; alerts already published are still being held by retained MQTT messages.
- Total internet loss at the school → devices keep operating on last-known config, render last-known alert state. Pupil-facing experience continues uninterrupted.

**Two-factor on alert initiation:**
- Per-school toggle (`schools.protect_duty_two_factor`).
- Default: ON.
- Mechanism: admin enters a 4-digit code (configured per school in setup) or completes a WebAuthn challenge if their browser supports it.
- Drill mode bypasses two-factor (otherwise drills are too painful to run regularly, and infrequent drills are unsafe).

**Audit log:**
- Every alert initiation, every All Clear, every config push, every device register/deregister, every zone change.
- Append-only. No UI to edit or delete. Database-level permission strips DELETE/UPDATE from authenticated users on `audit_log`.

**What's still out of scope (for v6000):**
- Insurance and liability documentation — your remit.
- Formal certification — not yet a thing in UK.
- Disaster recovery for the VPS — manual restore from Supabase backup + Mosquitto config from git. Add automated DR in v6001.
- SMS/voice escalation when an alert is initiated — would integrate Twilio. Defer to v6002 unless customers ask urgently.

---

## 10. Cost & ops

**Monthly:**
- Hetzner CX21 VPS: £5.83 (2 vCPU, 4GB RAM, 40GB SSD, 20TB transfer).
- Supabase Pro: £19 (8GB DB, 50GB egress, daily backups).
- Domain + Let's Encrypt: £0 (Let's Encrypt free).
- **Total: ~£25/month** for the backend stack supporting up to ~500 devices comfortably. Triples at 2000 devices when we go Hetzner CX31.

**Setup cost (one-off):**
- Domain configuration, DNS, broker certificate provisioning: a few hours.
- Database schema + RLS policies: 1–2 days.
- Bridge service: 2–3 days.
- Firmware v5092 (dual-stack) + v6000 (pure MQTT): 1–2 weeks combined.
- Dashboard rewrite (admin v3.0.0 + staff v13.0.0): 1–2 weeks.

**Monitoring:**
- Mosquitto: `mosquitto_sub -t '$SYS/#'` and a small Prometheus exporter. Alert on connected_clients < expected.
- Bridge: Healthcheck endpoint `/healthz` polled by uptimerobot.com (free tier).
- Supabase: built-in dashboards + email alerts for query timeouts.
- Per-device offline detection: `last_seen_at < now() - interval '5 minutes'` → flag in dashboard.

**Backup:**
- Postgres: Supabase daily snapshots (Pro tier includes this).
- Mosquitto config + ACLs: in git.
- Bridge service: in git.
- VPS itself: Hetzner snapshots weekly (£1/mo extra).

**Disaster scenarios:**
- VPS dies entirely → spin up a new one from snapshot in ~10 minutes. Devices reconnect to the broker's new IP via DNS (which is why we use a domain, not a bare IP).
- Mosquitto config corrupted → restore from git, restart, devices reconnect.
- Bridge service crashes → systemd restarts; if persistent, alerts via uptimerobot.
- Supabase outage → devices unaffected, dashboards unavailable. Wait it out.
- VPS network down for hours → devices on each school's local network can't push telemetry but continue functioning. They'll catch up on reconnect (though telemetry from the outage window is lost; alerts are not — retained MQTT preserves them).

---

## 11. Open questions for follow-up

- Final Mosquitto auth strategy: bcrypted passwords per device (recommended) vs client certs per device (more secure, more setup pain). Default to passwords for v6000, can move to certs later if needed.
- Whether to expose a public read-only "fleet status" endpoint for partner integrations (e.g., a school's existing safeguarding dashboard wanting to ingest our alert state).
- Retention of telemetry beyond 13 months — researchers may want longer; we'd archive to cheap storage rather than keep hot in Postgres.
- Whether v6000 firmware should add a hardware button combination for "trigger lockdown from the device itself" (e.g., long-press of a side button) — staff-of-last-resort escape hatch. Out of scope for now unless schools ask.

---

## 12. Sign-off and current status

This document supersedes the "Phase 2 — Supabase migration" section of `Holly_Master_Context_Prompt_v4.md`. The Master Prompt should be updated in the same PR that touches this plan next.

**Architecture decisions confirmed (in conversation, 2026-05-08):**
- ☑ Hybrid architecture: self-hosted Mosquitto for device messaging, Supabase for data plane and dashboards
- ☑ Self-hosted Mosquitto from day one (no managed-broker stepping-stone)
- ☑ Zones included in v6000 (single clean migration, not deferred)
- ☑ Four-state NaCTSO alert taxonomy: Lockdown / Invacuation / Evacuation / All Clear, plus Drill
- ☑ Two-factor on alert initiation: opt-in per school, default ON
- ☑ Evacuation overrides Lockdown
- ☑ Side-by-side model: v5 (trial, no commercial customers) and v6 (commercial product) run as wholly separate stacks; no migration is planned
- ☑ Commercial dashboard URL: `console.classroomsounds.co.uk`

**Commercial gating outstanding (see §8a):**
- ☐ Product liability insurance quote obtained
- ☐ First MAT design partner engaged
- ☐ Indemnity contract template drafted by solicitor

**Engineering decisions still open (see §11):**
- ☐ Mosquitto auth: passwords vs client certs (recommend: passwords for v6000)
- ☐ Public read-only fleet-status API endpoint
- ☐ Telemetry retention beyond 13 months
- ☐ Hardware-button lockdown trigger

**Operational state of the v5 trial stack (parallel, unaffected by this plan):**
- Firmware Holly v5091 (dormant LEDs off, mic-floor filter fix) deployed via OTA
- Apps Script v56 (flapping fix: numeric ms timestamps, cache invalidation, lock timeout) deployed
- Admin v2.8.5 (panel auto-refresh fix) deployed to Vercel
- Trial fleet healthy as of last review
