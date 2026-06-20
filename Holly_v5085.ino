/* 
==============================================
  Classroom Sounds Ltd - Holly_RDiQ5000
  A Chestnut Holdings Company
  Developed by Daniell Lee
  Version: 01/05/26
  VERSION = 2026.05.01.5079

  Changes since 2026.05.05.5084:
    🐛 P0 BUGFIX: Mic floor test rejecting valid quiet-room readings.
       Reported on bench EF3E90: room test #1 produced 10.4 dB result
       (a clipping-at-the-floor result that should have been
       a warning); retry failed with "only 204 samples (need 300)"
       in the same room.

       Root cause: updateMicFloorTest() had a hardcoded filter
       rejecting any reading below 10 dB OR above 90 dB. v5077
       dropped the readMicDB() lower clamp from 20 to 5, but this
       filter was never updated to match. Result: valid 5–10 dB
       readings (clamped but representing real quiet-room sound
       levels) were silently rejected by the test logic. In bench
       conditions where the mic genuinely sat 5–15 dB, most readings
       fell into the 5–10 band and got dropped. Tests that scraped
       through did so by chance, with results clipped at ~10 dB
       — useless as a calibration baseline.

       v5085: filter is now `db < 5.0f || db > 90.0f` to match
       readMicDB()'s actual clamp range. Room tests in quiet
       environments will now collect samples consistently.

       Note: this doesn't fix the root environmental issue — a
       mic floor of 5 dB is still anomalously low for any normal
       classroom (typical 30–45 dB). Bench-test environments
       should be representative of deployment conditions, OR
       admins should push a manual mic_db_offset_correction via
       the admin button to bring the effective floor up to a
       typical range.

       Single-line code change. Bench-test: re-run room test on
       EF3E90, expect 1000+ samples, completion result around the
       5–15 dB range.

  Changes since 2026.05.05.5083:
    🐛 P0 BUGFIX: cancel_learning was being undone on next boot.
       Reported on bench EF3E90: admin clicked Cancel Learning, the
       firmware acknowledged and zeroed Welford as expected, then
       Dan flashed v5083 — and the 14-day learning window kicked in
       on first boot, undoing the cancel.

       Root cause: the v5072 boot-time retrofit at the top of setup()
       used `!roomCalEnabled` as its gate, which can't distinguish
       "rc_enabled was never set in NVS" (the genuine retrofit case
       this code was added for) from "rc_enabled was explicitly set
       to false by cancel_learning" (admin deliberately disabled).
       Both states read back as roomCalEnabled = false, so the
       retrofit happily re-enabled learning post-cancel on every
       reboot — including a flash + reboot cycle.

       v5084 changes the gate to `!preferences.isKey("rc_enabled")`.
       The key exists in NVS as soon as ANYTHING has written it
       (captive-portal first-setup, prior retrofit fire, or the
       cancel handler itself). Key present means state has been
       deliberate; key absent means genuine retrofit case.

       No other changes in v5084. Single-line behavioural fix.
       Bench-test: cancel learning, reboot — confirm Welford does
       NOT restart and `cal_status` stays at "disabled" / band
       defaults.

  Changes since 2026.05.04.5082:
    📡 ACCURATE DEVICE STATUS REPORTING.
       Dashboards were showing "Online — Monitoring (Green)" even when
       the device was in a non-monitoring state (break, lunch, dormant,
       sleeping, etc.) with LEDs physically off. Cause: the firmware
       only reported `led_state` (last computed zone), not the actual
       operational phase.

       v5083 adds two new fields to every telemetry/heartbeat POST:
         - device_phase: one of monitoring, sleeping, holly_hop, break,
           lunch, assembly, out_of_hours, dormant, calibrating_learning,
           calibrating_mic_floor, locating, reward.
         - ws: raw window state (lesson, break, lunch, assembly, off).

       Precedence in getDevicePhase() mirrors the firmware's own render
       priority order — locate > mic_floor > hop > reward > dormant >
       (within-lesson) sleep > learning > monitoring > between-lesson
       phase. So whatever the LEDs are doing physically, that's what
       device_phase reports.

       Dashboards (v12.23 / v2.8.0) compose displayed status from
       these fields. Backward compat: old firmware that doesn't emit
       device_phase falls back to the existing led_state logic.

  Changes since 2026.05.04.5081:
    🐛 P0: GUARDRAILS NO LONGER OVERRIDE CALIBRATED GREEN.
       v5081's aspirational green formula (greenMax = clamp(mean-2,
       40, mean-0.5)) was being silently overridden by the
       applyGuardrails() call at the end of the same function.
       For a KS2L room with micFloor=30 and learned mean=43, v5081
       computed greenMax=41, then guardrails clamped to [49, 65] →
       greenMax snapped to 49. Class told "you can be 6dB above
       your average and still green" — opposite of intent.

       v5082: calibrated thresholds end with normalizeThresholds()
       only. Guardrails still apply to the band-default early-return
       path (calCount<60), where they're correct. The whole point of
       calibration is to derive room-specific values; clamping them
       back to band-default ranges defeats the exercise.

    🛡️ P1: applyRemoteConfig DEFERRED TO loop().
       v5080 deferred the four one-shot handlers but the version-
       guarded applyRemoteConfig() block still ran inside the
       postToSheets() TLS stack — flash writes for year_group,
       classroom, thresholds, brightness, timetable times, sensitivity.
       Bounded risk (only fires on actual admin edits, not every
       POST), but heap-pressure pattern v5038 warned against.

       v5082: response body is stashed to a String global on each
       config-version change; loop() drains the flag, re-parses on a
       clean stack, runs applyRemoteConfig there. Same pattern as
       the v5080 deferred-flag handlers.

    🎓 P1: WELFORD COMPLETION = SAMPLE COUNT, NOT WALL-CLOCK.
       The 14-day timer was wall-clock; Welford only accumulated
       during ws==LESSON. A device whose window straddled half-term
       got maybe 1500 samples instead of 3000 and still finalised on
       day 14, producing unreliable thresholds. Bank-holiday weeks
       compounded the problem.

       v5082: completion gate is calCount >= ROOM_CAL_MIN_SAMPLES
       (=2000, ≈33 lesson hours, ~7 normal school days). The wall-
       clock 14-day timer is removed as a completion gate.
       roomCalStartEpoch is retained for display (when did learning
       start) but no longer drives finalisation.

       Dashboard learning_progress field now reports
       calCount/ROOM_CAL_MIN_SAMPLES instead of elapsed-time/14d.
       Schools with normal usage complete in ≈7-10 school days.
       Schools with light usage take longer, proportionally — which
       is the right behaviour for data quality.

    🚧 BENCH ONLY UNTIL VERIFIED on EF3E90.
       Verify before fleet OTA:
       - cal_green ≈ (cal_mean - 2) on a calibration-complete device
         with low micFloor (no guardrail clamping).
       - learning_progress reports sample-based percentage.
       - applyRemoteConfig changes (e.g. brightness via admin) take
         effect on the next loop tick after the POST returns, not
         inside the response handler.

  Changes since 2026.05.04.5080:
    🎯 ASPIRATIONAL GREEN TARGET (product change).
       The v5080 calibrated greenMax floated around the learned mean
       depending on classroom SD — net effect was "don't be louder
       than your usual," not "aim a bit quieter than your usual."
       v5081 sets greenMax to clamp(mean - 2.0, 40.0, mean - 0.5).
         - 2 dB below the learned mean as the target.
         - Floor of 40 dB so genuinely quiet rooms aren't given an
           unachievable bar.
         - Ceiling at (mean - 0.5) so noisy rooms always have at
           least 0.5 dB of green-zone headroom.
       Amber and red keep their SD-based scaling — they detect
       "louder than typical variability," which is what we want
       them to do. Only green gets the aspirational shift.

       Will visibly recalibrate every device whose learning has
       already completed. Not a regression — a deliberate product
       change. Heads should be told: "your green threshold has
       moved 2 dB tighter, by design."

    💓 HEARTBEATS DURING HOLLY HOP (P1 reliability fix).
       v5080's hop early-return fired BEFORE the POST scheduler, so
       a 30-minute Holly Hop produced ZERO POSTs to the backend.
       Dashboard flagged the device offline every time a teacher
       used it — anxiety-inducing for staff and noise for any
       offline-detection logic.

       Same architectural pattern v5076 fixed for the mic floor
       test, applied here. During hop:
         - Heartbeats fire on HEARTBEAT_INTERVAL_MS (or fast-cadence
           equivalent during the boot window).
         - Telemetry stays suppressed — sound during a deliberate
           break is misleading.
         - As a side benefit, the hop START event now actually
           reaches the backend. Previously hopLogPending was set on
           enter, but the loop short-circuited before the POST
           scheduler could fire it; START got overwritten by END
           30 minutes later when the hop ended. The new heartbeat
           block fires the START log on its first cycle.

    🚫 NO TELEMETRY DURING SCHOOL CLOSURE (data-quality fix).
       v5080's dormant early-return ran AFTER the POST scheduler,
       so when schoolOpen=false but the wall-clock time fell in a
       "lesson hour" per the timetable (e.g. 10am during summer
       holiday), the device still logged sound_dB to the school
       data tab. Empty-classroom readings polluting reports.

       v5081 gates inActiveMonitoring on schoolOpen — telemetry
       is suppressed when school is closed regardless of
       wall-clock. Heartbeats still fire (the dashboard needs to
       see the device alive). Same fix applied to the boot
       check-in path.

    🚧 BENCH ONLY UNTIL VERIFIED on EF3E90.
       Before fleet OTA: confirm calibrated thresholds compute
       correctly (compare cal_green in registry to (cal_mean - 2)),
       confirm hop heartbeats arrive on the dashboard during a
       30-min hop, confirm a closed-school day produces zero
       telemetry but ongoing heartbeats.

  Changes since 2026.05.01.5079:
    🛡️ DEFERRED REMOTE-CONFIG HANDLERS (P1 RELIABILITY FIX).
       The v5038 design comment in processRemoteConfig() said it must
       ONLY set flags so it can run safely on the deep postToSheets()
       stack with TLS buffers active. v5075/v5077/v5078 added four
       handlers that violated this contract — they did flash writes
       AND opened a SECOND HTTPClient via postEvent() while the first
       postToSheets() stack was still live. Two concurrent TLS sessions
       on top of the response handler is the most likely cause of the
       crash_panic distribution observed across the Lyppard fleet.

       v5080 converts all four to the deferred-flag pattern that
       already protects remoteRebootRequested / locateRequested /
       micFloorTestRequested. Flags are set inside processRemoteConfig
       (zero TLS overhead); the actual work runs in loop() on a clean
       stack with idle WiFi.

         - set_room_cal_enabled  -> remoteSetRoomCalEnabledRequested
         - cancel_learning       -> remoteCancelLearningRequested
         - mic_db_offset_correction -> pendingMicOffsetCorrection
                                       + remoteMicOffsetUpdateRequested
         - clear_last_error      -> remoteClearLastErrorRequested

    🛡️ POST-RECONNECT GRACE + RSSI GATE FOR postEvent().
       postEvent() previously skipped both checks that postToSheets()
       enforces (10s grace after WiFi reconnect, RSSI > -88 dBm).
       Same TLS-handshake-stalls-past-watchdog risk that prompted those
       gates in v5030. Now postEvent() applies the same gates and
       wraps http.POST in esp_task_wdt_reset() bookends.

    ⚡ EDGE-ONLY BUTTON DEBOUNCE.
       The v5079 software debounce sampled 5 times across 25ms on
       EVERY loop iteration, adding ~25ms of unnecessary lag. Now we
       only run the 5-sample confirm when the raw read disagrees with
       lastButtonState — same noise rejection on edges, no cost when
       the button is steady.

    🧹 Removed duplicate buildDeviceId() call. Was already called
       inside connectToWiFi() (line ~2567 in v5079); the second call
       in setup() at line ~2692 was a leftover from the v5044 fix.

    🚧 BENCH ONLY UNTIL VERIFIED on EF3E90.
       Do not bump manifest.json until at least 30 minutes of clean
       runtime + a full config-pull round-trip + a learning_reset
       admin command have been observed on the bench device.

  Changes since 2026.05.01.5078:
    🚫 HOLLY HOP SUPPRESSED DURING LEARNING + REAL BUTTON DEBOUNCE.
       Two coordinated fixes for a Holly Hop misfire problem on the
       Lyppard bench unit. Symptom: bench Holly was rendering rainbow
       during the 14-day learning window even though the button had not
       been pressed and the diagnostic log showed sleep=N. Investigation
       revealed:

       1. The button "debounce" was just a 120ms RATE LIMIT, not a real
          debounce. A single spurious LOW reading from electrical noise
          on GPIO 13 (which is sensitive to WiFi RF and power ripple)
          was enough to trigger enterHollyHop(). Then 500-1000ms later
          a second noise spike would fire beginExitHollyHop("manual"),
          which matched the observed serial log:
            "Holly Hop Mode entered."
            "Holly Hop Mode ended (manual)."  (700ms later)

       2. Holly Hop's render path (line 2908) draws renderSleepingRainbow()
          directly — it does NOT route through the inSleepMode flag. So
          the diagnostic field sleep=N was technically correct (the sleep
          state machine wasn't engaged) while the LEDs still showed
          rainbow because Holly Hop was live. Same architectural pattern
          that bit us in v5076 with sleep-during-learning: render path
          and reporting path disagreed.

       3. Holly Hop being active during learning steals samples from
          Welford (the calibration accumulator returns early during hop)
          and shows the wrong visual to staff.

       Fixes in v5079:
         a. Real software debounce — readButtonDebounced() samples the
            button 5 times over 25ms and only returns LOW if ALL 5 reads
            agree. Kills RF/power noise. Used as a drop-in replacement
            for the bare digitalRead().

         b. enterHollyHop() early-returns if roomCalEnabled &&
            !roomCalComplete. Button press during the 14-day learning
            window is logged ("Holly Hop suppressed (learning active)")
            but takes no action. Resumes normal behavior post-learning.

         c. getCurrentLEDState() now reports "Holly Hop (override)"
            BEFORE "Charging", so if hop ever does fire (post-learning),
            the diagnostic field and the visual agree.

  Changes since 2026.05.01.5077:
    🧹 REMOTE LASTERROR CLEAR.
       Pairs with Apps Script v49+ and admin dashboard "Clear Error" button.
       Fixes the issue where the lastError[64] in-RAM buffer would persist
       a stale error string (e.g. "Sheets POST failed: read Timeout" from a
       transient Apps Script outage) and re-report it on every telemetry
       POST, causing the dashboard alert to keep reappearing seconds after
       admin clicked "Clear Error".

       Now: when admin clicks Clear Error, the dashboard sets
       clear_last_error=true in the registry. On Holly's next config pull,
       Apps Script delivers cfg.clear_last_error=true. Holly receives it,
       zeroes her lastError[] buffer, the next telemetry POST sends an
       empty error, dashboard stays clean. Pattern mirrors the v5075
       set_room_cal_enabled / cancel_learning one-shots — the handler is
       placed BEFORE the config_version guard so it fires even when the
       version hasn't changed.

  Changes since 2026.05.01.5076:
    🎤 MIC FLOOR ARCHITECTURE OVERHAUL.
       Three coordinated changes to fix the fleet-wide variance in
       mic floor readings (20.0 dB clamps, 75.7 dB outliers, 22 dB
       spread across same-building Hollies measured at the same time):

       1. Lower clamp dropped from 20 dB → 5 dB.
          The 20 dB floor was masking genuinely quiet rooms (sub-20 dB
          SPL is achievable in empty primary classrooms after hours).
          5 dB is below human hearing threshold so still rejects sub-
          audible electrical garbage but lets through real low values.

       2. Median-of-3 sampling in readMicDB().
          Each call now does 3 i2s_read cycles ~10ms apart and returns
          the median dB value. Smooths out DMA-buffer-alignment jitter
          which was producing 5+ dB swings between adjacent readings
          on the same Holly measuring the same constant noise.

       3. Per-unit MIC_DB_OFFSET correction via cloud.
          New NVS key 'mic_offset_corr' stores a per-Holly correction
          value pushed from Apps Script. Applied additively after
          MIC_DB_OFFSET in readMicDB(). Apps Script v49 computes this
          from the per-school median mic floor — Hollies whose floor
          is significantly different from their school's median get
          auto-corrected without any user action. The fleet self-
          calibrates as more Hollies come online.
          
          Telemetry now includes 'mic_offset_corr' so the dashboard
          can display the applied correction transparently.

    🎓 SLEEP SUPPRESSED DURING LEARNING.
       Sleep mode was kicking in during the 14-day learning window,
       which both (a) showed rainbow LEDs instead of the charging
       animation, and (b) caused the early-return at line 2868 to
       skip Welford accumulation entirely. Net effect: a Holly that
       went to sleep on day 1 because the room was quiet would still
       complete the 14-day clock with maybe 200 samples instead of
       5000+. Calibration would be unreliable.

       Fix: while roomCalEnabled && !roomCalComplete, the sleep state
       machine is suppressed entirely. Holly stays awake, Welford
       keeps counting, the charging animation renders correctly.
       After learning completes, sleep returns to normal behaviour.

  Changes since 2026.05.01.5075:
    🚀 FAST-CADENCE BOOT WINDOW (first 10 minutes after boot).
       During the first 10 minutes after every boot, Holly runs at
       accelerated POST cadence:
         - Telemetry POST: 15s (was 60s)
         - Heartbeat POST: 30s (was 180s)
       After the boot window expires, cadence reverts to the standard
       60s / 180s — no fleet POST-rate impact in steady state.

       Rationale: operators setting up new Hollies were waiting up to
       3 minutes to see the device appear in the dashboard, and a
       further minute per data point. The fast window covers the full
       5-min mic floor test PLUS a 5-min buffer for setup verification.
       After 10 mins the device is in normal operation and 60s is
       fine. The Apps Script LockService can absorb 14 devices at 15s
       cadence for ~10 mins (roughly 56 POSTs/min peak) — but only
       briefly, hence the strict 10-min sunset.

       The fast window timer starts at boot, NOT at first WiFi connect.
       So a Holly that takes 5 mins to find WiFi only gets 5 mins of
       fast cadence, not 10. Deliberate — boot-window is a wall-clock
       grace period, not a "first 10 mins of useful operation" window.

    📡 IMMEDIATE WIFI CHECK-IN.
       Boot check-in now fires the moment WiFi connects, dropping the
       previous POST_RECONNECT_GRACE_MS (10s) requirement on first boot.
       The grace period was there to let TLS settle after a recovery
       drop — but on first-ever WiFi connect after fresh boot, there's
       nothing to recover from. The grace still applies to subsequent
       reconnects (its original purpose).

       Combined with the fast-cadence window, a brand-new Holly now
       appears in the registry within ~5-15 seconds of WiFi connect,
       and the dashboard sees its first telemetry within another 15s.

    🩺 HEARTBEATS DURING MIC FLOOR TEST.
       Previously, the main loop returned early during micFloorTestActive
       and the device went radio-silent for 5 minutes. From the operator's
       view this looked like "Holly is offline mid-setup" — anxiety-
       inducing and unhelpful.
       
       Now during the test, Holly fires a heartbeat POST every
       HEARTBEAT_INTERVAL_MS (30s during fast window, 180s otherwise)
       so the dashboard sees "alive, calibrating mic floor" throughout.
       The heartbeat payload omits sound dB, so mic floor accuracy is
       unaffected. Test loop still returns early after the heartbeat
       check, preserving the mic-test isolation.

  Changes since 2026.05.01.5074:
    🐛 BUGFIX: set_room_cal_enabled and cancel_learning never fired.
       Bench test on EF3E90 confirmed the dashboard wrote TRUE to the
       registry, Apps Script cleared the cell on the next POST (proof
       it was read), POST returned 200 — but the firmware never logged
       the "REMOTE: LEARNING MODE..." line and learning never started.

       Root cause: in v5074 these handlers were placed inside
       applyRemoteConfig(), which is only called from processRemoteConfig()
       AFTER the config_version guard:

           if(serverVersion==0||serverVersion==nvsConfigVersion)return;
           applyRemoteConfig(resp);   // <-- never reached for one-shots

       Apps Script does NOT bump config_version when a one-shot action
       (reboot, room_test, locate, set_room_cal_enabled, cancel_learning)
       is queued — those are action flags, not config changes. So the
       version check returns early and applyRemoteConfig() never runs.

       reboot/room_test/locate work because they're handled in
       processRemoteConfig() BEFORE the version check, as action flags.
       The two new commands belong in the same place. Three lines moved.

       No Apps Script or dashboard change required — pure firmware fix.

  Changes since 2026.05.01.5073:
    🛑 REMOTE LEARNING CANCEL.
       Adds cancel_learning to the remote config pipe. Hard cancel —
       roomCalEnabled=false, roomCalComplete=false, Welford counters
       wiped clean. Active thresholds revert to band defaults via the
       existing resolveActiveThresholds() path.

       Use case: started learning by accident, bad data window (fire
       alarm, classroom painted, atypical week), or decision change
       to keep a device on band defaults. Without cancel, the only
       way out was waiting 14 days or USB-reflashing — neither
       acceptable for a managed product.

       Logs a new "learning_cancelled" milestone event distinct from
       learning_started/completed/reset, so the timeline shows when
       calibration was deliberately abandoned.

       The dashboard hides Start Learning and shows Cancel Learning
       on devices currently in learning state, so the operator never
       sees both at once on the same device.

  Changes since 2026.05.01.5072:
    🎓 REMOTE LEARNING MODE COMMAND.
       Adds set_room_cal_enabled to the remote config pipe. When the
       admin dashboard ticks "Set Room Cal Enabled" on a device's
       registry row, the next telemetry POST receives the command in
       the config response, the firmware resets the calibration state
       cleanly, and the 14-day Welford learning window starts fresh.

       Behaviour: ALWAYS resets cleanly. Whether the device has never
       run learning, is mid-learning, or has already completed
       learning, the command zeros the Welford counters, clears
       rc_complete, sets rc_enabled=true, and starts a new 14-day
       window from now. Use case: re-running learning after a room
       layout change, equipment installation, year-group migration,
       or any other event that invalidates prior calibration.

       The admin dashboard fires set_room_cal_enabled = true; Apps
       Script clears the registry cell after one delivery (same
       pattern as reboot_requested / room_test_requested). The
       firmware logs a new "learning_reset" milestone event whenever
       this fires, distinct from learning_started, so the timeline
       distinguishes "natural first learning" from "admin override".

       This complements (does not replace) the v5072 boot-time auto-
       activation. That handles the silent retrofit case where mic
       floor is valid but rc_enabled was never set. This handles the
       deliberate operator action.

  Changes since 2026.04.30.5071:
    🚨 STABILITY: HTTP_TIMEOUT_MS dropped 30000 → 12000.
       Root cause: 14-device Lyppard fleet hit a synchronised watchdog
       storm on morning of 1 May 2026. Every device showed task_watchdog
       resets clustered around 07:09 — 14 Hollies all rebooted within a
       3-minute window. Apps Script doPost holds a 30s LockService.waitLock
       per request; with 14 devices POSTing every 60s, queue depth grew
       unbounded as soon as one POST exceeded ~4s exec time. Devices then
       blocked in postToSheets() for up to 30s (HTTP_TIMEOUT_MS), but the
       hardware watchdog (15s) panicked first — every time. Reset, rejoin
       the queue, repeat. Self-perpetuating.

       The wdt_reset() either side of http.POST() doesn't help because
       the call blocks WITHOUT yielding for the full timeout. The fix
       is structural: HTTP timeout MUST be lower than watchdog timeout.

       12s leaves 3s headroom under the 15s watchdog. Apps Script
       responses normally land in 1-3s when the queue is healthy; if
       a request can't complete in 12s, the queue is overloaded and
       failing the POST and trying again next cycle is the right
       outcome — not blocking until the watchdog fires.

    🌊 STABILITY: Boot-time POST jitter (0-30s).
       The 07:09 stampede was caused by 14 devices all completing their
       boot check-in at roughly the same wall-clock minute, then ticking
       at exactly 60s intervals from there. Apps Script processed them
       serially via LockService — but they all arrived at the same
       second of every minute. Queue depth = fleet size, every minute.

       Fix: at boot check-in, set lastPostTime backwards by a random
       0-30000ms offset. The next periodic POST then fires anywhere
       between 30s and 60s after boot check-in instead of exactly 60s
       later. Across 14 devices this naturally spreads them across the
       interval, dropping peak Apps Script queue depth from 14 to ~1.

       This is one-shot per boot — no per-POST jitter, no ongoing
       randomness in the cadence. Simple, deterministic from the second
       POST onwards.

    📚 LEARNING MODE FIX: roomCalEnabled now activates after baseline.
       Root cause: roomCalEnabled was declared false at line 810, read
       from NVS with default false at line 2175, and NEVER WRITTEN TRUE
       anywhere in the firmware. The 14-day Welford learning machinery
       was fully wired but the gate was permanently shut. Every Lyppard
       Holly completed its 5-minute mic floor test and then sat on
       band-default thresholds forever, while reporting cal_status =
       "disabled" to the dashboard. Dead code for weeks before anyone
       noticed.

       Two fixes:
       1. finalizeMicFloorTest() success path now sets roomCalEnabled =
          true and persists rc_enabled = true to NVS. Future Hollies
          will activate learning the moment their baseline completes.
       2. setup() boot-time auto-activation: if mic_floor_valid &&
          !rc_enabled && !rc_complete, set roomCalEnabled = true and
          persist. This retro-activates learning on the 14 already-
          deployed Lyppard Hollies — they all have valid mic floors
          but rc_enabled was never set. After v5073 OTA, they all flip
          to "learning" state automatically on first boot.

       The existing main-loop logic (line 2557) sees roomCalEnabled
       true with no roomCalStartEpoch and starts the calibration —
       which fires the learning_started event automatically. No new
       event-posting code needed; the existing pipeline takes over.

       Welford uses live avgDB only, never mic floor. So devices with
       unusual mic floors (Lavender's 75dB false reading, the three
       devices clamped to 20dB) will still produce CORRECT room-
       specific thresholds after 14 days of Welford. Mic floor only
       affects the starting band-default thresholds before learning
       completes — and those will be replaced by the calibrated values.

  Changes since 2026.04.27.5060:
    📔 Milestone Event Logging
       Holly now POSTs explicit milestone events to a new "Events" tab
       in the Apps Script backend. Events are fire-and-forget (3s
       timeout, no retry) — main loop never blocks. Best-effort audit
       trail; if a POST fails, normal telemetry still updates the
       registry's current state. Events surfaced as a timeline in the
       admin dashboard device panel.

       Event types logged:
         - boot                  every successful boot, value=fw,
                                 detail=reset_reason
         - mic_floor_started     5-min test begins
         - mic_floor_completed   value=dB, detail=samples
         - mic_floor_failed      detail=reason
         - learning_started      14-day Welford begins
         - learning_completed    value=mean dB, detail=sd/min/max
         - ota_completed         set on next boot after OTA flash,
                                 value=new_fw, detail=previous_fw

       Apps Script: requires v45 or later (handles action=device_event,
       creates Events tab on first POST).
       Admin dashboard: shows History timeline in device info panel.
       Backend deployment MUST precede firmware deployment.

  Changes since 2026.04.27.5050:
    🚀 OTA test of the lwIP race fix — pure version bump (5050 → 5060,
       skipping intermediate numbers for a fresh-feeling milestone).
       No code changes other than the version string.

       What this proves: a v5050 device can pull and flash this binary
       OTA without:
         - Watchdog timeout during chunked write (fixed in v5046)
         - Stack canary trip in chunk buffer (fixed in v5048)
         - lwIP udp_new_ip_type assert on post-flash boot (fixed in v5050)

       If a v5050 device successfully pulls v5060, restarts cleanly,
       posts a v5060 banner, and then survives 5+ reboots without a
       single random crash — the OTA pipeline is production-ready and
       the lwIP race is genuinely solved.

       Carson reminds Dan: don't celebrate until 30+ minutes of clean
       runtime AND multiple plug-cycles without incident.

  Changes since 2026.04.27.5049:
    🚨 ROOT-CAUSE FIX: random crash on plug-in / post-OTA boot.
       Symptom: lwIP assert "Required to lock TCPIP core functionality"
       in udp_new_ip_type, sometimes immediately on plug-in, sometimes
       after several minutes, sometimes never. Same backtrace every time.

       Root cause: WiFi.onEvent() callback was calling WiFi.localIP()
       and Serial.printf() directly from inside the event handler. That
       callback runs on the WiFi/lwIP task — calling network functions
       from there races against the stack's own internal locking. Most
       of the time it works; intermittently it asserts and crashes.

       Fix (v5050 pattern): event handler now ONLY sets primitive volatile
       flags (wifiGotIpFlag, wifiDisconnectFlag, wifiDisconnectReason).
       Main loop() drains the flags at the top of every iteration and
       does the actual logging / network work in main task context where
       the lwIP lock is already held appropriately. Standard ESP32
       Arduino pattern.

    🛡️ DEFENCE IN DEPTH: OTA boot-check guard.
       Added two new gates to otaBootCheck():
         1. 30-second boot grace — refuses to run OTA in the first 30s
            after boot, giving lwIP time to settle before HTTPS work.
         2. IP-ready check — refuses to run if WiFi.localIP() is
            still 0.0.0.0 (briefly possible between WL_CONNECTED and
            DHCP completing).

       These shouldn't be strictly necessary now the event handler is
       fixed, but they stop OTA from being the trigger if any other
       race remains. Cost: first OTA check fires 30s later. Acceptable.

    📋 If a v5050 device still asserts udp_new_ip_type, the cause is
       elsewhere (probably postToSheets() or BME280 read on a busy
       stack). But this fix removes the obvious offender.

  Changes since 2026.04.27.5048:
    🚀 OTA pipeline test — third time lucky. Pure version bump, no code
       changes. v5048 device should pull this binary using the heap-allocated
       chunk buffer, log progress every 64KB, complete the flash, restart,
       and report v5049 in the post-WiFi banner.

       This is the proof: a real v1.28MB binary transferred over HTTPS,
       written to flash in chunks, with NVS preserved, no watchdog crash,
       no stack overflow. If we see the v5049 banner — OTA is production-
       ready and the pilot has a remote update path.

  Changes since 2026.04.27.5047:
    🚨 CRITICAL FIX: OTA chunk buffer stack overflow.
       v5046 introduced chunked OTA writes to fix the watchdog timeout,
       but the 4KB chunk buffer was stack-allocated:
         uint8_t buf[CHUNK_SIZE];   // 4096 bytes on the stack
       The Arduino loopTask stack is 8KB by default. The function is
       called deep in the call chain (loop → otaBootCheck →
       otaDownloadAndFlash) where HTTPClient/WiFiClientSecure already
       consume most of it. Adding 4KB on top blew the stack canary —
       resulting in:
         "Guru Meditation Error: Stack canary watchpoint triggered"
       followed by panic-reset bootloop.

       Fixed by heap-allocating the buffer with malloc(4096) and freeing
       on every exit path. Heap had ~170KB free at OTA time so this is
       safe. malloc-failure path also handled cleanly.

    📋 Mea culpa: should have caught this in v5046 review. Stack-allocated
       buffers >1KB are a known anti-pattern on ESP32 Arduino. Carson
       agrees with himself that this was avoidable.

  Changes since 2026.04.27.5046:
    🚀 OTA pipeline final test — v5047 exists purely to be pulled OTA
       by a v5046 device, proving the chunked-write fix works end-to-end.
       No code changes other than the version string. If a v5046 device
       sees this manifest, it should:
         1. Fetch manifest, parse version 2026.04.27.5047
         2. Compare versions, decide to update
         3. GET binary (HTTP 200, content-length ~1.28MB)
         4. Update.begin() OK
         5. Stream 4KB chunks, log progress every 64KB
         6. Update.end() OK, restart
         7. Boot at v5047, post-WiFi banner confirms
       If we see "OTA: Update complete" followed by a v5047 banner,
       the OTA pipeline is production-ready.

  Changes since 2026.04.27.5045:
    🚨 CRITICAL FIX: OTA writeStream watchdog crash.
       v5045 confirmed end-to-end OTA pipeline works through manifest
       fetch, version compare, GET request, content-length retrieval,
       and Update.begin() — all good. Then died inside
       Update.writeStream(*h.getStreamPtr()) because it's a single
       blocking call that reads the entire 1.28MB binary from HTTPS
       to flash without yielding. Took ~22 seconds. Watchdog is 15s.
       Bootloop ensued.

       Fixed by replacing writeStream with a manual chunked read/write
       loop: 4KB at a time, esp_task_wdt_reset() every chunk, plus
       progress logging every 64KB and a 10s stall detector. Total
       flash time is similar; no watchdog trip; you get visible
       progress in serial.

       This is the bug that would have ended the pilot the first
       time a Holly tried to OTA update from a slow classroom WiFi.
       Better to find it on the bench than at Lyppard.

    📋 Bootloop recovery note: if a Holly is stuck in OTA bootloop,
       update manifest.json to a version equal-to or older-than the
       device's current version. Device will see "up to date" and
       skip the download. Then USB-flash the fixed firmware.

  Changes since 2026.04.27.5044:
    🐛 Fixed empty Device ID in post-WiFi banner. v5044 banner showed
       "Device ID:  | IP: 10.254.4.121" — empty because buildDeviceId()
       was being called AFTER connectToWiFi() returned. Moved the call
       to right after WiFi.begin() so MAC is available, deviceId is
       populated, and the banner shows the correct ID.

    🚀 OTA pipeline test #2 — first release that proves the full
       download + flash path end-to-end. v5044 device should pull this
       binary, flash it, restart, and report v5045. The OTA-DBG lines
       will trace every step: GET, content-length, Update.begin,
       writeStream bytes, Update.end. If anything fails, we'll see
       exactly which line.

  Changes since 2026.04.27.5043:
    📋 Banner: added a SECOND version banner print right after WiFi
       connects in setup(). The original banner at the very top of
       setup() prints within ~50ms of boot, before most serial monitors
       have time to reconnect — which is why Dan was never seeing it.
       The new post-WiFi banner always lands well after the monitor has
       caught up, guaranteeing a visible version stamp + device ID + IP
       in every captured log.

    🔍 OTA: full diagnostic logging added throughout otaFetchManifest()
       and otaDownloadAndFlash(). Every return path now logs a reason.
       Every key value (HTTP code, content-length, bytes written,
       parsed version + URL) is printed.

       Why: device on v5041a was failing OTA silently — only "boot check
       (1/3)" appeared in serial, no clue what was failing. Manifest fetched
       fine in browser, .bin URL downloaded fine in browser, but the device
       never updated. Suspected redirect handling on WiFiClientSecure or
       Update.begin() rejecting content-length. Need verbose serial to find
       out which.

       Once v5044 is on the device, watch serial through a full OTA cycle
       to a v5045 release. The log will say exactly where any failure is.

    📋 Banner check: confirmed banner code at setup() (around line 1751)
       is intact and prints FW_VERSION on every boot. If you don't see
       it in serial, it's because serial monitor connected after boot
       — not because banner is missing.

  Changes since 2026.04.27.5042:
    🚀 OTA pipeline test release — second release in 24 hours to prove
       the end-to-end deployment path before pilot kickoff. No functional
       changes. Pure version bump. If a v5042 device sees this manifest,
       it should download, flash, restart, and report v5043 with NVS
       (school registration, mic floor, calibration, timetable) intact.

  Changes since 2026.04.20.5041a:
    📋 OTA: added "up to date" diagnostic log line on both boot check
       and daily check paths. Previously, when device version matched
       manifest version, the function returned silently — leaving the
       only evidence of a successful manifest fetch as the *absence*
       of a (2/3) retry. Now logs:
         "  OTA: up to date (manifest=<version>)"
       Test release: prove the OTA pipeline end-to-end before pilot.

  Changes since 2026.04.20.5041:
    📶 WiFi-only reconnect page: added network scan dropdown (auto-scans
       on page load, sorted by signal strength, shows lock icon + RSSI),
       and a show/hide password toggle (eye icon next to password field).
       Teachers no longer have to type the SSID from memory.

  Changes since 2026.04.20.5040:
    🔌 WiFi-only reset now actually does what it says. v5040 wiped the
       WiFi creds but then dropped the user into the full 8-screen setup
       wizard — no different from a factory reset from the teacher's
       perspective. Fixed in three places:
       • performWifiReset() now sets a `wifi_only` NVS flag before reboot
         (and fixes the wrong-key bug in v5040 — was `wifi_pass`, now `wifi_pw`
         matching what the portal actually uses)
       • setup() reads the flag on boot and, if set, launches the captive
         portal in "wifi-only mode" alongside the existing !setupDone path
       • CaptivePortal::begin() accepts a new wifiOnlyMode bool param;
         when true, the root route serves a small inline HTML page
         identifying the Holly by name + classroom, with just SSID and
         password inputs. On successful /connect, portal saves creds,
         clears the `wifi_only` flag, and reboots — NVS school reg,
         timetable, calibration, etc. all preserved.

  Changes since 2026.04.20.5039:
    🔘 Two-tier button reset — teachers can now recover Hollys without
       dashboard access. Hold the physical button:
         • 0-1s   = Holly Hop start/exit (existing, unchanged)
         • 10-20s = whole ring glows AMBER — release to reset WiFi only
                    (preserves school registration, timetable, calibration)
         • 20s+   = whole ring glows RED — release to FACTORY RESET
                    (wipes EVERYTHING — use only when moving devices between
                    schools)
       Colour-coded visual feedback means teachers see exactly what's
       about to happen and release at the right threshold.
       Release < 10s = cancel (no reset, Holly Hop behaviour applies).
       On execute: 3x flash in the relevant colour, then reboot. Holly
       comes up, fails to connect (creds cleared), drops into captive
       portal on WiFi/factory reset paths.
       Brightness capped at 120/255 so laptop-USB power is sufficient
       during testing (same cap as Locate mode).

  Changes since 2026.04.20.5038a:
    🕓 Afternoon break support — optional second break window between
       lunch end and day end, used by schools with an afternoon break
       (common in KS1, rare in KS2). New fields in the config pipe:
       `set_pmbreak_start` and `set_pmbreak_end`.
       Behaviour: If both fields are blank/unset, afternoon break is
       disabled and the school day runs straight from lunch end to day
       end (identical to pre-v5039 behaviour). If both are populated
       with valid times between lunch end and day end, the window is
       honoured as a BREAK state (LEDs off, rewards paused).
       Unlike morning break/lunch, pmbreak accepts blank strings via
       a new applyTimeOrBlank helper — other slots still reject empty
       strings as invalid to prevent accidental clearing.

  Changes since 2026.04.20.5038:
    💡 Locate mode rewritten as a "searchlight" sweep — a 16-LED comet
       trail rotates around the ring once every 800ms, alternating
       magenta/cyan on each revolution. Brightness capped at 120/255
       (~47%). Peak current ~800 mA, down from ~5 A with the full-ring
       strobe. Works on laptop USB power, which was browning out the
       ESP32 and causing a silent reboot every time Locate fired.
       Visually more distinctive anyway — sweep motion catches the eye
       where the strobe was just flashing the whole fixture.
    🐛 Locate crash fix — every time a "locate:true" config arrived,
       Holly crashed mid-POST during the Serial.println of the
       notification. Observed repeatedly in the field.
       Root cause: processRemoteConfig() was doing work (Serial output,
       flag-setting with side effects, etc.) while still inside
       postToSheets() — a deep stack with HTTP/TLS/WiFi buffers all
       allocated. With heap already low (~50KB min), any extra stack
       cost or allocation tipped things over.
       Fix: processRemoteConfig() now ONLY sets flags. All verbose
       Serial output and action handling (locate, locate_cancel,
       reboot, config-version announce) moved to the main loop, which
       runs on a clean stack with idle WiFi.

  Changes since 2026.04.20.5036:
    🔍 "Find My Holly" — admin/staff can trigger a 60-second rapid
       magenta/cyan flash at full brightness to physically locate a device
       in a school. Unmistakable against every other Holly state.
       • Triggered by config key `locate:true` (auto-clears server-side)
       • Cancellable via dashboard (`locate_cancel:true`) or a physical
         button press on the device itself
       • Blocks sleep, rewards and Holly Hop transitions for the duration
       • LED brightness saved/restored on exit so user-configured dimming
         isn't clobbered
       • `getCurrentLEDState()` reports "Locating" so the dashboard shows
         it in Last Seen / Zone columns

  Changes since 2026.04.18.5035:
    🔍 "Find My Holly" — admin/staff can trigger a 60-second rapid
       magenta/cyan flash at full brightness to physically locate a device
       in a school. Unmistakable against every other Holly state.
       • Triggered by config key `locate:true` (auto-clears server-side)
       • Cancellable via dashboard (`locate_cancel:true`) or a physical
         button press on the device itself
       • Blocks sleep, rewards and Holly Hop transitions for the duration
       • LED brightness saved/restored on exit so user-configured dimming
         isn't clobbered
       • `getCurrentLEDState()` reports "Locating" so the dashboard shows
         it in Last Seen / Zone columns

  Changes since 2026.04.18.5035:
    🐛 Sleep-mode silent-offline bugfix — previously, the POST scheduler was
       gated on `!inSleepMode`, meaning Holly stopped ALL communication with
       the backend whenever she went to sleep in a quiet classroom (i.e. every
       break, every lunchtime, every empty-room period). The admin dashboard
       would then flag her offline, and remote config couldn't be pushed
       until she woke up. Now sleep only downgrades the payload to a heartbeat
       — the communication pipe stays open and health data keeps flowing.
    📶 Lesson-cadence telemetry (60s) is reserved for "awake + in-lesson"
       only; asleep-in-lesson falls back to heartbeat cadence (3min), which
       is correct since there's nothing meaningful to report when no-one's
       talking anyway.

  Changes since 2026.04.16.5034:
    💓 Heartbeat check-in rewrite — Holly now checks in on a predictable
       cadence regardless of timetable state, so the admin dashboard always
       has fresh device health data and config pushes propagate quickly.

       New POST cadence:
         - Lessons / timetable disabled : 60 seconds (full telemetry)
         - School open, out of lesson   : 3 minutes  (heartbeat only)
         - School closed / dormant      : 3 minutes  (heartbeat only)

       Two distinct payload types now:
         - Telemetry POST: full payload (sound_dB, voice_percent, LED
           state, zone data + health). Written to school data tabs for
           reporting. Only fires during lessons or when useTimetable=false.
         - Heartbeat POST: health payload ONLY (RSSI, uptime, firmware,
           errors, mic floor, temp/humidity + confirmed_version). No
           sound data is ever logged outside lesson time.

       Both payload types pull config on the response, so admin changes
       propagate within 60s-3min depending on state. Every POST is a
       check-in, but not every POST carries telemetry.

       LED behaviour clarified:
         - Lesson OR timetable disabled -> active traffic light
         - School open, out of lesson   -> LEDs OFF (strip cleared)
         - School closed                -> cyan pulse (existing dormant)

       Rationale: previous build only POSTed during ws=LESSON. On weekends,
       evenings, and when timetable was accidentally left enabled outside
       school hours, Holly went completely silent to the backend. Dashboard
       showed stale "last seen" values for hours or days. Developers also
       couldn't test firmware changes outside lesson hours because Holly
       wouldn't phone home to confirm which build was running.

       Implementation notes:
         - New postHeartbeat() function — lean payload, ~800 bytes vs 3KB
         - LESSON_INTERVAL_MS (60000) and HEARTBEAT_INTERVAL_MS (180000)
           replace the single postInterval
         - POST decision now lives OUTSIDE the ws==LESSON branch
         - Double-POST guard preserved (hop/reward events take priority)
         - Boot check-in: fires ONCE per boot as soon as WiFi is up +
           post-reconnect grace has elapsed. Previously only fired on
           first-boot-ever (cv==0). Now also fires on reboots / crash
           recovery / dev flash cycles. Payload type is chosen based on
           state: first-boot = telemetry (registration), reboot in lesson
           or timetable-disabled = telemetry, reboot out-of-hours =
           heartbeat. Developers see check-in within ~15s of flash.

  Changes since 2026.04.16.5030:
    📡 Wi-Fi reconnect logic rewritten — cooperates with setAutoReconnect
       instead of fighting it. v5030's progressive backoff is kept, but
       the reconnect mechanism itself is now gentler.

       Root cause of the change: the previous pattern was
           WiFi.disconnect(false);
           WiFi.begin();
       every retry cycle. With WiFi.setAutoReconnect(true) enabled, the
       ESP32 is already trying to heal the connection in the background.
       Forcibly tearing the radio down and restarting it interrupts the
       healing attempt and restarts from scratch — on a marginal AP this
       can trap the device in a loop of "almost recover → force disconnect
       → start again". That's likely a contributor to the RSSI swings
       and repeated disconnects we've been seeing in the field.

       Four changes:
       1. Replaced disconnect+begin with WiFi.reconnect(). This is a
          gentler nudge that cooperates with the auto-reconnect state
          machine rather than resetting it.
       2. Added wl_status_t state-change tracking — we now log WiFi
          status transitions (IDLE/DISCONNECTED/CONNECTION_LOST etc.)
          rather than just "not connected", giving much better diagnostics
          on exactly what state the radio is in when it flaps.
       3. Kept v5030's progressive backoff (10s -> 20s -> 40s -> 60s)
          but first retry is now deferred by one WIFI_RETRY_MS window
          to give auto-reconnect a chance to fix things on its own before
          we intervene at all.
       4. First "nudge" call to WiFi.reconnect() is skipped entirely if
          the status is WL_IDLE_STATUS or WL_DISCONNECTED AND less than
          WIFI_FIRST_RETRY_GRACE_MS has elapsed since disconnect — this
          is the "let auto-reconnect heal it first" grace window.

  Changes since 2026.04.16.5029:
    🐛 Fixed watchdog crash on mid-transaction WiFi disconnect during 302
       redirect follow. Root cause confirmed from v5029 diagnostic log:
       heap is stable (~170KB, no drift); problem is postToSheets() blocking
       in http2.GET() inside the 302 redirect when WiFi drops mid-call.
       The 30s HTTP timeout is 2x the 15s watchdog window, so the GET
       blocks past the watchdog and the device resets.

       Five defensive fixes:
       1. RSSI gate — if WiFi.RSSI() is worse than RSSI_POST_GATE_DBM
          (-88 dBm default), skip postToSheets() entirely. Signal this
          weak means TLS handshake will likely block past watchdog.
       2. Post-reconnect grace period — after a WiFi reconnect, wait
          POST_RECONNECT_GRACE_MS (10s) before allowing postToSheets().
          Lets DHCP/DNS/TLS stack stabilise before we hammer it.
       3. Shorter redirect-GET timeout — the 302 redirect response from
          Google is small and cacheable, doesn't need 30s. Cut to 8s via
          REDIRECT_GET_TIMEOUT_MS. Leaves headroom under the 15s watchdog
          even if the main POST also consumed time.
       4. Mid-transaction WiFi check — verify WiFi.status()==WL_CONNECTED
          between the POST response and the redirect GET. If WiFi dropped,
          abort cleanly instead of blocking on a dead connection.
       5. Progressive WiFi reconnect backoff — replaced fixed 10s retry
          with 10s → 20s → 40s → 60s cap. Constant 10s hammering on a
          marginal AP contributes to the instability rather than helping.

  Changes since 2026.04.15.5028:
    🐛 Fixed 30-minute watchdog reset cycle — multiple defensive hardenings:
       1. OTA interval changed from 1 MINUTE to 1 HOUR (was hammering GitHub
          with TLS handshakes every 60s, fragmenting heap over ~30 min).
          The window-gated daily check (20:00-21:00) still fires once/day.
       2. Added esp_task_wdt_reset() inside OTA manifest fetch inner loops —
          TLS handshakes on marginal WiFi can block longer than 15s.
       3. Added esp_task_wdt_reset() inside the 302-redirect GET path in
          postToSheets() — this is a second blocking HTTP call that could
          exceed the watchdog alone.
       4. Added esp_task_wdt_reset() at the top of renderSleepingRainbow()
          and breathingBlue() — loop-heavy LED routines that run for
          extended periods during quiet/break windows.
       5. Added esp_task_wdt_reset() in updateMicFloorTest() loop and
          finalizeMicFloorTest() — the 150-element array scan plus
          preferences.putFloat() calls were not watchdog-safe.
       6. Added heap + uptime diagnostic line every 5 seconds to serial
          debug — helps identify memory leaks/fragmentation going forward.
       7. Added defensive WiFi status check before EVERY postToSheets() —
          prevents blocking HTTP calls when WiFi is marginally disconnected.

  Changes since 2026.04.14.5027:
    🐛 Fixed watchdog reset caused by double-POST in single loop iteration
       Root cause: When rewardLogPending or hopLogPending was true, both the
       deferred event POST and the periodic POST fired back-to-back in the
       same postInterval tick. Two sequential blocking HTTP round-trips
       (each up to 30s) exceeded the 15s watchdog timeout.
       Fix: Only ONE POST per loop iteration. Deferred event POST takes
       priority — periodic POST defers to the next interval cycle.
       This also applies to Holly Hop event logging (same pattern).

  Changes since 2026.04.13.5026:
    💡 Fixed LED flicker — five root causes identified and eliminated:
       1. fillStripSmooth() used uint8_t accumulators — integer truncation
          caused colour to get stuck 1 value below target, never settling.
          Fix: float accumulators (smoothR/G/B) with 1.5 snap threshold.
       2. No dirty check — strip.show() fired 60x/sec even when colour was
          unchanged. Fix: only write when uint8_t output actually changes.
       3. LED_SMOOTH_ALPHA was 0.12 (too responsive) — mic noise fed through
          to RGB target causing 1-2 value wobble per sample.
          Fix: reverted to 0.08 for heavier smoothing on ledDB.
       4. Green zone red channel (0-30 range) was coarse enough that 1dB
          changes caused visible 1-step flicker. Mitigated by fixes 1-3.
       5. Zone boundary jitter — ledDB followed mic noise closely enough
          to trigger hysteresis crossings. Mitigated by alpha reduction.
    📡 WiFi reconnect fix — removed WiFi.begin() from disconnect event handler.
       Event handler and main loop retry timer were both calling WiFi.begin(),
       causing "sta is connecting, return error" spam. Now only the main loop
       retry timer handles reconnection (every 10s).
    🚫 No rewards in empty rooms — added REWARD_MIN_DB (30dB) guard.
       avgDB must be above 30dB to prove room is occupied before rewards
       can accumulate. Prevents mic self-noise triggering false rewards.
    🛏️ Sleep timeout changed to 90 seconds (was 5 minutes in v5026).
       90s is long enough to confirm the room is empty but short enough
       that Holly doesn't sit in green for ages after everyone leaves.

  Changes since 2026.04.12.5025:
    🎓 Five-band year group system replacing three-band
    🎓 Friendly year group labels
    🎓 Mixed-year bias
    🎓 Guardrails updated for five bands
    🎓 Thresholds are offset-based from mic floor with 5 dB gaps
    🔧 LED transitions smoothed — exponential RGB blending
    🎤 Mic Floor Baseline — 5-minute silence test for hardware calibration
    🎤 Remote Room Test — admin dashboard trigger via config pipe
    🎤 Mic floor reported to registry
    🔧 Sleep/wake thresholds offset-based from mic floor

  Changes since 2026.04.11.5024:
    🐛 Fixed Holly Hop crash/hang on deactivation
    ⚡ Holly Hop enter/exit is now instant
    🐛 Fixed logQuietRewardEvent() blocking
    🐛 Fixed double http.end() in postToSheets()
    🐛 Fixed postToSheets() error path
    🐛 Fixed readMicDB() 2KB stack allocation
    🐛 Fixed millis() rollover bug
    🐛 Fixed localtime() non-reentrant call
    🐛 Fixed breathingBlue()/standbyCyanPulse() float precision loss
    🧹 Dormant mode health POST refactored
    🧹 Factory reset simplified
    🧹 Standardised DynamicJsonDocument sizes to 3072

  Changes since 2026.04.11.5023:
    ⏱️ HTTP timeout increased to 30s
    ✨ Reward animation: white base with gold sparkles
    🔄 Remote reboot support
    🔧 Fixed Google Apps Script redirect handling
    🎚️ Sensitivity Profile

  Changes since 2026.04.11.5022:
    📊 SEND Enhanced Monitoring

  Changes since 2026.04.10.5021:
    🔋 Battery Charging animation during learning mode
    ✨ Glitter White reward effect

  Changes since 2026.04.10.5020:
    📅 Set Reopen Date

  Changes since 2026.04.10.5019:
    🏫 School Open toggle

  Changes since 2026.04.10.5018:
    📊 Learning Progress percentage

  Changes since 2026.04.10.5017:
    🔌 Remote Timetable Enable/Disable
    🔄 Last Reset Reason
    ⚠️ Last Error tracking

  Changes since 2026.04.10.5016:
    🕐 Remote Timetable
    🌡️ Last Temperature reported

  Changes since 2026.04.09.5015:
    🔧 TWO-WAY CONFIG PIPE
    🔒 Manual Override
    🎓 Remote Year Group Change
    📊 Health Reporting

  Changes since 2026.04.09.5014:
    ✨ Speech-band energy detection

  (Earlier changelog entries omitted for brevity — see v5026)
==============================================
*/
 
// === Core Libraries ===
#include <WiFi.h>
#include "captive_portal.h"
#include <time.h>
#include <driver/i2s.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <arduinoFFT.h>
#include <string.h>
#include <ctype.h>
#include <esp_task_wdt.h>
#include <esp_mac.h>  // v5071: esp_efuse_mac_get_default() for radio-independent MAC read
 
#ifndef ESP_IDF_VERSION_MAJOR
  #define ESP_IDF_VERSION_MAJOR 3
#endif
 
// =====================
//   TUNING SWITCHES
// =====================
#define MIC_DB_OFFSET 93.0f
// v5077: Per-unit mic offset correction. Pushed from Apps Script v49+
// based on per-school median mic floor. Persisted in NVS as 'mic_offset_corr'.
// Applied additively after MIC_DB_OFFSET in readMicDB(). Default 0.0 means
// no correction (this Holly's mic matches the fleet baseline).
static float micOffsetCorrection = 0.0f;
#define LED_SMOOTH_ALPHA 0.08f   // v5027: reverted from 0.12 — reduces mic noise feed-through to LEDs

// === FFT / Voice Detection ===
#define FFT_SAMPLES        512
#define FFT_SAMPLING_FREQ  16000.0
#define VOICE_BIN_LOW   10
#define VOICE_BIN_HIGH  96
 
#define HYS_GA_DEFAULT 0.7f
#define HYS_AD_DEFAULT 0.9f
#define HYS_DR_DEFAULT 1.0f
 
// =====================
//   WATCHDOG & TIMEOUTS
// =====================
#define WDT_TIMEOUT_S          15
// v5072: HTTP_TIMEOUT_MS dropped from 30000 to 12000.
// MUST stay strictly under WDT_TIMEOUT_S * 1000 (15000), otherwise
// http.POST() can block past the watchdog and panic the device.
// 12000 leaves 3s headroom for response handling after the call
// returns. See header comment for full root-cause writeup.
#define HTTP_TIMEOUT_MS        12000
#define OTA_BIN_TIMEOUT_MS     30000

// v5030: Stricter timeouts + gating for marginal-WiFi survival.
// The v5029 diagnostic log proved heap is stable — the real problem is
// postToSheets() blocking past the 15s watchdog when WiFi dies mid-call.
#define REDIRECT_GET_TIMEOUT_MS  8000    // 302 follow is a short response; no need for 30s
#define POST_RECONNECT_GRACE_MS  10000   // ms to wait after WiFi reconnect before posting
#define RSSI_POST_GATE_DBM       -88     // skip POST if signal weaker than this (too likely to block)

// v5031: Give ESP32 auto-reconnect a chance before we intervene manually.
// When WiFi drops, wait this long before the first WiFi.reconnect() nudge.
// Auto-reconnect will usually heal a transient drop within a few seconds;
// jumping in too early interrupts it and restarts from scratch.
#define WIFI_FIRST_RETRY_GRACE_MS 8000   // ms after disconnect before we nudge
 
// =====================
//   ANTI-GAMING REWARD
// =====================
static const unsigned long REWARD_COOLDOWN_MS = 3UL * 60UL * 1000UL;
static const unsigned long SPIKE_PENALTY_MS = 2UL * 60UL * 1000UL;
static const float REWARD_SPIKE_MARGIN_DB = 8.0f;
static const unsigned long MIN_LESSON_BEFORE_REWARD_MS = 2UL * 60UL * 1000UL;
#define REWARD_MIN_DB             30.0f    // v5027: Room must be above this for rewards (proves occupancy)
 
// =====================
//        OTA
// =====================
static const char* FW_VERSION = "2026.05.05.5085";
 
static bool otaBootCheckDone = false;
static uint8_t otaBootRetries = 0;
static const uint8_t OTA_BOOT_MAX_RETRIES = 3;
static unsigned long lastBootOtaAttemptMs = 0;
static const unsigned long OTA_BOOT_RETRY_INTERVAL_MS = 15000;
static unsigned long lastOtaCheckMs = 0;
const unsigned long  OTA_CHECK_INTERVAL_MS = 3600000;   // v5029: 1 HOUR (was 60000/1min — TLS heap fragmentation)
static const char* OTA_MANIFEST_URL =
  "https://raw.githubusercontent.com/chestnutinfrastructure/classroomsounds/refs/heads/main/manifest.json";
static const int OTA_CHECK_START_HOUR = 20;
static const int OTA_CHECK_END_HOUR   = 21;
 
// =====================
//   Identity & Hardware
// =====================
Preferences preferences;
Adafruit_BME280 bme;
float temperature = 0.0;
float humidity    = 0.0;
float pressure    = 0.0;
char hollyName[32]     = "";
char classroomName[32] = "";
char deviceId[16]      = "";
char yearGroupInput[32] = "Y2";
char yearBand[16]       = "Y2Y3";
 
// --- Mic floor baseline (5-minute room test) ---
#define MIC_FLOOR_TEST_DURATION_MS  300000UL   // 5 minutes
#define MIC_FLOOR_SAMPLE_INTERVAL   200
#define MIC_FLOOR_DEFAULT           35.0f
static float   micFloor           = MIC_FLOOR_DEFAULT;
static bool    micFloorValid      = false;
static bool    micFloorTestActive = false;
static bool    micFloorTestRequested = false;
static unsigned long micFloorTestStartMs = 0;
static float   micFloorLow[150];
static unsigned long micFloorSamples = 0;
static char    micFloorDate[20]   = "";

// v5037: Locate mode ("Find My Holly")
// Admin/staff triggers rapid magenta/cyan flash for 60s to physically locate
// a device in a building. Blocks sleep, rewards and hop transitions for the
// duration. Cancellable via physical button press.
#define LOCATE_DURATION_MS       60000UL
#define LOCATE_FLASH_PERIOD_MS   250UL   // 2 Hz alternation (250ms per half-cycle)
static bool    locateActive          = false;
static bool    locateRequested       = false;
static bool    locateCancelRequested = false;  // v5038: deferred cancel
static unsigned long locateStartMs   = 0;

// v5038: Deferred action flags — set by processRemoteConfig, handled by main
// loop on a clean stack. Avoids crashes from doing work inside the HTTP
// response handler while WiFi/TLS buffers are still allocated.
static bool     remoteRebootRequested     = false;
static uint32_t remoteConfigVersionPending = 0;  // non-zero when config was just updated

// v5080: Deferred flags for handlers that previously did real work
// inside processRemoteConfig() — see header changelog. Set in
// processRemoteConfig (no TLS overhead), drained by loop().
static bool  remoteSetRoomCalEnabledRequested = false;
static bool  remoteCancelLearningRequested    = false;
static bool  remoteClearLastErrorRequested    = false;
static bool  remoteMicOffsetUpdateRequested   = false;
static float pendingMicOffsetCorrection       = 0.0f;

// v5082: Defer applyRemoteConfig() to loop() — was previously called
// synchronously inside processRemoteConfig() (which is itself inside
// the postToSheets() TLS stack). Stashing the body for re-parse adds
// ~500 bytes of heap during the deferral window; freed after drain.
static String   pendingConfigBody     = "";
static uint32_t pendingConfigVersion  = 0;
static bool     remoteApplyConfigRequested = false;

#define LED_PIN          26
#define NUM_LEDS         120
#define STATUS_LED       27
#define HOLLY_MODE_BTN   13
#define MIC_SD_PIN   32
#define MIC_WS_PIN   25
#define MIC_SCK_PIN  33
 
// === Thresholds ===
struct Thresholds { float greenMax; float amberMax; float redWarnDb; };
Thresholds ACTIVE_TH = {59, 64, 69};
 
// === Timings & Behaviour ===
#define AVG_SAMPLE_COUNT          75
#define SAMPLE_INTERVAL_MS        200
#define SILENCE_DURATION_MS     90000    // v5027: 90 seconds (was 300000/5min in v5026)
#define SLEEP_OFFSET_DB           5.0f
#define WAKE_OFFSET_DB            7.0f
#define DEBOUNCE_MS               120
#define BOOT_STANDBY_MS          8000
#define LED_FRAME_MS              16
 
// === Quiet Reward Behaviour ===
static unsigned long quietStartMs    = 0;
static unsigned long rewardStartMs   = 0;
static bool          rewardShowing   = false;
const unsigned long  QUIET_REWARD_MS    = 120000;
const unsigned long  REWARD_DURATION_MS = 8000;
static bool          rewardActiveForLog    = false;
static unsigned long rewardLogUntilMs      = 0;
const unsigned long  REWARD_LOG_WINDOW_MS  = 130000;
 
// === Anti-gaming reward state ===
static unsigned long rewardCooldownUntilMs = 0;
static unsigned long penaltyUntilMs        = 0;
static unsigned long lessonStartMs         = 0;
 
// === Device & schedule ===
enum DeviceMode { MODE_NORMAL, MODE_HOLLY_HOP };
DeviceMode deviceMode = MODE_NORMAL;
const unsigned long HOLLY_HOP_DURATION_MS = 30UL * 60UL * 1000UL;
unsigned long hollyHopEndAt = 0;
bool hopPendingExit = false;
bool hopLogPending = false;
char hopLogLabel[16] = "";
bool rewardLogPending = false;
float rewardLogDb = 0.0f;
 
// === Globals ===
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
float dbHistory[AVG_SAMPLE_COUNT];
int currentIndex = 0;
int validSamples = 0;
bool internetConnected = false;
bool inSleepMode = false;
unsigned long silenceStart = 0;
unsigned long lastSampleTime = 0;
static float ledDB = 35.0f;
static bool  hasNewSampleForLed = false;

static float voicePercent = 0.0f;
static double fftVReal[FFT_SAMPLES];
static double fftVImag[FFT_SAMPLES];
static ArduinoFFT<double> FFT = ArduinoFFT<double>(fftVReal, fftVImag, FFT_SAMPLES, FFT_SAMPLING_FREQ);
 
bool useTimetable = true;
static bool lastButtonState = HIGH;
static unsigned long buttonPressStart = 0;
static bool wipeTriggered = false;
// v5040: Two-tier button reset — 10s WiFi-only, 20s full factory.
// While holding, LEDs glow amber (10-20s) or red (20s+) so user sees what's
// about to happen and can release at the right colour. Release in the pink
// zone (< 10s) = Holly Hop start/exit (existing behaviour, no reset).
#define RESET_WIFI_HOLD_MS    10000UL   // 10s  — amber, WiFi-only reset
#define RESET_FACTORY_HOLD_MS 20000UL   // 20s  — red, full factory reset
#define RESET_LED_BRIGHTNESS  120       // laptop-USB-safe, matches locate cap
static bool     resetHoldActive  = false;   // button is held past 10s
static uint8_t  resetHoldZone    = 0;       // 0=none, 1=amber, 2=red
static uint8_t  resetSavedBrightness = 0;
static bool     resetBrightnessStashed = false;
static unsigned long lastToggleAt = 0;
unsigned long lastWifiRetry = 0;
const unsigned long WIFI_RETRY_MS = 10000;
// v5030: Post-reconnect grace period tracking + progressive backoff
static unsigned long wifiReconnectedAtMs = 0;   // millis() when WiFi last came up
static uint8_t wifiRetryBackoffStep = 0;        // 0..3 -> 10s/20s/40s/60s
// v5031: Disconnect-time tracking + state-change logging
static unsigned long wifiDisconnectedAtMs = 0;  // millis() when WiFi last dropped
static wl_status_t lastWifiStatusLogged = WL_IDLE_STATUS;
// v5050: Volatile flags drained by main loop. WiFi events fire on the WiFi/lwIP
// task — calling WiFi.localIP(), Serial.printf, or anything network-related
// from inside the event callback risks the lwIP "Required to lock TCPIP core
// functionality" assert (see backtrace ending in udp_new_ip_type). Pattern is:
// SET FLAGS in events, DO WORK in loop().
static volatile bool wifiGotIpFlag = false;
static volatile bool wifiDisconnectFlag = false;
static volatile uint8_t wifiDisconnectReason = 0;
const char* GOOGLE_SHEETS_URL = "https://script.google.com/macros/s/AKfycbz0EkrgGAsmvoMrEOgei-IA40HUNH_UIf20gD3pCg5FTYFwh7sSXoD54rCR1XGb8_8b/exec";
unsigned long lastPostTime = 0;
// v5035: Dual check-in cadence — telemetry during lessons, heartbeat otherwise.
// Every POST pulls config on the response, so both serve as config check-ins.
static const unsigned long LESSON_INTERVAL_MS    = 60000UL;    // 60s during lessons / timetable disabled
static const unsigned long HEARTBEAT_INTERVAL_MS = 180000UL;   // 3min out-of-hours / dormant
// v5076: Fast-cadence boot window — for the first 10 minutes after boot
// (NOT first 10 mins of WiFi), POST cadence is accelerated for fast
// operator feedback during setup. Reverts to the standard intervals
// above after the window expires. Wall-clock from millis(), not from
// any state change.
static const unsigned long FAST_CADENCE_DURATION_MS    = 600000UL;  // 10 minutes
static const unsigned long FAST_LESSON_INTERVAL_MS     = 15000UL;   // 15s telemetry during boot window
static const unsigned long FAST_HEARTBEAT_INTERVAL_MS  = 30000UL;   // 30s heartbeat during boot window
unsigned long postInterval = LESSON_INTERVAL_MS;               // kept for room-cal sampler compatibility
enum WindowState { OFF, LESSON, ASSEMBLY, BREAK, LUNCH };
enum ColorZone { Z_GREEN, Z_AMBER, Z_DEEP, Z_RED };
ColorZone currentZone = Z_GREEN;
static WindowState lastWs = OFF;
#define TIMETABLE_ENABLED 1
 
// === Room Calibration ===
static bool   roomCalEnabled    = false;
static bool   roomCalComplete   = false;
static time_t roomCalStartEpoch = 0;
static uint32_t calCount = 0;
static double   calMean  = 0.0;
static double   calM2    = 0.0;
static float    calMinDb = 9999.0f;
static float    calMaxDb = -9999.0f;
static Thresholds CAL_TH = {43, 48, 52};
static const uint32_t ROOM_CAL_DAYS    = 14;
static const uint32_t ROOM_CAL_SECONDS = ROOM_CAL_DAYS * 24UL * 60UL * 60UL;
// v5082: Sample-count completion gate. 2000 samples ≈ 33 lesson hours
// ≈ 7 normal school days. See header changelog for rationale.
static const uint32_t ROOM_CAL_MIN_SAMPLES = 2000;
static inline bool hasValidTime(time_t t) { return t > 1700000000; }

// === Remote Config ===
static uint32_t nvsConfigVersion   = 0;
static bool     manualOverride     = false;
static bool     schoolOpen         = true;
static char     reopenDate[12]     = "";
static bool     autoReopened       = false;
static unsigned long bootTimeMs    = 0;

// === Diagnostics ===
static char lastResetReason[32]    = "unknown";
static char lastError[64]          = "";
static unsigned long lastErrorMs   = 0;

// === Sensitivity Profile ===
static char sensitivityProfile[16] = "standard";
static float sensitivityMultiplier = 1.0f;
static float getSensitivityMultiplier(const char* profile) {
  if (!profile) return 1.0f;
  if (strcmp(profile, "quiet_focus") == 0) return 0.90f;
  if (strcmp(profile, "relaxed") == 0)     return 1.10f;
  if (strcmp(profile, "chill") == 0)       return 1.20f;
  return 1.0f;
}

// === SEND Enhanced Monitoring ===
static int      sendCount          = 0;
static uint16_t spikeCount         = 0;
static float    transitionAvgDb    = 0.0f;
static const unsigned long TRANSITION_WINDOW_MS = 5UL * 60UL * 1000UL;
static const float SPIKE_THRESHOLD_DB = 10.0f;
static double   transStartSum     = 0.0;
static uint32_t transStartCount   = 0;
static double   transEndSum       = 0.0;
static uint32_t transEndCount     = 0;
static int      currentLessonEndMins = -1;

static void setLastError(const char* err) {
  strlcpy(lastError, err, sizeof(lastError));
  lastErrorMs = millis();
}

static const char* getResetReasonString() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
    case ESP_RST_POWERON:   return "power_on";
    case ESP_RST_EXT:       return "external_reset";
    case ESP_RST_SW:        return "software_reset";
    case ESP_RST_PANIC:     return "crash_panic";
    case ESP_RST_INT_WDT:   return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:  return "task_watchdog";
    case ESP_RST_WDT:       return "other_watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep_wake";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio_reset";
    default:                return "unknown";
  }
}

const char* WEEKDAY[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
char t_day_start[6]="08:50"; char t_break1_start[6]="10:30"; char t_break1_end[6]="10:45";
char t_lunch_start[6]="12:00"; char t_lunch_end[6]="13:00"; char t_day_end[6]="15:20";
// v5039: Afternoon break — optional. Blank strings mean "no afternoon break at this school".
// getWindowState() skips afternoon break logic when either string is empty or invalid.
char t_pmbreak_start[6]=""; char t_pmbreak_end[6]="";
 
// =====================
//   Prototypes
// =====================
void setupI2SMic();
void connectToWiFi();
void buildDeviceId();
void logToGoogleSheets(float avgDB);
void postHeartbeat();
void logHollyHopEvent(const char* eventLabel);
void logQuietRewardEvent(float avgDB);
static void postToSheets(const char* logLabel, DynamicJsonDocument& doc);
static void postEvent(const char* eventType, const char* value, const char* detail);  // v5071
static void postEventF(const char* eventType, float value, const char* detail);       // v5071
float readMicDB();
void updateRollingAverage(float newVal);
float calculateAverage();
void clearDBHistory();
void updateLEDs(float dbForLeds);
void fillStrip(uint32_t color);
void fillStripSmooth(uint8_t tR, uint8_t tG, uint8_t tB);
void breathingBlue();
void standbyCyanPulse();
uint32_t wheel(byte pos);
void renderSleepingRainbow();
void renderLocateMode();
void endLocateMode();
const char* getDevicePhase();
const char* getWindowStateString();
String getCurrentLEDState();
void printDateTimeDebug(bool showMonitoring = false);
ColorZone applyStickyHysteresis(ColorZone current, float dB);
inline void showPink();
void showQuietReward(unsigned long nowMs);
void showBatteryCharging(float progressPct);
bool isSendClassroom();
int parseTimeToMins(const char* hhmm);
WindowState getWindowState(int wday, int h, int m);
void computeYearBandFromInput(const char* input, char* outBand, size_t outLen);
Thresholds thresholdsForBand(const char* band);
struct Guardrails { float gMin, gMax, aMin, aMax, rMin, rMax; };
Guardrails guardrailsForBand(const char* band);
static inline float clampf(float v, float lo, float hi);
Thresholds normalizeThresholds(Thresholds th);
Thresholds applyGuardrails(Thresholds th, const char* band);
void resetRoomCalibration(time_t nowEpoch);
void updateRoomCalibration(float avgDB);
static inline bool calibrationWindowActive(time_t nowEpoch);
Thresholds computeCalibratedThresholdsFromStats(const char* band);
void finalizeRoomCalibrationIfDue(time_t nowEpoch);
static bool otaShouldCheckNow(const tm& ti);
static void otaDailyCheck();
static void otaBootCheck();
static bool otaFetchManifest(String& outVersion, String& outUrl);
static int  compareVersionTokens(const char* a, const char* b);
static bool otaDownloadAndFlash(const String& binUrl);
static String todayKeyYYYYMMDD(const tm& ti);
inline void enterHollyHop(unsigned long nowMs);
inline void beginExitHollyHop(unsigned long nowMs, const char* why);
void processRemoteConfig(const String& responseBody);
void applyRemoteConfig(DynamicJsonDocument& cfg);
void addHealthPayload(DynamicJsonDocument& doc);
void resolveActiveThresholds();
void updateSpikeDetection(float currentSampleDb, float rollingAvgDb);
void updateTransitionTracking(float avgDB, unsigned long nowMs, int currentMins);
int  getLessonEndMins(int currentMins);
void resetSendLessonState();
void startMicFloorTest();
void updateMicFloorTest();
void finalizeMicFloorTest();
void showMicFloorTestAnimation();
 
// =====================
//   Device ID helper
// =====================
void buildDeviceId() {
  String mac = WiFi.macAddress(); mac.replace(":", "");
  String suffix = mac.substring(mac.length() - 6); suffix.toUpperCase();
  suffix.toCharArray(deviceId, sizeof(deviceId));
}
 
// =====================
//   Year-band helpers (offset-based from mic floor)
// =====================
Thresholds thresholdsForBand(const char* band) {
  float f = micFloor;
  if (!band) return {f+24, f+29, f+34};
  String b = String(band); b.toUpperCase();
  if (b == "EYFS") return {f+22, f+27, f+32};
  if (b == "KS1L") return {f+23, f+28, f+33};
  if (b == "KS1U") return {f+24, f+29, f+34};
  if (b == "KS2L") return {f+25, f+30, f+35};
  if (b == "KS2U") return {f+26, f+31, f+36};
  if (b == "SEND") return {f+28, f+34, f+40};
  return {f+24, f+29, f+34};
}
 
void computeYearBandFromInput(const char* input, char* outBand, size_t outLen) {
  String in = String(input ? input : ""); in.trim();
  String lower = in; lower.toLowerCase();
  if (lower == "nursery" || lower == "reception") { strlcpy(outBand, "EYFS", outLen); return; }
  if (lower == "reception and year 1" || lower == "year 1") { strlcpy(outBand, "KS1L", outLen); return; }
  if (lower == "year 1 and year 2" || lower == "year 2" || lower == "year 2 and year 3" || lower == "year 3") { strlcpy(outBand, "KS1U", outLen); return; }
  if (lower == "year 3 and year 4" || lower == "year 4" || lower == "year 4 and year 5" || lower == "year 5") { strlcpy(outBand, "KS2L", outLen); return; }
  if (lower == "year 5 and year 6" || lower == "year 6") { strlcpy(outBand, "KS2U", outLen); return; }
  String up = in; up.toUpperCase(); up.replace(" ", "");
  if (up.indexOf("SEND") >= 0) { strlcpy(outBand, "SEND", outLen); return; }
  if (up == "EYFS") { strlcpy(outBand, "EYFS", outLen); return; }
  if (up == "KS1L") { strlcpy(outBand, "KS1L", outLen); return; }
  if (up == "KS1U") { strlcpy(outBand, "KS1U", outLen); return; }
  if (up == "KS2L") { strlcpy(outBand, "KS2L", outLen); return; }
  if (up == "KS2U") { strlcpy(outBand, "KS2U", outLen); return; }
  up.replace("YR", "R"); up.replace("RECEPTION", "R"); up.replace("NURSERY", "N"); up.replace("Y", "");
  if (up.length() == 0 || up == "N") { strlcpy(outBand, "EYFS", outLen); return; }
  int maxYear = -1; bool hasReception = false; int start = 0;
  while (true) {
    int comma = up.indexOf(',', start);
    String token = (comma == -1) ? up.substring(start) : up.substring(start, comma); token.trim();
    if (token == "R") { hasReception = true; if (maxYear < 1) maxYear = 0; }
    else if (token.length() > 0 && isDigit(token[0])) { int yr = token.toInt(); if (yr > maxYear) maxYear = yr; }
    if (comma == -1) break; start = comma + 1;
  }
  if (hasReception && maxYear <= 0)  strlcpy(outBand, "EYFS", outLen);
  else if (maxYear <= 1)             strlcpy(outBand, "KS1L", outLen);
  else if (maxYear <= 3)             strlcpy(outBand, "KS1U", outLen);
  else if (maxYear <= 5)             strlcpy(outBand, "KS2L", outLen);
  else                               strlcpy(outBand, "KS2U", outLen);
}
 
bool isSendClassroom() { return (strcmp(yearBand, "SEND") == 0); }
 
Guardrails guardrailsForBand(const char* band) {
  float f = micFloor;
  String b = String(band ? band : ""); b.toUpperCase();
  if (b == "SEND") return { f+18, f+38,  f+24, f+44,  f+30, f+50 };
  if (b == "EYFS") return { f+16, f+32,  f+21, f+37,  f+26, f+42 };
  if (b == "KS1L") return { f+17, f+33,  f+22, f+38,  f+27, f+43 };
  if (b == "KS1U") return { f+18, f+34,  f+23, f+39,  f+28, f+44 };
  if (b == "KS2L") return { f+19, f+35,  f+24, f+40,  f+29, f+45 };
  if (b == "KS2U") return { f+20, f+36,  f+25, f+41,  f+30, f+46 };
  return                   { f+18, f+34,  f+23, f+39,  f+28, f+44 };
}
 
// =====================
//   Mic Floor Baseline Test (v5026)
// =====================
void startMicFloorTest() {
  micFloorTestActive = true; micFloorTestStartMs = millis(); micFloorSamples = 0;
  for (int i = 0; i < 150; i++) micFloorLow[i] = 999.0f;
  Serial.println("  === MIC FLOOR TEST STARTED (5 minutes) ===");
  postEvent("mic_floor_started", "", "");  // v5071: audit trail
}
void updateMicFloorTest() {
  esp_task_wdt_reset();  // v5029: runs during 5-min test
  float db = readMicDB();
  // v5085: Filter range now matches readMicDB()'s clamp range (5..90 dB).
  // v5077 dropped the readMicDB lower clamp from 20 to 5, but this filter
  // was left at 10. Result: valid 5-10 dB readings (clamped but real) were
  // silently rejected by updateMicFloorTest, causing room tests in very
  // quiet environments to fail with "only N samples (need 300)" even when
  // the mic was reporting 5+ samples per second the whole time. Bench
  // EF3E90 reproduced this twice — first test scraped through with 483
  // samples, retry got 204 and failed.
  if (!isfinite(db) || db < 5.0f || db > 90.0f) return;
  micFloorSamples++;
  int maxIdx = 0;
  for (int i = 1; i < 150; i++) { if (micFloorLow[i] > micFloorLow[maxIdx]) maxIdx = i; }
  if (db < micFloorLow[maxIdx]) micFloorLow[maxIdx] = db;
}
void finalizeMicFloorTest() {
  esp_task_wdt_reset();  // v5029: multiple preferences.putX calls can block on flash
  micFloorTestActive = false;
  if (micFloorSamples >= 300) {
    float sum = 0.0f; int count = 0;
    for (int i = 0; i < 150; i++) { if (micFloorLow[i] < 900.0f) { sum += micFloorLow[i]; count++; } }
    if (count >= 50) {
      micFloor = sum / (float)count; micFloorValid = true;
      preferences.putFloat("mic_floor", micFloor); preferences.putBool("mic_floor_ok", true);
      esp_task_wdt_reset();
      struct tm ti;
      if (getLocalTime(&ti)) {
        snprintf(micFloorDate, sizeof(micFloorDate), "%04d-%02d-%02d %02d:%02d",
          ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday, ti.tm_hour, ti.tm_min);
        preferences.putString("mic_floor_date", micFloorDate);
      }
      Serial.printf("  === MIC FLOOR TEST COMPLETE: %.1f dB (%lu samples) ===\n", micFloor, micFloorSamples);
      // v5072: Activate learning mode now that we have a valid baseline.
      // Previously this was never set anywhere — Welford was dead code.
      // The main-loop logic (~line 2557) will detect roomCalEnabled=true
      // with no start epoch and fire the learning_started event itself.
      if (!roomCalEnabled && !roomCalComplete) {
        roomCalEnabled = true;
        preferences.putBool("rc_enabled", true);
        Serial.println("  === LEARNING MODE ACTIVATED ===");
      }
      resolveActiveThresholds();
      // v5071: log completion event
      char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "%.1f", micFloor);
      char dbuf[48]; snprintf(dbuf, sizeof(dbuf), "%lu samples, %d valid bins", micFloorSamples, count);
      postEvent("mic_floor_completed", vbuf, dbuf);
    } else {
      Serial.printf("  === MIC FLOOR TEST FAILED: not enough low samples (%d) ===\n", count);
      // v5071: log failure
      char dbuf[48]; snprintf(dbuf, sizeof(dbuf), "Only %d valid bins (need 50)", count);
      postEvent("mic_floor_failed", "", dbuf);
    }
  } else {
    Serial.printf("  === MIC FLOOR TEST FAILED: only %lu samples ===\n", micFloorSamples);
    // v5071: log failure
    char dbuf[48]; snprintf(dbuf, sizeof(dbuf), "Only %lu samples (need 300)", micFloorSamples);
    postEvent("mic_floor_failed", "", dbuf);
  }
}
void showMicFloorTestAnimation() {
  esp_task_wdt_reset();  // v5029: runs during 5-min test
  unsigned long elapsed = millis() - micFloorTestStartMs;
  float progress = clampf((float)elapsed / (float)MIC_FLOOR_TEST_DURATION_MS, 0.0f, 1.0f);
  float breath = 0.5f + 0.5f * sin((float)millis() / 800.0f);
  uint8_t baseBright = (uint8_t)(30.0f + 40.0f * breath);
  int litLeds = (int)(progress * NUM_LEDS);
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i <= litLeds) strip.setPixelColor(i, strip.Color(baseBright, baseBright, baseBright));
    else strip.setPixelColor(i, strip.Color(5, 5, 8));
  }
  strip.show();
}
 
static inline float clampf(float v, float lo, float hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }
Thresholds normalizeThresholds(Thresholds th) {
  if (th.amberMax < th.greenMax + 2.0f) th.amberMax = th.greenMax + 2.0f;
  if (th.redWarnDb < th.amberMax + 2.0f) th.redWarnDb = th.amberMax + 2.0f;
  return th;
}
Thresholds applyGuardrails(Thresholds th, const char* band) {
  Guardrails gr = guardrailsForBand(band);
  th.greenMax = clampf(th.greenMax, gr.gMin, gr.gMax);
  th.amberMax = clampf(th.amberMax, gr.aMin, gr.aMax);
  th.redWarnDb = clampf(th.redWarnDb, gr.rMin, gr.rMax);
  return normalizeThresholds(th);
}
 
// =====================
//   Room calibration
// =====================
void resetRoomCalibration(time_t nowEpoch) {
  roomCalComplete = false; roomCalStartEpoch = hasValidTime(nowEpoch) ? nowEpoch : 0;
  calCount = 0; calMean = 0.0; calM2 = 0.0; calMinDb = 9999.0f; calMaxDb = -9999.0f;
  CAL_TH = applyGuardrails(thresholdsForBand(yearBand), yearBand);
  preferences.putBool("rc_complete", false); preferences.putLong("rc_start", (long)roomCalStartEpoch);
  preferences.putULong("rc_count", calCount); preferences.putDouble("rc_mean", calMean);
  preferences.putDouble("rc_m2", calM2); preferences.putFloat("rc_min", calMinDb); preferences.putFloat("rc_max", calMaxDb);
  preferences.putFloat("rc_g", CAL_TH.greenMax); preferences.putFloat("rc_a", CAL_TH.amberMax); preferences.putFloat("rc_r", CAL_TH.redWarnDb);
  Serial.println("🧪 Room calibration reset.");
}
void updateRoomCalibration(float avgDB) {
  if (!isfinite(avgDB)) return; calCount++;
  double x = (double)avgDB, delta = x - calMean; calMean += delta / (double)calCount;
  double delta2 = x - calMean; calM2 += delta * delta2;
  if (avgDB < calMinDb) calMinDb = avgDB; if (avgDB > calMaxDb) calMaxDb = avgDB;
}
static inline bool calibrationWindowActive(time_t nowEpoch) {
  if (!roomCalEnabled || roomCalComplete) return false;
  if (!hasValidTime(nowEpoch) || !hasValidTime(roomCalStartEpoch)) return false;
  return (uint32_t)(nowEpoch - roomCalStartEpoch) < ROOM_CAL_SECONDS;
}
Thresholds computeCalibratedThresholdsFromStats(const char* band) {
  if (calCount < 60) return applyGuardrails(thresholdsForBand(band), band);
  double variance = (calCount > 1) ? (calM2 / (double)(calCount - 1)) : 0.0;
  if (variance < 0.0) variance = 0.0;
  double sd = sqrt(variance);
  Thresholds th;
  // v5081: Aspirational green target — 2 dB below learned mean.
  // Floor at 40 dB so quiet rooms aren't given an unachievable bar.
  // Ceiling at (mean - 0.5) so noisy rooms always have at least
  // 0.5 dB of green-zone headroom.
  float greenTarget = (float)(calMean - 2.0);
  float greenCeiling = (float)(calMean - 0.5);
  if (greenCeiling < 40.0f) greenCeiling = 40.0f;  // ensure clamp range is valid
  th.greenMax = clampf(greenTarget, 40.0f, greenCeiling);
  // Amber and red retain SD-based scaling — they detect "louder than
  // typical variability," which is the right semantic for those zones.
  if (sd < 1.0) {
    th.amberMax  = (float)(calMean + 3.0);
    th.redWarnDb = (float)(calMean + 6.0);
  } else {
    th.amberMax  = (float)(calMean + 1.60 * sd);
    th.redWarnDb = (float)(calMean + 2.40 * sd);
  }
  // v5082: normalizeThresholds only — DO NOT apply guardrails to calibrated
  // thresholds. Guardrails clamp green to [micFloor+17..20, micFloor+32..36]
  // depending on band, which silently overrides the v5081 aspirational
  // green target for any room with low micFloor + low learned mean.
  // Calibration is meant to produce room-specific values; clamping them
  // back to band-default ranges defeats the exercise. Band-default early
  // return above (calCount<60) still applies guardrails, which is correct
  // for band defaults.
  return normalizeThresholds(th);
}
void finalizeRoomCalibrationIfDue(time_t nowEpoch) {
  if (!roomCalEnabled || roomCalComplete) return;
  // v5082: Sample-count completion gate replaces the v5081 wall-clock gate.
  // The 14-day timer was wall-clock; Welford only accumulated during
  // ws==LESSON, so a window straddling half-term finalised on a half-baked
  // mean. Now we wait until we've actually accumulated enough lesson data
  // regardless of how long it takes. roomCalStartEpoch is kept for display
  // purposes only.
  if (calCount < ROOM_CAL_MIN_SAMPLES) return;
  CAL_TH = computeCalibratedThresholdsFromStats(yearBand); roomCalComplete = true;
  preferences.putBool("rc_complete", true);
  preferences.putFloat("rc_g", CAL_TH.greenMax); preferences.putFloat("rc_a", CAL_TH.amberMax); preferences.putFloat("rc_r", CAL_TH.redWarnDb);
  preferences.putULong("rc_count", calCount); preferences.putDouble("rc_mean", calMean); preferences.putDouble("rc_m2", calM2);
  preferences.putFloat("rc_min", calMinDb); preferences.putFloat("rc_max", calMaxDb);
  Serial.printf("✅ Room cal COMPLETE. G<=%.1f, A<=%.1f, R<=%.1f, n=%lu\n", CAL_TH.greenMax, CAL_TH.amberMax, CAL_TH.redWarnDb, (unsigned long)calCount);
  ACTIVE_TH = CAL_TH;
  // v5071: log learning completion event
  double variance = (calCount > 1) ? (calM2 / (double)(calCount - 1)) : 0.0;
  if (variance < 0.0) variance = 0.0;
  double sd = sqrt(variance);
  char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "%.1f", (float)calMean);
  char dbuf[80]; snprintf(dbuf, sizeof(dbuf), "SD=%.2f, min=%.1f, max=%.1f, n=%lu",
                          (float)sd, calMinDb, calMaxDb, (unsigned long)calCount);
  postEvent("learning_completed", vbuf, dbuf);
}
 
// =====================
//   Rainbow / Holly Hop / Time / Timetable
// =====================
uint32_t wheel(byte pos) {
  pos = 255 - pos;
  if (pos < 85) return strip.Color(255-pos*3, 0, pos*3);
  else if (pos < 170) { pos -= 85; return strip.Color(0, pos*3, 255-pos*3); }
  else { pos -= 170; return strip.Color(pos*3, 255-pos*3, 0); }
}
void renderSleepingRainbow() {
  esp_task_wdt_reset();  // v5029: defensive — this runs during sleep/Holly Hop for extended periods
  static unsigned long lastUpdate = 0; static uint16_t hueOffset = 0;
  if (millis() - lastUpdate < 16) return; lastUpdate = millis();
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, wheel(((i*256)/NUM_LEDS+hueOffset)&0xFF));
  strip.show(); hueOffset++;
}
// v5037/v5038a: Locate mode — "searchlight" sweep.
// A comet-trail of ~16 LEDs sweeps around the 120-LED ring, rotating
// once every 800ms. The head colour alternates magenta <-> cyan on each
// full revolution, giving a distinctive two-tone sweep that cannot be
// mistaken for any other Holly state (sleep rainbow, zones, reward).
// Brightness capped at 120 (~47% of max) so peak current stays well
// under 1A — fine on laptop USB power. Roughly ~800 mA peak vs the
// ~5A the old full-ring full-brightness strobe was asking for.
static uint8_t locateSavedBrightness = 0;
static bool    locateBrightnessStashed = false;
#define LOCATE_BRIGHTNESS      120       // capped — laptop-USB-safe
#define LOCATE_COMET_LEN       16        // trail length (LEDs)
#define LOCATE_SWEEP_MS        800       // one full revolution
#define LOCATE_FRAME_MS        16        // render every 16ms (~60fps)
void renderLocateMode() {
  esp_task_wdt_reset();
  static unsigned long lastFrame = 0;
  static bool cyanPhase = false;
  static int  lastHead = -1;
  if (!locateBrightnessStashed) {
    locateSavedBrightness = strip.getBrightness();
    strip.setBrightness(LOCATE_BRIGHTNESS);
    locateBrightnessStashed = true;
    lastFrame = 0;
    lastHead = -1;
  }
  unsigned long nowMs = millis();
  if (nowMs - lastFrame < LOCATE_FRAME_MS && lastFrame != 0) return;
  lastFrame = nowMs;
  // Head position based on time, one revolution per LOCATE_SWEEP_MS
  unsigned long elapsedMs = nowMs - locateStartMs;
  int head = (int)((elapsedMs * NUM_LEDS) / LOCATE_SWEEP_MS) % NUM_LEDS;
  // Toggle colour phase each revolution (at head==0 crossing)
  if (head < lastHead) cyanPhase = !cyanPhase;
  lastHead = head;
  uint8_t headR, headG, headB;
  if (cyanPhase) { headR = 0;   headG = 220; headB = 255; }  // cyan
  else           { headR = 255; headG = 0;   headB = 220; }  // magenta
  // Paint whole ring: comet trail fades from head colour -> dark
  for (int i = 0; i < NUM_LEDS; i++) {
    // distance from head going "backwards" around the ring
    int dist = (head - i + NUM_LEDS) % NUM_LEDS;
    if (dist < LOCATE_COMET_LEN) {
      // Linear falloff from head (dist=0, full) to tail (dist=COMET_LEN-1, dim)
      float fade = 1.0f - ((float)dist / (float)LOCATE_COMET_LEN);
      uint8_t r = (uint8_t)((float)headR * fade);
      uint8_t g = (uint8_t)((float)headG * fade);
      uint8_t b = (uint8_t)((float)headB * fade);
      strip.setPixelColor(i, strip.Color(r, g, b));
    } else {
      strip.setPixelColor(i, 0);  // dark
    }
  }
  strip.show();
}
// Call this when locate mode ends (timeout or cancel) to restore LED brightness.
void endLocateMode() {
  if (locateBrightnessStashed) {
    strip.setBrightness(locateSavedBrightness);
    locateBrightnessStashed = false;
  }
}

// v5040: Two-tier reset — LED feedback while user holds button past 10s.
// Renders a solid glow so the user can watch the colour shift and release
// at the right threshold. Called every loop iteration while resetHoldActive.
// Does NOT animate (deliberately static — the colour IS the message).
void renderResetHold(uint8_t zone) {
  if (!resetBrightnessStashed) {
    resetSavedBrightness = strip.getBrightness();
    strip.setBrightness(RESET_LED_BRIGHTNESS);
    resetBrightnessStashed = true;
  }
  uint32_t c = (zone == 2)
    ? strip.Color(255, 0, 0)    // red — factory reset zone
    : strip.Color(255, 140, 0); // amber — WiFi reset zone
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, c);
  strip.show();
}

// Restores LED brightness when the user releases or cancels out of the hold.
// Does NOT repaint — the next main-loop render will take over naturally.
void endResetHold() {
  if (resetBrightnessStashed) {
    strip.setBrightness(resetSavedBrightness);
    resetBrightnessStashed = false;
  }
  resetHoldActive = false;
  resetHoldZone = 0;
}

// Flashes the ring N times in the given colour, then reboots. Blocks — by
// design, because we're about to wipe NVS/WiFi anyway and nothing else
// sensible can happen in parallel.
void flashAndReboot(uint8_t r, uint8_t g, uint8_t b, const char* label) {
  strip.setBrightness(RESET_LED_BRIGHTNESS);
  for (int n = 0; n < 3; n++) {
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(r, g, b));
    strip.show();
    delay(200);
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
    strip.show();
    delay(200);
  }
  Serial.printf("  *** %s CONFIRMED — REBOOTING ***\n", label);
  Serial.flush();
  delay(500);
  ESP.restart();
}

// WiFi-only reset — clear SSID/password from NVS but preserve everything
// else (school registration, timetable, thresholds, calibration). Also sets
// a `wifi_only` flag that setup() reads on next boot to launch the captive
// portal in a reduced "reconnect only" mode — teacher sees a single-screen
// page with just SSID + password, not the full 8-screen setup wizard.
// The flag is cleared by the portal on successful reconnect.
void performWifiReset() {
  Serial.println("  *** WIFI RESET — clearing saved WiFi credentials ***");
  preferences.remove("wifi_ssid");
  preferences.remove("wifi_pw");
  preferences.putBool("wifi_only", true);  // v5041: signal captive portal on next boot
  WiFi.disconnect(true, true);
  delay(200);
  flashAndReboot(255, 140, 0, "WIFI RESET");
}

// Full factory reset — wipe the entire Preferences namespace. Holly forgets
// everything: WiFi, school, Holly name, timetable, calibration, config
// version. On next boot she's a fresh unit that needs full setup via the
// captive portal's multi-screen wizard.
void performFactoryReset() {
  Serial.println("  *** FACTORY RESET — wiping all stored configuration ***");
  preferences.clear();
  WiFi.disconnect(true, true);
  delay(200);
  flashAndReboot(255, 0, 0, "FACTORY RESET");
}

inline void showPink() { for (int i=0;i<NUM_LEDS;i++) strip.setPixelColor(i, strip.Color(255,20,200)); strip.show(); }
inline void enterHollyHop(unsigned long nowMs) {
  if (deviceMode == MODE_HOLLY_HOP) return;
  // v5079: Button is a no-op during the 14-day learning window. Two reasons:
  //   1. Hop renders rainbow which contradicts the "Charging" visual that
  //      staff/admins expect during learning — same architectural mismatch
  //      we fixed for sleep mode in v5077.
  //   2. Hop suppresses Welford accumulation (the cal update runs only in
  //      MODE_NORMAL), so every hop event punches a hole in the calibration
  //      data that can't be recovered.
  // Logged for visibility but no state change.
  if (roomCalEnabled && !roomCalComplete) {
    Serial.println("  Holly Hop suppressed (learning active).");
    postEvent("hop_suppressed", "", "learning window active");
    return;
  }
  deviceMode = MODE_HOLLY_HOP; hollyHopEndAt = nowMs + HOLLY_HOP_DURATION_MS;
  strip.setBrightness(150); hopPendingExit = false; showPink();
  strlcpy(hopLogLabel, "START", sizeof(hopLogLabel)); hopLogPending = true;
  Serial.println("  Holly Hop Mode entered.");
}
inline void beginExitHollyHop(unsigned long nowMs, const char* why) {
  if (deviceMode != MODE_HOLLY_HOP) return;
  deviceMode = MODE_NORMAL; hopPendingExit = false; hollyHopEndAt = 0;
  strip.setBrightness(100);
  strlcpy(hopLogLabel, "END", sizeof(hopLogLabel)); hopLogPending = true;
  Serial.printf("  Holly Hop Mode ended (%s).\n", why);
}
int parseTimeToMins(const char* hhmm) {
  if (!hhmm) return -1; int len = strlen(hhmm); if (len < 4 || len > 5) return -1;
  int h = 0, m = 0; if (sscanf(hhmm, "%d:%d", &h, &m) != 2) return -1;
  if (h<0||h>23||m<0||m>59) return -1; return h*60+m;
}
WindowState getWindowState(int wday, int h, int m) {
  if (!useTimetable || TIMETABLE_ENABLED == 0) return LESSON;
  if (wday < 1 || wday > 5) return OFF;
  int mins = h*60+m;
  int ds=parseTimeToMins(t_day_start),b1s=parseTimeToMins(t_break1_start),b1e=parseTimeToMins(t_break1_end);
  int ls=parseTimeToMins(t_lunch_start),le=parseTimeToMins(t_lunch_end),de=parseTimeToMins(t_day_end);
  // v5039: Afternoon break — only active when both strings parse successfully.
  // A school without an afternoon break leaves these blank and the logic
  // degrades to identical pre-v5039 behaviour.
  int pbs=parseTimeToMins(t_pmbreak_start),pbe=parseTimeToMins(t_pmbreak_end);
  bool pmOk=(pbs>=0)&&(pbe>=0)&&(pbs<pbe)&&(pbs>=le)&&(pbe<=de);
  bool ok=(ds>=0)&&(b1s>=0)&&(b1e>=0)&&(ls>=0)&&(le>=0)&&(de>=0)&&(ds<b1s)&&(b1s<b1e)&&(b1e<=ls)&&(ls<le)&&(le<de);
  if (!ok) { if(mins>=530&&mins<625)return LESSON; if(mins>=625&&mins<645)return ASSEMBLY; if(mins>=645&&mins<660)return BREAK; if(mins>=660&&mins<720)return LESSON; if(mins>=720&&mins<780)return LUNCH; if(mins>=780&&mins<920)return LESSON; return OFF; }
  if(mins<ds||mins>=de)return OFF;
  if(mins<b1s)return LESSON;
  if(mins<b1e)return BREAK;
  if(mins<ls)return LESSON;
  if(mins<le)return LUNCH;
  // After lunch: check afternoon break if configured
  if(pmOk){
    if(mins<pbs)return LESSON;
    if(mins<pbe)return BREAK;
    return LESSON;
  }
  return LESSON;
}
 
// =====================
//   SEND Monitoring
// =====================
int getLessonEndMins(int currentMins) {
  if (!useTimetable || TIMETABLE_ENABLED == 0) return -1;
  int ds=parseTimeToMins(t_day_start),b1s=parseTimeToMins(t_break1_start),b1e=parseTimeToMins(t_break1_end);
  int ls=parseTimeToMins(t_lunch_start),le=parseTimeToMins(t_lunch_end),de=parseTimeToMins(t_day_end);
  int pbs=parseTimeToMins(t_pmbreak_start),pbe=parseTimeToMins(t_pmbreak_end);
  bool pmOk=(pbs>=0)&&(pbe>=0)&&(pbs<pbe)&&(pbs>=le)&&(pbe<=de);
  bool ok=(ds>=0)&&(b1s>=0)&&(b1e>=0)&&(ls>=0)&&(le>=0)&&(de>=0)&&(ds<b1s)&&(b1s<b1e)&&(b1e<=ls)&&(ls<le)&&(le<de);
  if (!ok) { if(currentMins>=530&&currentMins<625)return 625; if(currentMins>=660&&currentMins<720)return 720; if(currentMins>=780&&currentMins<920)return 920; return -1; }
  if(currentMins>=ds&&currentMins<b1s)return b1s;
  if(currentMins>=b1e&&currentMins<ls)return ls;
  // After lunch: lesson ends at either afternoon break start OR day end
  if(pmOk){
    if(currentMins>=le&&currentMins<pbs)return pbs;
    if(currentMins>=pbe&&currentMins<de)return de;
  } else {
    if(currentMins>=le&&currentMins<de)return de;
  }
  return -1;
}
void resetSendLessonState() { spikeCount=0; transStartSum=0.0; transStartCount=0; transEndSum=0.0; transEndCount=0; transitionAvgDb=0.0f; currentLessonEndMins=-1; }
void updateSpikeDetection(float currentSampleDb, float rollingAvgDb) {
  if (sendCount<=0||!isfinite(currentSampleDb)||!isfinite(rollingAvgDb)) return;
  if (currentSampleDb > rollingAvgDb + SPIKE_THRESHOLD_DB) spikeCount++;
}
void updateTransitionTracking(float avgDB, unsigned long nowMs, int currentMins) {
  if (sendCount<=0||!useTimetable||!isfinite(avgDB)) return;
  if (currentLessonEndMins<0) currentLessonEndMins=getLessonEndMins(currentMins);
  unsigned long elapsedMs=nowMs-lessonStartMs;
  if (elapsedMs<TRANSITION_WINDOW_MS) { transStartSum+=(double)avgDB; transStartCount++; }
  if (currentLessonEndMins>0) { int mue=currentLessonEndMins-currentMins; if(mue>=0&&mue<5){transEndSum+=(double)avgDB;transEndCount++;} }
  float sa=(transStartCount>0)?(float)(transStartSum/transStartCount):0.0f;
  float ea=(transEndCount>0)?(float)(transEndSum/transEndCount):0.0f;
  if(transStartCount>0&&transEndCount>0) transitionAvgDb=(sa+ea)/2.0f;
  else if(transStartCount>0) transitionAvgDb=sa; else transitionAvgDb=0.0f;
}
 
// =====================
//   Zone + hysteresis
// =====================
ColorZone applyStickyHysteresis(ColorZone current, float dB) {
  const float hysGA = isSendClassroom() ? 1.2f : HYS_GA_DEFAULT;
  const float hysAD = isSendClassroom() ? 1.5f : HYS_AD_DEFAULT;
  const float hysDR = HYS_DR_DEFAULT;
  switch (current) {
    case Z_GREEN: return (dB > ACTIVE_TH.greenMax + hysGA) ? Z_AMBER : Z_GREEN;
    case Z_AMBER: if(dB<ACTIVE_TH.greenMax-hysGA)return Z_GREEN; if(dB>ACTIVE_TH.amberMax+hysAD)return Z_DEEP; return Z_AMBER;
    case Z_DEEP: if(dB<ACTIVE_TH.amberMax-hysAD)return Z_AMBER; if(dB>ACTIVE_TH.redWarnDb+hysDR)return Z_RED; return Z_DEEP;
    case Z_RED: default: return (dB<ACTIVE_TH.redWarnDb-hysDR)?Z_DEEP:Z_RED;
  }
}
 
// =====================
//   Mic / I2S
// =====================
void setupI2SMic() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8, .dma_buf_len = 512, .use_apll = false, .tx_desc_auto_clear = false, .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {};
#if (ESP_IDF_VERSION_MAJOR >= 4)
  pin_config.mck_io_num = I2S_PIN_NO_CHANGE;
#endif
  pin_config.bck_io_num = MIC_SCK_PIN; pin_config.ws_io_num = MIC_WS_PIN;
  pin_config.data_out_num = I2S_PIN_NO_CHANGE; pin_config.data_in_num = MIC_SD_PIN;
  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}
// v5077: Single-shot dB reading. Used internally by readMicDB() which
// now medians 3 of these to smooth DMA-buffer-alignment jitter. The
// FFT path is preserved here on the third (final) read so voice%
// calculation continues to work.
static float readMicDBSingle(bool runFFT) {
  static int32_t samples[FFT_SAMPLES]; size_t bytesRead = 0;
  i2s_read(I2S_NUM_0, (char*)samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(10));
  int count = bytesRead / sizeof(int32_t); if (count <= 0) return calculateAverage();
  double sum = 0.0;
  for (int i = 0; i < count; i++) {
    float s = (float)samples[i] / 2147483648.0f; sum += (double)s * (double)s;
    if (runFFT && i < FFT_SAMPLES) { fftVReal[i] = (double)s; fftVImag[i] = 0.0; }
  }
  float rms = sqrt(sum / (double)count); if (rms < 1e-9f) rms = 1e-9f;
  // v5077: MIC_DB_OFFSET (93.0) + per-unit correction (default 0.0, set
  // by Apps Script v49+ based on fleet median).
  float db = 20.0f * log10f(rms) + MIC_DB_OFFSET + micOffsetCorrection;
  // v5077: Lower clamp dropped 20.0 → 5.0. Sub-5dB readings are below
  // human hearing threshold and almost certainly electrical garbage.
  // 5dB still preserves real quiet-room measurements that were being
  // squashed to 20.0 in the old design.
  db = constrain(db, 5.0f, 90.0f);
  if (!isfinite(db)) db = calculateAverage();
  if (runFFT && count >= FFT_SAMPLES) {
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward); FFT.compute(FFTDirection::Forward); FFT.complexToMagnitude();
    double voiceEnergy=0.0, totalEnergy=0.0; int halfN=FFT_SAMPLES/2;
    for (int i=1;i<halfN;i++) { double mag2=fftVReal[i]*fftVReal[i]; totalEnergy+=mag2; if(i>=VOICE_BIN_LOW&&i<=VOICE_BIN_HIGH) voiceEnergy+=mag2; }
    voicePercent = (totalEnergy>0.0) ? (float)(voiceEnergy/totalEnergy*100.0) : 0.0f;
    voicePercent = constrain(voicePercent, 0.0f, 100.0f);
  }
  return db;
}

// v5077: Public readMicDB() — medians 3 single-shot reads to smooth out
// DMA-buffer-alignment jitter. The 10ms i2s_read timeout means a single
// call can return a partial buffer (160-320 samples instead of 512),
// producing 5+ dB swings between adjacent reads on the same Holly
// measuring constant noise. Three reads ~10ms apart with median selection
// is the cheapest fix that doesn't require restructuring the I2S DMA.
//
// FFT runs only on the FINAL (3rd) read — this preserves the voice%
// calculation while saving CPU on the first two reads. The whole call
// completes in ~30ms which is well within the SAMPLE_INTERVAL_MS budget.
float readMicDB() {
  float a = readMicDBSingle(false);
  float b = readMicDBSingle(false);
  float c = readMicDBSingle(true);  // FFT runs only on final read
  // Median of 3: the middle value when sorted
  float lo = min(a, min(b, c));
  float hi = max(a, max(b, c));
  return a + b + c - lo - hi;  // mathematical median = sum - min - max
}

// =====================
//   Rolling average
// =====================

void updateRollingAverage(float newVal) {
  if (!isfinite(newVal)) return;
  dbHistory[currentIndex] = newVal; currentIndex = (currentIndex+1) % AVG_SAMPLE_COUNT;
  if (validSamples < AVG_SAMPLE_COUNT) validSamples++;
}
float calculateAverage() {
  int count = (validSamples > 0) ? validSamples : 1; float total = 0;
  for (int i = 0; i < count; i++) total += dbHistory[(currentIndex-1-i+AVG_SAMPLE_COUNT)%AVG_SAMPLE_COUNT];
  return total / (float)count;
}
void clearDBHistory() {
  float initVal = micFloorValid ? micFloor : MIC_FLOOR_DEFAULT;
  for (int i = 0; i < AVG_SAMPLE_COUNT; i++) dbHistory[i] = initVal;
  validSamples = 0;
}
 
// =====================
//   LEDs — v5027 anti-flicker rewrite
// =====================
// Float accumulators for sub-pixel precision (uint8_t truncation caused flicker)
static float smoothR = 0.0f, smoothG = 255.0f, smoothB = 0.0f;
static uint8_t lastShownR = 0, lastShownG = 255, lastShownB = 0;
#define LED_COLOR_SMOOTH  0.12f   // RGB smoothing rate (separate from LED_SMOOTH_ALPHA which smooths dB)

void fillStrip(uint32_t color) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
  strip.show();
}

void fillStripSmooth(uint8_t tR, uint8_t tG, uint8_t tB) {
  // v5027: Float accumulators — eliminates integer truncation flicker
  smoothR += LED_COLOR_SMOOTH * ((float)tR - smoothR);
  smoothG += LED_COLOR_SMOOTH * ((float)tG - smoothG);
  smoothB += LED_COLOR_SMOOTH * ((float)tB - smoothB);
  // Snap to target within 1.5 — prevents stuck-1-below oscillation
  if (fabsf((float)tR - smoothR) < 1.5f) smoothR = (float)tR;
  if (fabsf((float)tG - smoothG) < 1.5f) smoothG = (float)tG;
  if (fabsf((float)tB - smoothB) < 1.5f) smoothB = (float)tB;
  // v5027: Only push to strip if colour actually changed — eliminates unnecessary SPI writes
  uint8_t newR = (uint8_t)smoothR, newG = (uint8_t)smoothG, newB = (uint8_t)smoothB;
  if (newR == lastShownR && newG == lastShownG && newB == lastShownB) return;
  lastShownR = newR; lastShownG = newG; lastShownB = newB;
  fillStrip(strip.Color(newR, newG, newB));
}

void updateLEDs(float dbForLeds) {
  const float gMax=ACTIVE_TH.greenMax, aMax=ACTIVE_TH.amberMax, rWarn=ACTIVE_TH.redWarnDb;
  uint8_t r=0, g=0, b=0;
  switch (currentZone) {
    case Z_GREEN: { float t=clampf((dbForLeds-20.0f)/(gMax-20.0f),0.0f,1.0f); r=(uint8_t)(t*30.0f); g=255; } break;
    case Z_AMBER: { float t=clampf((dbForLeds-gMax)/(aMax-gMax),0.0f,1.0f); r=(uint8_t)(80.0f+t*100.0f); g=(uint8_t)(255.0f-t*155.0f); } break;
    case Z_DEEP:  { float t=clampf((dbForLeds-aMax)/(rWarn-aMax),0.0f,1.0f); r=(uint8_t)(180.0f+t*75.0f); g=(uint8_t)(100.0f-t*100.0f); } break;
    case Z_RED: default: r=255; g=isSendClassroom()?80:0; break;
  }
  fillStripSmooth(r, g, b);
}
 
void breathingBlue() {
  esp_task_wdt_reset();  // v5029: runs for extended periods during assembly/break/lunch
  static unsigned long lastUpdate = 0;
  if (millis()-lastUpdate >= 16) {
    float t=fmodf((float)(millis())*0.001f,3.0f);
    float breath=sinf(t*2.094f); breath=(breath+1.0f)*0.5f; breath=breath*breath*(3.0f-2.0f*breath);
    uint8_t b=(uint8_t)(30.0f+breath*225.0f);
    for(int i=0;i<NUM_LEDS;i++) strip.setPixelColor(i,strip.Color(0,0,b));
    strip.show(); lastUpdate=millis();
  }
}
void standbyCyanPulse() {
  esp_task_wdt_reset();  // v5029: runs for extended periods during dormant mode
  static unsigned long lastUpdate = 0;
  if (millis()-lastUpdate >= 16) {
    float t=fmodf((float)(millis())*0.001f,2.5f);
    float breath=sinf(t*2.513f); breath=(breath+1.0f)*0.5f; breath=breath*breath*(3.0f-2.0f*breath);
    uint8_t v=(uint8_t)(20.0f+breath*235.0f);
    for(int i=0;i<NUM_LEDS;i++) strip.setPixelColor(i,strip.Color(0,v,v));
    strip.show(); lastUpdate=millis();
  }
}
void showQuietReward(unsigned long nowMs) {
  static unsigned long lastFrame = 0; if (nowMs-lastFrame < 16) return; lastFrame = nowMs;
  unsigned long elapsed = nowMs - rewardStartMs;
  if (elapsed < 6000) {
    float pulse = 0.85f + 0.15f * sinf((float)elapsed * 0.005f);
    for (int i=0;i<NUM_LEDS;i++) {
      uint8_t baseR=(uint8_t)(255.0f*pulse), baseG=(uint8_t)(255.0f*pulse), baseB=(uint8_t)(255.0f*pulse);
      uint32_t seed=(uint32_t)(i*7919+elapsed/40);
      seed=(seed^(seed>>16))*0x45d9f3b; seed=(seed^(seed>>16))*0x45d9f3b; seed=seed^(seed>>16);
      if((seed&0xFF)<38) { float sparkle=(float)((seed>>8)&0xFF)/255.0f; sparkle=sparkle*sparkle;
        baseR=255; baseG=(uint8_t)(180.0f+75.0f*(1.0f-sparkle)); baseB=(uint8_t)(40.0f*(1.0f-sparkle)); }
      strip.setPixelColor(i, strip.Color(baseR, baseG, baseB));
    }
    strip.show(); return;
  }
  float t=clampf((float)(elapsed-6000)/2000.0f,0.0f,1.0f); t=t*t*(3.0f-2.0f*t);
  fillStrip(strip.Color((uint8_t)(230.0f*(1.0f-t)),(uint8_t)(180.0f+(255.0f-180.0f)*t),(uint8_t)(20.0f*(1.0f-t))));
}
void showBatteryCharging(float progressPct) {
  static unsigned long lastFrame = 0; unsigned long now = millis();
  if (now-lastFrame < 16) return; lastFrame = now;
  float pct=clampf(progressPct,0.0f,100.0f); int chargedLeds=(int)((pct/100.0f)*(float)NUM_LEDS);
  if (chargedLeds<1) chargedLeds=1; float breathe=0.7f+0.3f*sinf((float)now*0.003f);
  for (int i=0;i<NUM_LEDS;i++) {
    if (i<chargedLeds) {
      float pos=(float)i/(float)NUM_LEDS; uint8_t r,g,b=0;
      if(pos<0.33f){float t=pos/0.33f;r=(uint8_t)((255.0f-t*55.0f)*breathe);g=(uint8_t)((t*140.0f)*breathe);}
      else if(pos<0.66f){float t=(pos-0.33f)/0.33f;r=(uint8_t)((200.0f-t*140.0f)*breathe);g=(uint8_t)((140.0f+t*115.0f)*breathe);}
      else{float t=(pos-0.66f)/0.34f;r=(uint8_t)((60.0f-t*60.0f)*breathe);g=(uint8_t)(255.0f*breathe);}
      if(i==chargedLeds-1){float edge=0.5f+0.5f*sinf((float)now*0.012f);
        r=(uint8_t)clampf((float)r+edge*60.0f,0.0f,255.0f);g=(uint8_t)clampf((float)g+edge*40.0f,0.0f,255.0f);b=(uint8_t)(edge*30.0f);}
      strip.setPixelColor(i,strip.Color(r,g,b));
    } else strip.setPixelColor(i,0);
  }
  strip.show();
}
 
// =====================
//   Debug + state text
// =====================
// v5083: Operational-phase reporter for dashboard status display.
// Precedence matches the firmware's actual render priority — whatever
// the LEDs are physically doing right now, that's what we report.
// Distinct from getCurrentLEDState() which composes a human-readable
// LED-zone label and is preserved for backward compat.
const char* getDevicePhase() {
  if (micFloorTestActive)              return "calibrating_mic_floor";
  if (locateActive)                    return "locating";
  if (deviceMode == MODE_HOLLY_HOP)    return "holly_hop";
  if (rewardShowing)                   return "reward";
  if (!schoolOpen)                     return "dormant";
  // Compute current window state. Returns OFF if NTP isn't valid yet.
  struct tm ti;
  WindowState ws = OFF;
  if (getLocalTime(&ti)) ws = getWindowState(ti.tm_wday, ti.tm_hour, ti.tm_min);
  // In-lesson states (or always-on if useTimetable=false)
  if (ws == LESSON || !useTimetable) {
    if (inSleepMode)                              return "sleeping";
    if (roomCalEnabled && !roomCalComplete)       return "calibrating_learning";
    return "monitoring";
  }
  // Between-lesson states
  switch (ws) {
    case BREAK:    return "break";
    case LUNCH:    return "lunch";
    case ASSEMBLY: return "assembly";
    case OFF:
    default:       return "out_of_hours";
  }
}

// v5083: Window-state stringifier — exposes the raw timetable phase
// alongside device_phase so dashboards can distinguish e.g.
// "between lesson, in break" from "between lesson, after school".
const char* getWindowStateString() {
  struct tm ti;
  WindowState ws = OFF;
  if (getLocalTime(&ti)) ws = getWindowState(ti.tm_wday, ti.tm_hour, ti.tm_min);
  switch (ws) {
    case LESSON:   return "lesson";
    case ASSEMBLY: return "assembly";
    case BREAK:    return "break";
    case LUNCH:    return "lunch";
    case OFF:
    default:       return "off";
  }
}

String getCurrentLEDState() {
  // v5079: Priority ORDER MUST MATCH THE ACTUAL RENDER PATH in handleLEDs().
  // Render order (from line ~2908 onwards):
  //   1. locate active        -> blue pulse
  //   2. Holly Hop active     -> rainbow (early return)
  //   3. reward showing       -> quiet reward visual (early return)
  //   4. sleep mode (post-learning only) -> rainbow (early return)
  //   5. learning active      -> charging animation
  //   6. zone color           -> traffic light
  //
  // Previously "Charging" sat above "Holly Hop" which caused a diagnostic
  // mismatch when hop fired during learning (v5077 era bug — fixed in
  // 5079 by suppressing hop during learning, but the priority order is
  // still corrected here for future-proofing).
  if (locateActive) return "Locating";
  if (deviceMode == MODE_HOLLY_HOP) return "Holly Hop (override)";
  if (rewardShowing) return "Reward (Quiet)";
  if (inSleepMode) return "Sleeping (Rainbow)";
  if (roomCalEnabled && !roomCalComplete) return "Charging";
  switch (currentZone) {
    case Z_GREEN: return "Green"; case Z_AMBER: return "Amber";
    case Z_DEEP: return "Deep Amber"; case Z_RED: return "Red"; default: return "Green";
  }
}
void printDateTimeDebug(bool showMonitoring) {
  struct tm ti; if (!getLocalTime(&ti)) { Serial.println("getLocalTime() failed"); return; }
  char buf[32]; strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&ti);
  Serial.printf("  %s | wday=%d (%s)\n",buf,ti.tm_wday,WEEKDAY[ti.tm_wday]);
  if (showMonitoring) { int mins=ti.tm_hour*60+ti.tm_min; WindowState ws=getWindowState(ti.tm_wday,ti.tm_hour,ti.tm_min);
    const char*s=(ws==LESSON?"LESSON":ws==ASSEMBLY?"ASSEMBLY":ws==BREAK?"BREAK":ws==LUNCH?"LUNCH":"OFF");
    Serial.printf("  state=%s | mins=%d\n",s,mins); }
}
 
// =====================
//   Milestone Event Logging (v5071)
// =====================
// Fire-and-forget POST of milestone events to Apps Script.
// 3-second timeout, no retry, never blocks the main loop.
// Best-effort audit trail — if the POST fails, normal telemetry
// will still update current state in the registry.
//
// Apps Script must be v45+ to handle action="device_event".
//
// value/detail are optional — pass empty string if not used.
static void postEvent(const char* eventType, const char* value, const char* detail) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("  [event] %s skipped (no WiFi)\n", eventType);
    return;
  }
  // v5080: Same gates as postToSheets() — TLS handshake during the
  // post-reconnect window or below RSSI gate has historically blocked
  // past the watchdog. Events are best-effort, so skip cleanly.
  if (wifiReconnectedAtMs != 0 && (millis() - wifiReconnectedAtMs) < POST_RECONNECT_GRACE_MS) {
    Serial.printf("  [event] %s skipped (post-reconnect grace)\n", eventType);
    return;
  }
  int evtRssi = WiFi.RSSI();
  if (evtRssi != 0 && evtRssi < RSSI_POST_GATE_DBM) {
    Serial.printf("  [event] %s skipped (RSSI %d dBm)\n", eventType, evtRssi);
    return;
  }
  DynamicJsonDocument doc(640);
  doc["action"]           = "device_event";
  doc["device_id"]        = deviceId;
  doc["mac_address"]      = WiFi.macAddress();
  doc["firmware_version"] = FW_VERSION;
  doc["event_type"]       = eventType;
  if (value  && value[0])  doc["value"]  = value;
  if (detail && detail[0]) doc["detail"] = detail;
  // Timestamp from device — Apps Script also stamps server-side.
  struct tm ti;
  if (getLocalTime(&ti)) {
    char ts[30];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &ti);
    doc["timestamp"] = ts;
  }
  String payload; serializeJson(doc, payload);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(3000);  // 3-second hard cap — must not block main loop
  http.begin(GOOGLE_SHEETS_URL);
  http.addHeader("Content-Type", "application/json");
  esp_task_wdt_reset();
  int code = http.POST(payload);
  esp_task_wdt_reset();
  Serial.printf("  [event] %s -> HTTP %d\n", eventType, code);
  http.end();
}

// Convenience: numeric value
static void postEventF(const char* eventType, float value, const char* detail) {
  char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "%.2f", value);
  postEvent(eventType, vbuf, detail);
}

// =====================
//   Google Sheets Logging
// =====================
static void postToSheets(const char* logLabel, DynamicJsonDocument& doc) {
  // v5029: Defensive WiFi status check — was already here.
  if (WiFi.status() != WL_CONNECTED) return;

  // v5030: Reject if we just reconnected. Let the TLS/DHCP stack settle.
  // Without this, the first POST after a WiFi recovery blocks in TLS
  // handshake for >15s and the watchdog fires.
  if (wifiReconnectedAtMs != 0 && (millis() - wifiReconnectedAtMs) < POST_RECONNECT_GRACE_MS) {
    Serial.printf("  Google POST (%s): skipped (post-reconnect grace, %lums elapsed)\n",
                  logLabel, (unsigned long)(millis() - wifiReconnectedAtMs));
    return;
  }

  // v5030: Reject if RSSI is in the "will probably block" zone.
  // Signal weaker than -88 dBm means the TLS handshake is overwhelmingly
  // likely to stall past the 15s watchdog. Skip and try again next cycle.
  int rssi = WiFi.RSSI();
  if (rssi != 0 && rssi < RSSI_POST_GATE_DBM) {
    Serial.printf("  Google POST (%s): skipped (RSSI %d dBm below gate %d dBm)\n",
                  logLabel, rssi, RSSI_POST_GATE_DBM);
    return;
  }

  char displayName[66]; snprintf(displayName,sizeof(displayName),"Holly%s%s",strlen(hollyName)>0?" ":"",strlen(hollyName)>0?hollyName:"");
  doc["holly_name"]=hollyName; doc["display_name"]=displayName; doc["classroom"]=classroomName; doc["device_id"]=deviceId;
  doc["year_band"]=yearBand; doc["ip_address"]=WiFi.localIP().toString(); doc["mac_address"]=WiFi.macAddress();
  doc["temperature_C"]=temperature; doc["humidity_percent"]=humidity; doc["pressure_hPa"]=pressure;
  addHealthPayload(doc);
  struct tm timeinfo; if(getLocalTime(&timeinfo)){char ts[30];strftime(ts,sizeof(ts),"%Y-%m-%d %H:%M:%S",&timeinfo);doc["timestamp"]=ts;}
  else doc["timestamp"]="TIME_UNAVAILABLE";
  HTTPClient http; http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS); http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(GOOGLE_SHEETS_URL); http.addHeader("Content-Type","application/json");
  const char*hdrs[]={"Location"}; http.collectHeaders(hdrs,1);
  String payload; serializeJson(doc,payload);
  esp_task_wdt_reset(); int responseCode=http.POST(payload); esp_task_wdt_reset();
  String responseBody="";
  if(responseCode==302&&http.hasHeader("Location")){
    String redirectUrl=http.header("Location"); http.end();
    // v5030: CRITICAL — re-check WiFi before the redirect GET. If WiFi
    // dropped between POST and GET, the http2.GET() will block on a dead
    // connection for its full timeout, which is exactly the failure mode
    // we saw in the v5029 log. Abort cleanly instead.
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("  Redirect GET aborted: WiFi dropped mid-transaction");
      setLastError("WiFi lost mid-POST");
      return;
    }
    // v5030: Shorter timeout on the redirect GET. The response from Google
    // Apps Script after a 302 is small and fast — doesn't need 30s.
    // 8s leaves comfortable headroom under the 15s watchdog.
    WiFiClientSecure client; client.setInsecure(); client.setTimeout(REDIRECT_GET_TIMEOUT_MS/1000);
    HTTPClient http2; http2.setTimeout(REDIRECT_GET_TIMEOUT_MS); http2.begin(client,redirectUrl);
    esp_task_wdt_reset(); int rc2=http2.GET(); esp_task_wdt_reset();
    if(rc2==HTTP_CODE_OK){responseBody=http2.getString(); esp_task_wdt_reset(); responseCode=200;}
    else Serial.printf("  Redirect GET failed: %d\n",rc2);
    http2.end();
  } else if(responseCode==HTTP_CODE_OK){responseBody=http.getString(); esp_task_wdt_reset(); http.end();}
  else{String errStr="";
    if(responseCode>0){String body=http.getString();Serial.printf("  Response (%d): len=%d\n",responseCode,body.length());}
    else{errStr=http.errorToString(responseCode);}
    http.end();
    if(responseCode>0){char eb[64];snprintf(eb,sizeof(eb),"Sheets POST HTTP %d",responseCode);setLastError(eb);}
    else if(errStr.length()>0){char eb[64];snprintf(eb,sizeof(eb),"Sheets POST failed: %s",errStr.c_str());setLastError(eb);}
  }
  if(responseBody.length()>2)processRemoteConfig(responseBody);
  Serial.printf("  Google POST (%s): %d\n",logLabel,responseCode);
}
void addHealthPayload(DynamicJsonDocument& doc) {
  doc["confirmed_version"]=nvsConfigVersion; doc["active_green"]=ACTIVE_TH.greenMax; doc["active_amber"]=ACTIVE_TH.amberMax; doc["active_red"]=ACTIVE_TH.redWarnDb;
  doc["active_brightness"]=strip.getBrightness(); doc["active_year_group"]=yearGroupInput; doc["active_year_band"]=yearBand; doc["active_classroom"]=classroomName;
  doc["last_zone"]=getCurrentLEDState();
  // v5083: device_phase and ws are new fields used by dashboards
  // (v12.23 / v2.8.0) to compose accurate status text. Old dashboards
  // ignore them; old firmware doesn't emit them and dashboards fall
  // back gracefully.
  doc["device_phase"]=getDevicePhase();
  doc["ws"]=getWindowStateString();
  doc["firmware_version"]=FW_VERSION;
  doc["wifi_rssi"]=WiFi.RSSI();
  doc["uptime_minutes"]=(unsigned long)((millis()-bootTimeMs)/60000UL);
  doc["last_reset_reason"]=lastResetReason; if(strlen(lastError)>0)doc["last_error"]=lastError;
  doc["temperature_C"]=temperature;
  doc["active_timetable_enabled"]=useTimetable?"Yes":"No"; doc["active_school_open"]=schoolOpen?"Yes":"No";
  doc["active_send_count"]=sendCount; doc["active_sensitivity"]=sensitivityProfile;
  doc["mic_floor"]=micFloor; doc["mic_floor_valid"]=micFloorValid?"Yes":"No";
  if(strlen(micFloorDate)>0) doc["mic_floor_date"]=micFloorDate;
  // v5077: Per-unit mic offset correction. Reported back to the dashboard
  // so admins can see what correction is currently active on this Holly.
  doc["mic_offset_corr"] = micOffsetCorrection;
  if(autoReopened){doc["auto_reopened"]="Yes";autoReopened=false;}
  doc["active_day_start"]=t_day_start; doc["active_break_start"]=t_break1_start; doc["active_break_end"]=t_break1_end;
  doc["active_lunch_start"]=t_lunch_start; doc["active_lunch_end"]=t_lunch_end; doc["active_day_end"]=t_day_end;
  // v5039: Afternoon break — empty string means "not configured at this school"
  doc["active_pmbreak_start"]=t_pmbreak_start; doc["active_pmbreak_end"]=t_pmbreak_end;
  if(roomCalEnabled)doc["cal_status"]=roomCalComplete?"complete":"learning"; else doc["cal_status"]=manualOverride?"manual_override":"disabled";
  doc["cal_mean"]=(float)calMean; double variance=(calCount>1)?(calM2/(double)(calCount-1)):0.0;
  doc["cal_sd"]=(float)sqrt(variance<0?0:variance); doc["cal_min"]=calMinDb<9000.0f?calMinDb:0.0f; doc["cal_max"]=calMaxDb>-9000.0f?calMaxDb:0.0f;
  doc["cal_samples"]=calCount; doc["cal_green"]=CAL_TH.greenMax; doc["cal_amber"]=CAL_TH.amberMax; doc["cal_red"]=CAL_TH.redWarnDb;
  if(hasValidTime(roomCalStartEpoch)){struct tm ti;time_t t=roomCalStartEpoch;localtime_r(&t,&ti);char buf[20];strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M",&ti);doc["cal_start_date"]=buf;}
  if(roomCalComplete&&hasValidTime(roomCalStartEpoch)){time_t ct=roomCalStartEpoch+ROOM_CAL_SECONDS;struct tm ti;localtime_r(&ct,&ti);char buf[20];strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M",&ti);doc["cal_complete_date"]=buf;}
}
 
// =====================
//   Remote Config
// =====================
// v5038: processRemoteConfig now ONLY sets flags and parses simple key/value
// config. It does NOT do any Serial output inside the handler itself (apart
// from a single line at the end) — verbose diagnostic prints moved to the
// main loop where they can happen on a clean stack. Observed crash pattern:
// Serial output interleaved with WiFi/HTTP teardown caused heap corruption
// when locate:true was in the response. Action items (locate/reboot/room
// test) are set as flags; the main loop does the real work on the next
// iteration with fresh stack + idle WiFi.
void processRemoteConfig(const String& responseBody) {
  DynamicJsonDocument resp(4096); DeserializationError err=deserializeJson(resp,responseBody);
  if(err)return;
  if(!resp["ok"].as<bool>())return; if(!resp.containsKey("config"))return;
  JsonObject cfg=resp["config"]; if(cfg.isNull())return;
  // Set action flags — main loop will print and act on them
  if(cfg.containsKey("reboot")&&cfg["reboot"].as<bool>()){remoteRebootRequested=true;return;}
  if(cfg.containsKey("room_test")&&cfg["room_test"].as<bool>())micFloorTestRequested=true;
  if(cfg.containsKey("locate")&&cfg["locate"].as<bool>())locateRequested=true;
  if(cfg.containsKey("locate_cancel")&&cfg["locate_cancel"].as<bool>())locateCancelRequested=true;
  // v5080: All four handlers below are now deferred-flag only.
  // They previously did flash writes + opened a SECOND HTTPClient
  // (via postEvent) while still inside the postToSheets() TLS stack.
  // See header changelog for root-cause analysis.

  // v5075/v5080: Learning mode start/reset — one-shot action.
  if (cfg.containsKey("set_room_cal_enabled") && cfg["set_room_cal_enabled"].as<bool>()) {
    remoteSetRoomCalEnabledRequested = true;
  }
  // v5075/v5080: Learning mode CANCEL — one-shot action.
  if (cfg.containsKey("cancel_learning") && cfg["cancel_learning"].as<bool>()) {
    remoteCancelLearningRequested = true;
  }
  // v5077/v5080: Per-unit mic offset correction — one-shot delivery.
  if (cfg.containsKey("mic_db_offset_correction")) {
    float newOffset = cfg["mic_db_offset_correction"].as<float>();
    if (isfinite(newOffset)) {
      pendingMicOffsetCorrection = constrain(newOffset, -25.0f, 25.0f);
      remoteMicOffsetUpdateRequested = true;
    }
  }
  // v5078/v5080: Remote clear of in-RAM lastError[] buffer.
  if (cfg.containsKey("clear_last_error") && cfg["clear_last_error"].as<bool>()) {
    remoteClearLastErrorRequested = true;
  }
  uint32_t serverVersion=cfg["config_version"]|0;
  if(serverVersion==0||serverVersion==nvsConfigVersion)return;
  // v5082: Defer applyRemoteConfig to loop() — flash writes were
  // previously running inside the postToSheets() TLS stack. Stash
  // the body for re-parse; loop() drain handles version bump too.
  pendingConfigBody = responseBody;
  pendingConfigVersion = serverVersion;
  remoteApplyConfigRequested = true;
}
void applyRemoteConfig(DynamicJsonDocument& resp) {
  JsonObject cfg=resp["config"]; bool thresholdsChanged=false,yearGroupChanged=false;
  if(cfg.containsKey("set_year_group")){const char*v=cfg["set_year_group"].as<const char*>();
    if(v&&strlen(v)>0&&strcmp(v,yearGroupInput)!=0){strlcpy(yearGroupInput,v,sizeof(yearGroupInput));
      computeYearBandFromInput(yearGroupInput,yearBand,sizeof(yearBand));
      preferences.putString("yearGroupInput",yearGroupInput);preferences.putString("yearBand",yearBand);yearGroupChanged=true;}}
  if(cfg.containsKey("set_classroom")){const char*v=cfg["set_classroom"].as<const char*>();
    if(v&&strlen(v)>0&&strcmp(v,classroomName)!=0){strlcpy(classroomName,v,sizeof(classroomName));preferences.putString("classroom",classroomName);}}
  float nG=cfg["set_green_max"]|-1.0f,nA=cfg["set_amber_max"]|-1.0f,nR=cfg["set_red_warn"]|-1.0f;
  if((nG>0&&nG!=ACTIVE_TH.greenMax)||(nA>0&&nA!=ACTIVE_TH.amberMax)||(nR>0&&nR!=ACTIVE_TH.redWarnDb))thresholdsChanged=true;
  if(thresholdsChanged){Thresholds remote;remote.greenMax=(nG>0)?nG:ACTIVE_TH.greenMax;remote.amberMax=(nA>0)?nA:ACTIVE_TH.amberMax;remote.redWarnDb=(nR>0)?nR:ACTIVE_TH.redWarnDb;
    remote=applyGuardrails(remote,yearBand);ACTIVE_TH=remote;manualOverride=true;preferences.putBool("manual_override",true);
    preferences.putFloat("mo_green",ACTIVE_TH.greenMax);preferences.putFloat("mo_amber",ACTIVE_TH.amberMax);preferences.putFloat("mo_red",ACTIVE_TH.redWarnDb);
    Serial.printf("  Applied: G<=%.1f, A<=%.1f, R<=%.1f | manual_override=ON\n",ACTIVE_TH.greenMax,ACTIVE_TH.amberMax,ACTIVE_TH.redWarnDb);}
  if(cfg.containsKey("manual_override")){bool v=cfg["manual_override"].as<bool>();if(!v&&manualOverride){manualOverride=false;preferences.putBool("manual_override",false);resolveActiveThresholds();}}
  if(cfg.containsKey("set_brightness")){int v=cfg["set_brightness"].as<int>();if(v>=0&&v<=255&&(uint8_t)v!=strip.getBrightness()){strip.setBrightness((uint8_t)v);preferences.putUChar("brightness",(uint8_t)v);}}
  if(yearGroupChanged){if(roomCalEnabled){time_t now=time(NULL);resetRoomCalibration(now);if(!manualOverride)resolveActiveThresholds();}
    else{if(!manualOverride)ACTIVE_TH=applyGuardrails(thresholdsForBand(yearBand),yearBand);}}
  if(cfg.containsKey("set_timetable_enabled")){bool v=cfg["set_timetable_enabled"].as<bool>();if(v!=useTimetable){useTimetable=v;preferences.putBool("use_timetable",useTimetable);}}
  if(cfg.containsKey("set_school_open")){bool v=cfg["set_school_open"].as<bool>();if(v!=schoolOpen){schoolOpen=v;preferences.putBool("school_open",schoolOpen);}}
  if(cfg.containsKey("set_reopen_date")){const char*d=cfg["set_reopen_date"].as<const char*>();if(d&&strcmp(d,reopenDate)!=0){strlcpy(reopenDate,d,sizeof(reopenDate));preferences.putString("reopen_date",reopenDate);}}
  bool ttChanged=false;
  auto applyTime=[&](const char*key,char*dest,size_t dl){if(cfg.containsKey(key)){const char*v=cfg[key].as<const char*>();if(v&&strlen(v)>=4&&strlen(v)<=5&&strcmp(v,dest)!=0){strlcpy(dest,v,dl);ttChanged=true;}}};
  applyTime("set_day_start",t_day_start,sizeof(t_day_start));applyTime("set_break_start",t_break1_start,sizeof(t_break1_start));
  applyTime("set_break_end",t_break1_end,sizeof(t_break1_end));applyTime("set_lunch_start",t_lunch_start,sizeof(t_lunch_start));
  applyTime("set_lunch_end",t_lunch_end,sizeof(t_lunch_end));applyTime("set_day_end",t_day_end,sizeof(t_day_end));
  // v5039: Afternoon break — allowed to be blank. Use applyTimeOrBlank which
  // accepts empty strings (unlike applyTime which rejects them).
  auto applyTimeOrBlank=[&](const char*key,char*dest,size_t dl){
    if(cfg.containsKey(key)){
      const char*v=cfg[key].as<const char*>();
      if(v){
        size_t vl=strlen(v);
        if(vl==0){if(dest[0]!='\0'){dest[0]='\0';ttChanged=true;}}  // clear
        else if(vl>=4&&vl<=5&&strcmp(v,dest)!=0){strlcpy(dest,v,dl);ttChanged=true;}
      }
    }
  };
  applyTimeOrBlank("set_pmbreak_start",t_pmbreak_start,sizeof(t_pmbreak_start));
  applyTimeOrBlank("set_pmbreak_end",t_pmbreak_end,sizeof(t_pmbreak_end));
  if(ttChanged){preferences.putString("t_day_start",t_day_start);preferences.putString("t_break1_start",t_break1_start);preferences.putString("t_break1_end",t_break1_end);
    preferences.putString("t_lunch_start",t_lunch_start);preferences.putString("t_lunch_end",t_lunch_end);preferences.putString("t_day_end",t_day_end);
    preferences.putString("t_pmbreak_start",t_pmbreak_start);preferences.putString("t_pmbreak_end",t_pmbreak_end);}
  if(cfg.containsKey("set_send_count")){int v=cfg["set_send_count"].as<int>();if(v<0)v=0;if(v!=sendCount){sendCount=v;preferences.putInt("send_count",sendCount);resetSendLessonState();}}
  if(cfg.containsKey("sensitivity_profile")){const char*p=cfg["sensitivity_profile"].as<const char*>();
    if(p&&strlen(p)>0&&strcmp(p,sensitivityProfile)!=0){strlcpy(sensitivityProfile,p,sizeof(sensitivityProfile));sensitivityMultiplier=getSensitivityMultiplier(sensitivityProfile);
      preferences.putString("sens_profile",sensitivityProfile);resolveActiveThresholds();}}
  // v5075: set_room_cal_enabled and cancel_learning moved to
  // processRemoteConfig() above (they're one-shot action flags, not
  // config changes — must run BEFORE the config_version guard).
  Serial.println("  Config applied.");
}
void resolveActiveThresholds() {
  if(manualOverride){ACTIVE_TH.greenMax=preferences.getFloat("mo_green",ACTIVE_TH.greenMax);ACTIVE_TH.amberMax=preferences.getFloat("mo_amber",ACTIVE_TH.amberMax);ACTIVE_TH.redWarnDb=preferences.getFloat("mo_red",ACTIVE_TH.redWarnDb);ACTIVE_TH=applyGuardrails(ACTIVE_TH,yearBand);}
  else if(roomCalEnabled&&roomCalComplete){ACTIVE_TH=CAL_TH;}
  else{ACTIVE_TH=applyGuardrails(thresholdsForBand(yearBand),yearBand);}
  if(sensitivityMultiplier!=1.0f){ACTIVE_TH.greenMax*=sensitivityMultiplier;ACTIVE_TH.amberMax*=sensitivityMultiplier;ACTIVE_TH.redWarnDb*=sensitivityMultiplier;ACTIVE_TH=applyGuardrails(ACTIVE_TH,yearBand);}
}
void logToGoogleSheets(float avgDB) {
  DynamicJsonDocument doc(3072);
  doc["year_groups"]=yearGroupInput;doc["th_green_max"]=ACTIVE_TH.greenMax;doc["th_amber_max"]=ACTIVE_TH.amberMax;doc["th_red_warn"]=ACTIVE_TH.redWarnDb;
  doc["sound_dB"]=avgDB;doc["voice_percent"]=voicePercent;doc["led_state"]=getCurrentLEDState();
  doc["holly_hop"]=(deviceMode==MODE_HOLLY_HOP);doc["quiet_reward"]=rewardActiveForLog?"Yes":"";
  doc["room_cal_enabled"]=roomCalEnabled?"Yes":"";doc["room_cal_complete"]=roomCalComplete?"Yes":"";doc["room_cal_days"]=(int)ROOM_CAL_DAYS;
  // v5082: Sample-count progress (replaces wall-clock progress).
  // calCount is the count of in-lesson Welford increments — same metric
  // the completion gate uses, so progress is 100% exactly when learning
  // completes.
  if(roomCalEnabled && !roomCalComplete){
    int pct = constrain((int)((float)calCount * 100.0f / (float)ROOM_CAL_MIN_SAMPLES), 0, 100);
    char ps[8]; snprintf(ps, sizeof(ps), "%d%%", pct);
    doc["learning_progress"] = ps;
  }
  if(sendCount>0){doc["spike_count"]=spikeCount;doc["transition_avg_db"]=transitionAvgDb;doc["send_count"]=sendCount;}
  postToSheets("periodic",doc);
}

// v5035: Heartbeat POST — health payload only, no sound data.
// Sent every HEARTBEAT_INTERVAL_MS when outside lesson window.
// Still pulls config on response, so serves as config check-in too.
// Payload is marked with event_type=HEARTBEAT so Apps Script can route it
// away from the telemetry tab and keep sound-data tabs clean.
void postHeartbeat() {
  DynamicJsonDocument doc(2048);
  doc["event_type"]   = "HEARTBEAT";
  doc["led_state"]    = getCurrentLEDState();
  // addHealthPayload() is called inside postToSheets() — it already adds
  // confirmed_version, RSSI, uptime, firmware, errors, mic floor, etc.
  // We do NOT add sound_dB, voice_percent, th_green_max etc here.
  postToSheets("heartbeat", doc);
}
void logHollyHopEvent(const char*el){DynamicJsonDocument doc(3072);doc["event_type"]="HOLLY_HOP";doc["event"]=el;doc["led_state"]="Holly Hop";postToSheets(el,doc);}
void logQuietRewardEvent(float avgDB){DynamicJsonDocument doc(3072);doc["event_type"]="QUIET_REWARD";doc["event"]="TRIGGER";doc["sound_dB"]=avgDB;doc["led_state"]="Reward (Quiet)";doc["quiet_reward"]="Yes";postToSheets("Quiet Reward",doc);}
 
// =====================
//   OTA Implementation
// =====================
static String todayKeyYYYYMMDD(const tm&ti){char buf[16];snprintf(buf,sizeof(buf),"%04d%02d%02d",ti.tm_year+1900,ti.tm_mon+1,ti.tm_mday);return String(buf);}
static bool otaShouldCheckNow(const tm&ti){return(ti.tm_hour>=OTA_CHECK_START_HOUR&&ti.tm_hour<OTA_CHECK_END_HOUR);}
static void otaBootCheck(){
  if(otaBootCheckDone)return; if(!hasValidTime(time(NULL))||WiFi.status()!=WL_CONNECTED)return;
  // v5050: 30-second boot grace before first OTA check. Gives the lwIP
  // stack time to fully initialise before any HTTPS work — particularly
  // important on the first boot after an OTA flash, where the timing
  // race against udp_new_ip_type assert was reliably reproducible.
  if (millis() - bootTimeMs < 30000) return;
  // v5050: Belt and braces — verify we actually have a usable IP, not
  // just WL_CONNECTED. WiFi can briefly report connected before DHCP
  // completes; calling network code in that window can race the stack.
  if (WiFi.localIP() == IPAddress(0,0,0,0)) return;
  if(rewardShowing||deviceMode!=MODE_NORMAL)return; unsigned long now=millis();
  if(lastBootOtaAttemptMs!=0&&(now-lastBootOtaAttemptMs<OTA_BOOT_RETRY_INTERVAL_MS))return;
  lastBootOtaAttemptMs=now;otaBootRetries++;
  Serial.printf("  OTA: boot check (%d/%d)\n",otaBootRetries,OTA_BOOT_MAX_RETRIES);
  String nv,bu; if(!otaFetchManifest(nv,bu)){if(otaBootRetries>=OTA_BOOT_MAX_RETRIES)otaBootCheckDone=true;return;}
  if(compareVersionTokens(FW_VERSION,nv.c_str())>=0){Serial.printf("  OTA: up to date (manifest=%s)\n",nv.c_str());otaBootCheckDone=true;return;}
  if(otaDownloadAndFlash(bu)){preferences.putString("ota_last_installed",nv);delay(800);ESP.restart();}
  if(otaBootRetries>=OTA_BOOT_MAX_RETRIES)otaBootCheckDone=true;
}
static bool otaFetchManifest(String&outV,String&outU){
  if(WiFi.status()!=WL_CONNECTED){Serial.println("  OTA-DBG: manifest fetch aborted — WiFi not connected");return false;}
  Serial.printf("  OTA-DBG: fetching manifest from %s\n", OTA_MANIFEST_URL);
  WiFiClientSecure c;c.setInsecure();c.setTimeout(HTTP_TIMEOUT_MS/1000);
  HTTPClient h;h.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);h.setTimeout(HTTP_TIMEOUT_MS);
  if(!h.begin(c,OTA_MANIFEST_URL)){Serial.println("  OTA-DBG: h.begin() returned false on manifest URL");return false;}
  esp_task_wdt_reset();int code=h.GET();esp_task_wdt_reset();
  Serial.printf("  OTA-DBG: manifest GET returned HTTP %d\n", code);
  if(code!=HTTP_CODE_OK){h.end();return false;}
  String body=h.getString(); esp_task_wdt_reset(); h.end();
  Serial.printf("  OTA-DBG: manifest body (%d bytes): %s\n", body.length(), body.c_str());
  DynamicJsonDocument doc(2048);
  DeserializationError jerr = deserializeJson(doc,body);
  if(jerr){Serial.printf("  OTA-DBG: manifest JSON parse failed: %s\n", jerr.c_str()); esp_task_wdt_reset(); return false;}
  esp_task_wdt_reset();
  const char*v=doc["version"]|"";const char*u=doc["url"]|"";
  Serial.printf("  OTA-DBG: parsed version='%s', url='%s'\n", v, u);
  if(!v||!u||strlen(v)<3||strlen(u)<8){Serial.println("  OTA-DBG: manifest missing/short version or url");return false;}
  outV=String(v);outU=String(u);return true;
}
static int compareVersionTokens(const char*a,const char*b){
  if(!a||!b)return 0;int ia=0,ib=0;
  while(a[ia]||b[ib]){while(a[ia]&&!isDigit((unsigned char)a[ia]))ia++;while(b[ib]&&!isDigit((unsigned char)b[ib]))ib++;
    long va=0,vb=0;bool ha=false,hb=false;
    while(a[ia]&&isDigit((unsigned char)a[ia])){ha=true;va=va*10+(a[ia]-'0');ia++;}
    while(b[ib]&&isDigit((unsigned char)b[ib])){hb=true;vb=vb*10+(b[ib]-'0');ib++;}
    if(!ha&&!hb)break;if(va<vb)return-1;if(va>vb)return 1;}
  return 0;
}
static bool otaDownloadAndFlash(const String&binUrl){
  if(WiFi.status()!=WL_CONNECTED){Serial.println("  OTA-DBG: download aborted — WiFi not connected");return false;}
  Serial.printf("  OTA-DBG: downloading firmware from %s\n", binUrl.c_str());
  WiFiClientSecure c;c.setInsecure();c.setTimeout(OTA_BIN_TIMEOUT_MS/1000);
  HTTPClient h;h.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);h.setTimeout(OTA_BIN_TIMEOUT_MS);
  if(!h.begin(c,binUrl)){Serial.println("  OTA-DBG: h.begin() returned false on binary URL");return false;}
  esp_task_wdt_reset();int code=h.GET();esp_task_wdt_reset();
  Serial.printf("  OTA-DBG: binary GET returned HTTP %d\n", code);
  if(code!=HTTP_CODE_OK){h.end();return false;}
  int cl=h.getSize();
  Serial.printf("  OTA-DBG: content-length = %d bytes\n", cl);
  if(cl<=0){Serial.println("  OTA-DBG: content-length missing or zero — cannot flash");h.end();return false;}
  if(!Update.begin((size_t)cl)){
    Serial.printf("  OTA-DBG: Update.begin(%d) failed: %s\n", cl, Update.errorString());
    h.end();return false;
  }
  Serial.println("  OTA-DBG: Update.begin OK — writing stream in chunks...");
  // v5046: CRITICAL FIX. Was Update.writeStream() — a single blocking call
  // that reads the entire HTTPS stream and writes to flash without ever
  // returning, which on a 1.28MB binary over 2.4GHz WiFi takes 22+ seconds.
  // Watchdog (15s) fires before flash completes — bootloop. Now we read
  // in 4KB chunks, write each chunk to Update, and reset the watchdog
  // between chunks. Total flash time roughly the same; no watchdog trip.
  WiFiClient* stream = h.getStreamPtr();
  const size_t CHUNK_SIZE = 4096;
  // v5048: Heap-allocate the chunk buffer. v5046's stack-allocated 4KB
  // buffer blew the loopTask stack canary (default 8KB stack, deep call
  // chain through HTTPClient/WiFiClientSecure already consumes most of it).
  // malloc on heap is safe — heap had 170KB free at OTA time.
  uint8_t* buf = (uint8_t*)malloc(CHUNK_SIZE);
  if (!buf) {
    Serial.println("  OTA-DBG: malloc(4096) failed — out of heap");
    Update.abort(); h.end(); return false;
  }
  size_t totalWritten = 0;
  unsigned long chunkStartMs = millis();
  unsigned long lastProgressMs = millis();
  while (h.connected() && (cl<0 || totalWritten<(size_t)cl)) {
    size_t available = stream->available();
    if (available > 0) {
      size_t toRead = (available > CHUNK_SIZE) ? CHUNK_SIZE : available;
      int bytesRead = stream->readBytes(buf, toRead);
      if (bytesRead > 0) {
        size_t written = Update.write(buf, bytesRead);
        if (written != (size_t)bytesRead) {
          Serial.printf("  OTA-DBG: chunk write mismatch (read %d, wrote %d): %s\n",
                        bytesRead, (int)written, Update.errorString());
          free(buf); Update.abort(); h.end(); return false;
        }
        totalWritten += written;
        lastProgressMs = millis();
        // Progress log every ~64KB
        if ((totalWritten % (CHUNK_SIZE * 16)) < CHUNK_SIZE) {
          int pct = cl > 0 ? (int)((totalWritten * 100) / cl) : 0;
          Serial.printf("  OTA-DBG: %d / %d bytes (%d%%)\n", (int)totalWritten, cl, pct);
        }
      }
    } else {
      // No data available right now — yield briefly and check for stall
      delay(1);
      if (millis() - lastProgressMs > 10000) {
        Serial.println("  OTA-DBG: stream stalled >10s with no data — aborting");
        free(buf); Update.abort(); h.end(); return false;
      }
    }
    esp_task_wdt_reset();  // CRITICAL: keep watchdog happy on every loop iteration
  }
  size_t w = totalWritten;
  free(buf);  // v5048: release heap buffer on success path
  Serial.printf("  OTA-DBG: chunked write complete — wrote %d bytes (expected %d) in %lums\n",
                (int)w, cl, (unsigned long)(millis() - chunkStartMs));
  if(w!=(size_t)cl){
    Serial.printf("  OTA-DBG: write incomplete — Update.errorString: %s\n", Update.errorString());
    Update.abort();h.end();return false;
  }
  if(!Update.end()){
    Serial.printf("  OTA-DBG: Update.end() failed: %s\n", Update.errorString());
    h.end();return false;
  }
  if(!Update.isFinished()){
    Serial.println("  OTA-DBG: Update.isFinished() returned false");
    h.end();return false;
  }
  h.end();
  // v5071: stash current FW so the next boot can log an ota_completed event
  preferences.putString("ota_prev_fw", FW_VERSION);
  Serial.println("  OTA: Update complete");return true;
}
static void otaDailyCheck(){
  struct tm ti;if(!getLocalTime(&ti)||!hasValidTime(time(NULL))||!otaShouldCheckNow(ti))return;
  String tk=todayKeyYYYYMMDD(ti);if(preferences.getString("ota_last_day","")==tk)return;
  if(rewardShowing||deviceMode!=MODE_NORMAL)return;
  String nv,bu;if(!otaFetchManifest(nv,bu))return;
  if(compareVersionTokens(FW_VERSION,nv.c_str())>=0){Serial.printf("  OTA: up to date (manifest=%s)\n",nv.c_str());preferences.putString("ota_last_day",tk);return;}
  if(!otaDownloadAndFlash(bu))return;
  preferences.putString("ota_last_day",tk);preferences.putString("ota_last_installed",nv);delay(800);ESP.restart();
}
 
// =====================
//   Wi-Fi Connect
// =====================
void connectToWiFi() {
  // v5034: Custom captive portal replaces WiFiManager.
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  bool setupDone = preferences.getBool("setup_done", false);
  // v5041: Check for wifi-only reset flag (set by performWifiReset before reboot).
  // If set, Holly was previously fully configured; only WiFi creds were cleared.
  // Launch the captive portal in reduced "reconnect WiFi" mode instead of the
  // full 8-screen wizard. The portal clears this flag on successful connect.
  bool wifiOnlyFlag = preferences.getBool("wifi_only", false);

  if (!setupDone || wifiOnlyFlag) {
    if (wifiOnlyFlag) {
      Serial.println("  WiFi-only reset flag set -- launching portal in reconnect mode");
    } else {
      Serial.println("  No setup in NVS -- launching captive portal");
    }
    // Boot rainbow while portal starts
    {unsigned long rs=millis();uint16_t ho=0;float sp=3.0f;
      while(millis()-rs<BOOT_STANDBY_MS){float pr=(float)(millis()-rs)/(float)BOOT_STANDBY_MS;sp=3.0f*(1.0f-pr*0.7f);
        for(int i=0;i<NUM_LEDS;i++)strip.setPixelColor(i,wheel(((i*256)/NUM_LEDS+ho)&0xFF));strip.show();ho+=(uint16_t)sp;delay(16);}}

    CaptivePortal::begin(&preferences, wifiOnlyFlag);
    Serial.printf("  Portal active: %s (IP 192.168.4.1)%s\n",
                  CaptivePortal::getApSsid(),
                  wifiOnlyFlag ? " [WiFi-only mode]" : "");
    // v5071: print full MAC while waiting at portal — needed for dispatch QR generation.
    // Read directly from EFUSE base MAC. WiFi.macAddress() can return zeros if
    // called after softAP has reconfigured the radio. esp_efuse_mac_get_default()
    // returns the STA MAC (the one captive_portal used to derive SSID + password)
    // and is independent of WiFi state.
    {
      uint8_t mac[6];
      esp_efuse_mac_get_default(mac);
      char macStr[13];
      snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      Serial.printf("  Full MAC: %s\n", macStr);
    }

    while (!CaptivePortal::shouldReboot()) {
      CaptivePortal::loop();
      esp_task_wdt_reset();
      float t = (float)(millis() % 4000) / 4000.0f;
      float b = 0.3f + 0.5f * (0.5f + 0.5f * sinf(t * 2.0f * 3.14159f));
      for (int i = 0; i < NUM_LEDS; i++)
        strip.setPixelColor(i, strip.Color((uint8_t)(40*b), (uint8_t)(180*b), (uint8_t)(240*b)));
      strip.show();
      delay(20);
    }

    Serial.println("  Portal committed -- restarting");
    CaptivePortal::stop();
    delay(300);
    ESP.restart();
    return;
  }

  // Setup previously completed -- reconnect with stored credentials
  String ssid = preferences.getString("wifi_ssid", "");
  String pw   = preferences.getString("wifi_pw",   "");
  if (ssid.length() == 0) {
    Serial.println("  setup_done=true but no SSID in NVS -- forcing portal");
    preferences.putBool("setup_done", false);
    ESP.restart();
    return;
  }

  // Reload teacher-entered config
  String shn = preferences.getString("holly_name", "");
  String scr = preferences.getString("classroom",  "");
  String syg = preferences.getString("yearGroupInput", yearGroupInput);
  shn.toCharArray(hollyName,      sizeof(hollyName));
  scr.toCharArray(classroomName,  sizeof(classroomName));
  syg.toCharArray(yearGroupInput, sizeof(yearGroupInput));
  computeYearBandFromInput(yearGroupInput, yearBand, sizeof(yearBand));

  Serial.printf("  Connecting to: %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pw.c_str());
  buildDeviceId();  // v5045: build deviceId now (MAC available after WiFi.begin) so post-WiFi banner shows it

  // Boot rainbow while connecting — LEDs visible from power-on
  {unsigned long rs=millis();uint16_t ho=0;float sp=3.0f;
    while(WiFi.status()!=WL_CONNECTED && millis()-rs<30000){
      esp_task_wdt_reset();
      float pr=clampf((float)(millis()-rs)/30000.0f,0.0f,1.0f);
      sp=3.0f*(1.0f-pr*0.7f);
      for(int i=0;i<NUM_LEDS;i++)strip.setPixelColor(i,wheel(((i*256)/NUM_LEDS+ho)&0xFF));
      strip.show();ho+=(uint16_t)sp;delay(16);}}

  if (WiFi.status() == WL_CONNECTED) {
    internetConnected = true;
    digitalWrite(STATUS_LED, HIGH);
    // v5044: Re-print version banner now that serial monitor is almost
    // certainly connected. The banner at start of setup() prints within
    // ~50ms of boot — before most serial monitors finish reconnecting,
    // so it's typically lost. This second print guarantees a visible
    // version stamp in every captured log.
    Serial.println("\n==============================================");
    Serial.printf("  Holly_RDiQ5000 v%s (post-WiFi banner)\n", FW_VERSION);
    Serial.printf("  Device ID: %s | IP: %s\n", deviceId, WiFi.localIP().toString().c_str());
    Serial.println("==============================================");
  } else {
    internetConnected = false;
    digitalWrite(STATUS_LED, LOW);
    Serial.println("  Wi-Fi timeout -- continuing, will retry in background");
  }

  // Resolve active thresholds
  Thresholds bth = applyGuardrails(thresholdsForBand(yearBand), yearBand);
  Thresholds bdf = thresholdsForBand(yearBand);
  CAL_TH.greenMax  = preferences.getFloat("rc_g", bdf.greenMax);
  CAL_TH.amberMax  = preferences.getFloat("rc_a", bdf.amberMax);
  CAL_TH.redWarnDb = preferences.getFloat("rc_r", bdf.redWarnDb);
  CAL_TH = applyGuardrails(CAL_TH, yearBand);
  roomCalComplete   = preferences.getBool("rc_complete", false);
  roomCalStartEpoch = (time_t)preferences.getLong("rc_start", 0);
  ACTIVE_TH = (roomCalEnabled && roomCalComplete) ? CAL_TH : bth;

  Serial.printf("  band=%s th=(G<=%.1f,A<=%.1f,R<=%.1f)\n",
                yearBand, ACTIVE_TH.greenMax, ACTIVE_TH.amberMax, ACTIVE_TH.redWarnDb);
}
 
// =====================
//   Arduino Setup
// =====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n==============================================");
  Serial.printf("  Holly_RDiQ5000 v%s\n", FW_VERSION);
  Serial.println("==============================================");
  bootTimeMs = millis();
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info){
    // v5050: This callback runs on the WiFi/lwIP task. Calling WiFi.localIP()
    // or Serial.printf() here can race with the lwIP core lock and trigger:
    //   "assert failed: udp_new_ip_type ... Required to lock TCPIP core
    //    functionality!"
    // ...which is the random crash Dan was seeing on plug-in. Now we ONLY
    // set primitive flags; loop() observes them and does the actual work.
    if(e==ARDUINO_EVENT_WIFI_STA_GOT_IP){
      wifiGotIpFlag = true;
      wifiReconnectedAtMs = millis();
      wifiRetryBackoffStep = 0;
      wifiDisconnectedAtMs = 0;
    }
    else if(e==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){
      wifiDisconnectFlag = true;
      wifiDisconnectReason = info.wifi_sta_disconnected.reason;
      if (wifiDisconnectedAtMs == 0) wifiDisconnectedAtMs = millis();
      // v5027: Do NOT call WiFi.begin() here — main loop retry handles it.
      // Calling WiFi.begin() from both the event handler AND the retry timer
      // causes "sta is connecting, return error" spam when they fight each other.
    }
  });
  preferences.begin("classroom",false);
  strlcpy(lastResetReason,getResetReasonString(),sizeof(lastResetReason));
  Serial.printf("  Reset reason: %s\n",lastResetReason);
  String shn=preferences.getString("holly_name",""),scr=preferences.getString("classroom","");
  String syg=preferences.getString("yearGroupInput","Y2"),sbd=preferences.getString("yearBand","Y2Y3");
  useTimetable=preferences.getBool("use_timetable",true);
  roomCalEnabled=preferences.getBool("rc_enabled",false);roomCalComplete=preferences.getBool("rc_complete",false);
  if(preferences.getBool("skip_learn",false)){Serial.println("  Learning mode was skipped at setup -- registry thresholds will apply");}
  roomCalStartEpoch=(time_t)preferences.getLong("rc_start",0);
  calCount=preferences.getULong("rc_count",0);calMean=preferences.getDouble("rc_mean",0.0);calM2=preferences.getDouble("rc_m2",0.0);
  calMinDb=preferences.getFloat("rc_min",9999.0f);calMaxDb=preferences.getFloat("rc_max",-9999.0f);
  nvsConfigVersion=preferences.getULong("cfg_version",0);manualOverride=preferences.getBool("manual_override",false);
  schoolOpen=preferences.getBool("school_open",true);
  String sr=preferences.getString("reopen_date","");sr.toCharArray(reopenDate,sizeof(reopenDate));
  sendCount=preferences.getInt("send_count",0);
  String ss=preferences.getString("sens_profile","standard");ss.toCharArray(sensitivityProfile,sizeof(sensitivityProfile));
  sensitivityMultiplier=getSensitivityMultiplier(sensitivityProfile);
  micFloorValid=preferences.getBool("mic_floor_ok",false);micFloor=preferences.getFloat("mic_floor",MIC_FLOOR_DEFAULT);
  String sfd=preferences.getString("mic_floor_date","");sfd.toCharArray(micFloorDate,sizeof(micFloorDate));
  Serial.printf("  Mic floor: %.1f dB (valid=%s)\n",micFloor,micFloorValid?"YES":"NO");
  // v5077: Load per-unit mic offset correction (cloud-pushed via Apps Script
  // v49+). Default 0.0 means this Holly's mic matches the fleet baseline
  // and no correction is needed. Applied additively in readMicDBSingle().
  micOffsetCorrection = preferences.getFloat("mic_offset_corr", 0.0f);
  Serial.printf("  Mic offset correction: %+.2f dB %s\n", micOffsetCorrection,
                (micOffsetCorrection == 0.0f) ? "(none)" : "(applied)");
  // v5072: Boot-time learning auto-activation. Retrofits the Lyppard fleet
  // (and any other devices) where mic floor is valid but rc_enabled was
  // never set due to the original v5071 bug. Idempotent — only fires
  // once, and only when learning hasn't already completed.
  //
  // v5084: BUGFIX. The previous condition (!roomCalEnabled) couldn't
  // distinguish "rc_enabled was never set in NVS" (true retrofit case)
  // from "rc_enabled was explicitly set to false by cancel_learning"
  // (admin deliberately disabled). After cancel + flash, the retrofit
  // would re-enable learning on boot — fighting the cancel.
  // Now uses isKey() — the key exists in NVS as soon as ANYTHING has
  // set it (captive-portal first setup, prior retrofit fire, or the
  // cancel handler). Its presence means state has been deliberate;
  // its absence means genuine retrofit case.
  if (micFloorValid && !preferences.isKey("rc_enabled") && !roomCalComplete) {
    roomCalEnabled = true;
    preferences.putBool("rc_enabled", true);
    Serial.println("  === LEARNING MODE AUTO-ACTIVATED ON BOOT (retrofit) ===");
  }
  shn.toCharArray(hollyName,sizeof(hollyName));scr.toCharArray(classroomName,sizeof(classroomName));
  syg.toCharArray(yearGroupInput,sizeof(yearGroupInput));sbd.toCharArray(yearBand,sizeof(yearBand));
  Thresholds bdf=thresholdsForBand(yearBand);
  CAL_TH.greenMax=preferences.getFloat("rc_g",bdf.greenMax);CAL_TH.amberMax=preferences.getFloat("rc_a",bdf.amberMax);CAL_TH.redWarnDb=preferences.getFloat("rc_r",bdf.redWarnDb);
  CAL_TH=applyGuardrails(CAL_TH,yearBand);resolveActiveThresholds();
  Serial.printf("  band=%s th=(G<=%.1f,A<=%.1f,R<=%.1f)\n",yearBand,ACTIVE_TH.greenMax,ACTIVE_TH.amberMax,ACTIVE_TH.redWarnDb);
  Serial.printf("  Config: v=%lu, mo=%s, open=%s\n",(unsigned long)nvsConfigVersion,manualOverride?"ON":"OFF",schoolOpen?"YES":"NO");
  Serial.printf("  SEND: %d, Sensitivity: %s (x%.2f)\n",sendCount,sensitivityProfile,sensitivityMultiplier);
  pinMode(STATUS_LED,OUTPUT);digitalWrite(STATUS_LED,LOW);pinMode(HOLLY_MODE_BTN,INPUT_PULLUP);
  strip.begin();strip.setBrightness(preferences.getUChar("brightness",100));strip.show();
  configTzTime("GMT0BST,M3.5.0/1,M10.5.0/2","pool.ntp.org","time.nist.gov");
  Serial.println("  Waiting for NTP...");
  const esp_task_wdt_config_t wc={.timeout_ms=WDT_TIMEOUT_S*1000,.idle_core_mask=0,.trigger_panic=true};
  esp_err_t we=esp_task_wdt_reconfigure(&wc);if(we==ESP_ERR_INVALID_STATE)esp_task_wdt_init(&wc);
  esp_task_wdt_add(NULL);Serial.printf("  Watchdog armed (%ds)\n",WDT_TIMEOUT_S);
  connectToWiFi();
  char dn[66];snprintf(dn,sizeof(dn),"Holly%s%s",strlen(hollyName)>0?" ":"",strlen(hollyName)>0?hollyName:"");
  Serial.printf("  Identity: %s | Room: %s | ID: %s\n",dn,classroomName,deviceId);
  setupI2SMic();
  if(!bme.begin(0x76)){Serial.println("  BME280 not found!");setLastError("BME280 sensor not found");}
  else Serial.println("  BME280 ok.");
  clearDBHistory();ledDB=calculateAverage();lessonStartMs=millis();
  if(!micFloorValid){
    bool skipRoomTest = preferences.getBool("skip_rt", false);
    if (skipRoomTest) {
      micFloor = MIC_FLOOR_DEFAULT;
      micFloorValid = true;
      preferences.putFloat("mic_floor", micFloor);
      preferences.putBool("mic_floor_ok", true);
      strlcpy(micFloorDate, "skipped-at-setup", sizeof(micFloorDate));
      preferences.putString("mic_floor_date", micFloorDate);
      Serial.printf("  Room test skipped by user -- mic floor seeded to %.1f dB\n", micFloor);
    } else {
      Serial.println("  No mic floor -- auto-starting room test.");
      startMicFloorTest();
    }
  }

  // v5071: log boot event (fire-and-forget audit trail).
  postEvent("boot", FW_VERSION, lastResetReason);

  // v5071: if previous boot wrote a pending OTA marker, flush it now.
  // Marker is set by the OTA flash code once a new binary boots successfully
  // (via Update.end() returning true and the NVS flag below).
  String prevFw = preferences.getString("ota_prev_fw", "");
  if (prevFw.length() > 0) {
    char dbuf[64]; snprintf(dbuf, sizeof(dbuf), "Previous: %s", prevFw.c_str());
    postEvent("ota_completed", FW_VERSION, dbuf);
    preferences.remove("ota_prev_fw");
    Serial.printf("  Logged ota_completed (was %s)\n", prevFw.c_str());
  }

}
 
// =====================
//   Main Loop
// =====================
void loop() {
  esp_task_wdt_reset();
  // v5050: Drain WiFi event flags. The event callback only sets these flags
  // — actual logging and state lookups happen here in main task context, so
  // they don't race against the lwIP core lock.
  if (wifiGotIpFlag) {
    wifiGotIpFlag = false;
    Serial.printf("  Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
  }
  if (wifiDisconnectFlag) {
    wifiDisconnectFlag = false;
    Serial.printf("  Wi-Fi disconnected (reason %d)\n", (int)wifiDisconnectReason);
  }
  bool cbs;
  // v5080: Edge-only debounce. Only run the 5-sample confirm when the
  // raw read disagrees with lastButtonState. Same noise rejection on
  // transitions, zero cost when the button is steady. Saves ~25ms of
  // loop lag per iteration.
  {
    int raw = digitalRead(HOLLY_MODE_BTN);
    if (raw == lastButtonState) {
      cbs = (raw == LOW) ? LOW : HIGH;
    } else {
      int lowCount = 0;
      for (int i = 0; i < 5; i++) {
        if (digitalRead(HOLLY_MODE_BTN) == LOW) lowCount++;
        delayMicroseconds(5000);
      }
      // Confirm transition: require unanimous agreement
      if (raw == LOW)  cbs = (lowCount == 5) ? LOW  : HIGH;
      else             cbs = (lowCount == 0) ? HIGH : LOW;
    }
  }
  unsigned long nowMs = millis();

  // v5040: Button release — handle reset-zone releases AND reset state
  if(cbs==HIGH&&lastButtonState==LOW){
    // If we were in a reset hold zone, execute the corresponding reset
    if(resetHoldActive){
      uint8_t zone = resetHoldZone;
      endResetHold();
      if(zone == 2){performFactoryReset(); /* doesn't return */}
      else if(zone == 1){performWifiReset(); /* doesn't return */}
    }
    buttonPressStart=0;
    wipeTriggered=false;
  }

  // Button press edge — start tracking + Holly Hop toggle (existing behaviour)
  if(cbs==LOW&&lastButtonState==HIGH){
    if(nowMs-lastToggleAt>=DEBOUNCE_MS){
      buttonPressStart=nowMs;
      wipeTriggered=false;
      lastToggleAt=nowMs;
      if(locateActive){
        locateActive=false;endLocateMode();
        Serial.println("  Locate mode cancelled by button press.");
      }
      else if(deviceMode==MODE_HOLLY_HOP)beginExitHollyHop(nowMs,"manual");
      else enterHollyHop(nowMs);
    }
  }

  // Button held — progress through reset zones
  if(cbs==LOW && buttonPressStart!=0){
    unsigned long heldMs = nowMs - buttonPressStart;
    uint8_t newZone = 0;
    if(heldMs >= RESET_FACTORY_HOLD_MS)      newZone = 2;  // red
    else if(heldMs >= RESET_WIFI_HOLD_MS)    newZone = 1;  // amber
    // Transition into/between zones — log + take over LEDs
    if(newZone != resetHoldZone){
      if(newZone == 1 && resetHoldZone == 0){
        Serial.println("  Button held 10s — release to reset WiFi (amber).");
        resetHoldActive = true;
      }
      else if(newZone == 2){
        Serial.println("  Button held 20s — release to FACTORY RESET (red).");
      }
      resetHoldZone = newZone;
    }
    // Render the glow while held in a reset zone
    if(resetHoldActive) renderResetHold(resetHoldZone);
  }

  lastButtonState=cbs;

  // v5040: While the user is holding past 10s, short-circuit the rest of
  // the loop. The reset glow has taken over the LEDs; we don't want
  // subsequent rendering paths (zones, hop, sleep rainbow) to repaint over
  // it. Heartbeat POSTs are also paused — they'd resume on release anyway.
  if(resetHoldActive) return;
 
  // v5038: Deferred remote actions — processRemoteConfig only sets flags
  // to avoid crashing inside the HTTP response handler. We do the real
  // work here on a clean stack with WiFi idle.
  if(remoteRebootRequested){
    Serial.println("\n  *** REMOTE REBOOT ***");
    Serial.flush(); delay(2000); ESP.restart();
  }
  if(locateCancelRequested){
    locateCancelRequested=false;
    Serial.println("  *** REMOTE LOCATE CANCEL ***");
    if(locateActive){locateActive=false;endLocateMode();Serial.println("  Locate mode cancelled remotely.");}
    locateRequested=false;
  }
  if(remoteConfigVersionPending!=0){
    Serial.printf("  --- Config updated to v%lu ---\n",(unsigned long)remoteConfigVersionPending);
    remoteConfigVersionPending=0;
  }

  // v5080: Drain deferred remote-config flags here on a clean stack.
  // These were previously executed inside processRemoteConfig() while
  // still on the postToSheets() TLS stack — see header changelog.
  if (remoteSetRoomCalEnabledRequested) {
    remoteSetRoomCalEnabledRequested = false;
    Serial.println("  === REMOTE: LEARNING MODE START/RESET ===");
    time_t nowEpoch = time(NULL);
    roomCalEnabled = true;
    preferences.putBool("rc_enabled", true);
    resetRoomCalibration(nowEpoch);
    if (manualOverride) {
      manualOverride = false;
      preferences.putBool("manual_override", false);
      Serial.println("  Manual override cleared (learning takes precedence)");
    }
    resolveActiveThresholds();
    char dbuf[40];
    snprintf(dbuf, sizeof(dbuf), "%lu day window (admin)", (unsigned long)ROOM_CAL_DAYS);
    postEvent("learning_reset", "", dbuf);
  }
  if (remoteCancelLearningRequested) {
    remoteCancelLearningRequested = false;
    Serial.println("  === REMOTE: LEARNING CANCELLED ===");
    roomCalEnabled = false;
    roomCalComplete = false;
    preferences.putBool("rc_enabled", false);
    preferences.putBool("rc_complete", false);
    calCount = 0; calMean = 0.0; calM2 = 0.0;
    calMinDb = 9999.0f; calMaxDb = -9999.0f;
    roomCalStartEpoch = 0;
    preferences.putULong("rc_count", 0);
    preferences.putDouble("rc_mean", 0.0);
    preferences.putDouble("rc_m2", 0.0);
    preferences.putFloat("rc_min", 9999.0f);
    preferences.putFloat("rc_max", -9999.0f);
    preferences.putLong("rc_start", 0);
    resolveActiveThresholds();
    Serial.printf("  Active thresholds reverted to band default: G<=%.1f A<=%.1f R<=%.1f\n",
                  ACTIVE_TH.greenMax, ACTIVE_TH.amberMax, ACTIVE_TH.redWarnDb);
    postEvent("learning_cancelled", "", "admin cancel");
  }
  if (remoteMicOffsetUpdateRequested) {
    remoteMicOffsetUpdateRequested = false;
    if (fabs(pendingMicOffsetCorrection - micOffsetCorrection) > 0.05f) {
      float oldOffset = micOffsetCorrection;
      micOffsetCorrection = pendingMicOffsetCorrection;
      preferences.putFloat("mic_offset_corr", micOffsetCorrection);
      Serial.printf("  === REMOTE: MIC OFFSET CORRECTION %+.2f -> %+.2f dB ===\n",
                    oldOffset, micOffsetCorrection);
      char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "%+.2f", micOffsetCorrection);
      char dbuf[64]; snprintf(dbuf, sizeof(dbuf), "was %+.2f, fleet auto-correction", oldOffset);
      postEvent("mic_offset_updated", vbuf, dbuf);
    }
  }
  if (remoteClearLastErrorRequested) {
    remoteClearLastErrorRequested = false;
    if (strlen(lastError) > 0) {
      Serial.printf("  === REMOTE: CLEARING LAST ERROR (was: \"%s\") ===\n", lastError);
      char old[64]; strlcpy(old, lastError, sizeof(old));
      lastError[0] = '\0';
      lastErrorMs = 0;
      postEvent("last_error_cleared", "", old);
    } else {
      Serial.println("  === REMOTE: clear_last_error received (buffer already empty) ===");
    }
  }

  // v5082: Drain deferred applyRemoteConfig request. Re-parses the stashed
  // response body on a clean stack and runs the heavy config-application
  // path (year group, classroom, threshold edits, timetable times, etc.)
  // here rather than inside the postToSheets() TLS stack.
  if (remoteApplyConfigRequested) {
    remoteApplyConfigRequested = false;
    if (pendingConfigBody.length() > 0) {
      DynamicJsonDocument cfgDoc(4096);
      DeserializationError err = deserializeJson(cfgDoc, pendingConfigBody);
      if (!err && cfgDoc["ok"].as<bool>() && cfgDoc.containsKey("config")) {
        applyRemoteConfig(cfgDoc);
        nvsConfigVersion = pendingConfigVersion;
        preferences.putULong("cfg_version", nvsConfigVersion);
        remoteConfigVersionPending = pendingConfigVersion;
      } else {
        Serial.println("  [deferred] applyRemoteConfig: re-parse failed, skipping");
      }
    }
    pendingConfigBody = "";  // free heap
    pendingConfigVersion = 0;
  }

  // Mic floor test intercept
  // v5076: Heartbeats fire during the test so dashboard shows the
  // device as alive ("calibrating") rather than offline. The heartbeat
  // payload omits sound dB, so mic floor accuracy is unaffected.
  if(micFloorTestActive){
    if(nowMs-micFloorTestStartMs>=MIC_FLOOR_TEST_DURATION_MS){finalizeMicFloorTest();}
    else{
      if(nowMs-lastSampleTime>=MIC_FLOOR_SAMPLE_INTERVAL){updateMicFloorTest();lastSampleTime=nowMs;}
      // v5076: Heartbeat during mic test. Uses fast-cadence interval if
      // we're still in the boot window. Suppresses telemetry POSTs (the
      // dB readings would be misleading mid-test), but heartbeats are safe.
      const bool inBootWindow = (nowMs - bootTimeMs) < FAST_CADENCE_DURATION_MS;
      const unsigned long hbInterval = inBootWindow ? FAST_HEARTBEAT_INTERVAL_MS : HEARTBEAT_INTERVAL_MS;
      if (internetConnected && (nowMs - lastPostTime >= hbInterval)) {
        temperature = bme.readTemperature();
        humidity    = bme.readHumidity();
        pressure    = bme.readPressure() / 100.0F;
        postHeartbeat();
        lastPostTime = nowMs;
      }
      showMicFloorTestAnimation();return;
    }
  }
  if(micFloorTestRequested){micFloorTestRequested=false;Serial.println("  Remote room test starting.");startMicFloorTest();return;}

  // v5037/v5038: Locate mode intercept — runs the 60s magenta/cyan flash loop,
  // takes over the LEDs and blocks all other states. Exits on timeout or
  // physical button press (handled in the button section above via the
  // locateActive flag — see main button handler).
  // v5038: Serial output is now safe here — we're on a clean main-loop stack,
  // not inside postToSheets(). processRemoteConfig() only sets the flag.
  if(locateRequested){
    locateRequested=false;
    locateActive=true;
    locateStartMs=nowMs;
    Serial.println("  *** REMOTE LOCATE REQUESTED ***");
    Serial.println("  Locate mode starting (60s).");
  }
  if(locateActive){
    if(nowMs-locateStartMs>=LOCATE_DURATION_MS){
      locateActive=false;
      endLocateMode();
      Serial.println("  Locate mode ended (timeout).");
    }else{
      renderLocateMode();
      return;  // block all other LED rendering and state logic
    }
  }

  // v5035: Boot check-in — fires ONCE per boot, as soon as WiFi is up and
  // the post-reconnect grace period has elapsed. Covers three scenarios:
  //   1. First-boot registration (nvsConfigVersion == 0) — full telemetry
  //      so the device appears in the admin dashboard immediately.
  //   2. Reboot / crash recovery — heartbeat so the dashboard knows Holly
  //      came back up and to confirm the running firmware version.
  //   3. Dev flashing — developer sees "Boot check-in" in the log within
  //      ~15 seconds of flash, not 3 minutes later.
  // v5076: First-ever boot check-in fires the moment WiFi associates,
  // bypassing POST_RECONNECT_GRACE_MS. The grace period exists to let
  // TLS settle after a recovery drop — but on the first WiFi connect
  // after a fresh boot, there's nothing to recover from, and waiting
  // 10s adds nothing but operator anxiety. Subsequent reconnects keep
  // the grace requirement (its original purpose).
  static bool bootCheckinDone = false;
  if (!bootCheckinDone
      && internetConnected
      && wifiReconnectedAtMs != 0
      && (
           // First-ever check-in: no grace required (fresh boot).
           (nvsConfigVersion == 0)
           // Subsequent check-ins still wait for grace.
           || ((millis() - wifiReconnectedAtMs) >= POST_RECONNECT_GRACE_MS)
         )) {
    bootCheckinDone = true;
    const bool isFirstBoot = (nvsConfigVersion == 0);
    // Compute current window state locally — main loop's tib/ws vars
    // aren't populated yet at this point in the loop iteration.
    time_t btNow = time(NULL);
    struct tm btTi;
    localtime_r(&btNow, &btTi);
    // v5081: schoolOpen guard, same reasoning as the main POST scheduler.
    const bool inActiveMonitoring =
      schoolOpen
      && ((getWindowState(btTi.tm_wday, btTi.tm_hour, btTi.tm_min) == LESSON)
          || (!useTimetable));
    Serial.printf("  Boot check-in (%s)\n",
                  isFirstBoot ? "first-boot registration"
                              : (inActiveMonitoring ? "telemetry" : "heartbeat"));
    temperature = bme.readTemperature();
    humidity    = bme.readHumidity();
    pressure    = bme.readPressure() / 100.0F;
    if (isFirstBoot || inActiveMonitoring) {
      logToGoogleSheets(calculateAverage());   // full telemetry
    } else {
      postHeartbeat();                         // heartbeat only
    }
    // v5072: Boot-time POST jitter. Backdating lastPostTime by a random
    // 0-30000ms means the next periodic POST fires anywhere between
    // 30 seconds and 60 seconds from now, instead of exactly 60 seconds.
    // Across a 14-device fleet this naturally desyncs the cadence and
    // drops Apps Script LockService queue depth from O(fleet) to O(1).
    // One-shot per boot — after this, the normal 60s/3min cadence runs.
    // randomSeed() is implicitly seeded by the noise floor of the ADC
    // and the time taken to reach this point — sufficient for jitter.
    unsigned long jitter = (unsigned long)random(0, 30000);
    lastPostTime = millis() - jitter;
    Serial.printf("  POST jitter: next periodic in ~%lu sec\n",
                  (unsigned long)((LESSON_INTERVAL_MS - jitter) / 1000UL));
  }
 
  if(nowMs-lastSampleTime>=SAMPLE_INTERVAL_MS){float db=readMicDB();updateRollingAverage(db);
    if(sendCount>0)updateSpikeDetection(db,calculateAverage());lastSampleTime=nowMs;hasNewSampleForLed=true;}
 
  time_t tnow=time(NULL);struct tm tib;localtime_r(&tnow,&tib);
  int hour=tib.tm_hour,minute=tib.tm_min,wday=tib.tm_wday;
  WindowState ws=getWindowState(wday,hour,minute);
  if(!otaBootCheckDone)otaBootCheck();else if(nowMs-lastOtaCheckMs>=OTA_CHECK_INTERVAL_MS){otaDailyCheck();lastOtaCheckMs=nowMs;}
  if(ws==LESSON&&lastWs!=LESSON){lessonStartMs=nowMs;quietStartMs=0;resetSendLessonState();}
  lastWs=ws;
  float avgDB=calculateAverage();
  if(hasNewSampleForLed){ledDB=(1.0f-LED_SMOOTH_ALPHA)*ledDB+LED_SMOOTH_ALPHA*avgDB;hasNewSampleForLed=false;}
  currentZone=applyStickyHysteresis(currentZone,ledDB);
 
  // v5031: Wi-Fi reconnect logic — cooperates with setAutoReconnect.
  //
  // Key design decisions:
  //   1. Use WiFi.reconnect() instead of WiFi.disconnect(false)+WiFi.begin().
  //      The old pattern forcibly tore down the radio every cycle, which
  //      interrupted any in-progress auto-reconnect attempt and restarted
  //      from scratch. On marginal APs this can actively prevent recovery.
  //   2. Let auto-reconnect heal transient drops on its own. Don't even
  //      try to nudge until WIFI_FIRST_RETRY_GRACE_MS has elapsed since
  //      the disconnect event.
  //   3. Log WiFi state transitions (not just "not connected") — gives
  //      much better diagnostics on what the radio is actually doing.
  //   4. Keep v5030's progressive backoff (10s -> 20s -> 40s -> 60s)
  //      for the subsequent nudges if auto-reconnect fails to recover.
  wl_status_t curWifiStatus = WiFi.status();

  // Log state transitions (helps diagnose "stuck" states)
  if (curWifiStatus != lastWifiStatusLogged) {
    const char* statusStr =
      (curWifiStatus == WL_CONNECTED)       ? "CONNECTED"      :
      (curWifiStatus == WL_IDLE_STATUS)     ? "IDLE"           :
      (curWifiStatus == WL_NO_SSID_AVAIL)   ? "NO_SSID_AVAIL"  :
      (curWifiStatus == WL_CONNECT_FAILED)  ? "CONNECT_FAILED" :
      (curWifiStatus == WL_CONNECTION_LOST) ? "CONNECTION_LOST":
      (curWifiStatus == WL_DISCONNECTED)    ? "DISCONNECTED"   : "OTHER";
    Serial.printf("  Wi-Fi status: %s (%d)\n", statusStr, (int)curWifiStatus);
    lastWifiStatusLogged = curWifiStatus;
  }

  if (curWifiStatus != WL_CONNECTED) {
    // Determine current backoff interval (v5030's progressive backoff kept)
    unsigned long backoffMs = WIFI_RETRY_MS;
    if      (wifiRetryBackoffStep == 1) backoffMs = 20000UL;
    else if (wifiRetryBackoffStep == 2) backoffMs = 40000UL;
    else if (wifiRetryBackoffStep >= 3) backoffMs = 60000UL;

    // v5031: First-retry grace — give auto-reconnect a chance before we nudge
    bool inFirstGrace = (wifiDisconnectedAtMs != 0) &&
                        ((nowMs - wifiDisconnectedAtMs) < WIFI_FIRST_RETRY_GRACE_MS) &&
                        (wifiRetryBackoffStep == 0);

    if (!inFirstGrace && (nowMs - lastWifiRetry >= backoffMs)) {
      setLastError("WiFi disconnected");
      // v5031: Gentle nudge — cooperate with auto-reconnect instead of
      // tearing the radio down. Auto-reconnect continues running in the
      // background; reconnect() just kicks it to try again now.
      WiFi.reconnect();
      lastWifiRetry = nowMs;
      if (wifiRetryBackoffStep < 3) wifiRetryBackoffStep++;
    }
  }
  bool wn=(WiFi.status()==WL_CONNECTED);
  if(wn!=internetConnected){internetConnected=wn;digitalWrite(STATUS_LED,wn?HIGH:LOW);if(wn&&strlen(lastError)>0&&strstr(lastError,"WiFi"))lastError[0]='\0';}
 
  static unsigned long lastTP=0;
  if(nowMs-lastTP>=5000){char dn[66];snprintf(dn,sizeof(dn),"Holly%s%s",strlen(hollyName)>0?" ":"",strlen(hollyName)>0?hollyName:"");
    Serial.printf("  %02d:%02d|ws=%d|LED=%s|sleep=%s|wifi=%s|avg=%.1f|led=%.1f|v=%.0f%%|z=%d|band=%s|th=(%.1f,%.1f,%.1f)|%s|%s|%s|cv=%lu|mo=%s|open=%s|floor=%.1f\n",
      hour,minute,ws,getCurrentLEDState().c_str(),inSleepMode?"Y":"N",(WiFi.status()==WL_CONNECTED)?"Y":"N",
      avgDB,ledDB,voicePercent,(int)currentZone,yearBand,ACTIVE_TH.greenMax,ACTIVE_TH.amberMax,ACTIVE_TH.redWarnDb,
      dn,classroomName,deviceId,(unsigned long)nvsConfigVersion,manualOverride?"Y":"N",schoolOpen?"Y":"N",micFloor);
    // v5029: Heap + uptime diagnostics — detect memory leaks/fragmentation
    Serial.printf("  HEAP free=%u min=%u maxalloc=%u | uptime=%lus | rssi=%d\n",
      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
      (unsigned long)((millis()-bootTimeMs)/1000UL), WiFi.RSSI());
    lastTP=nowMs;}
 
  if(deviceMode==MODE_HOLLY_HOP){
    if(hollyHopEndAt!=0&&nowMs>=hollyHopEndAt) beginExitHollyHop(nowMs,"timer");
    if(deviceMode==MODE_HOLLY_HOP){
      // v5081: Heartbeat during Holly Hop. Same pattern as v5076 mic
      // floor test. Keeps the dashboard alive ("paused, on break")
      // instead of flagging the device offline for 30 minutes.
      // Telemetry stays suppressed — sound during a deliberate break
      // is misleading. Side benefit: the hop START log finally reaches
      // the backend (was previously overwritten by END before any POST
      // could fire).
      const bool hopBootWindow = (nowMs - bootTimeMs) < FAST_CADENCE_DURATION_MS;
      const unsigned long hopHbInterval = hopBootWindow ? FAST_HEARTBEAT_INTERVAL_MS : HEARTBEAT_INTERVAL_MS;
      if (internetConnected && (nowMs - lastPostTime >= hopHbInterval)) {
        temperature = bme.readTemperature();
        humidity    = bme.readHumidity();
        pressure    = bme.readPressure() / 100.0F;
        if (hopLogPending) {
          // Fire the queued hop event (START on first heartbeat after enter,
          // or END if the timer fired and we haven't yet exited the loop).
          hopLogPending = false;
          logHollyHopEvent(hopLogLabel);
        } else {
          postHeartbeat();
        }
        lastPostTime = nowMs;
      }
      renderSleepingRainbow();
      return;
    }
  }
  if(rewardShowing){unsigned long t=millis();if(t-rewardStartMs<REWARD_DURATION_MS){showQuietReward(t);return;}
    else{rewardShowing=false;quietStartMs=0;Serial.println("  Quiet reward finished");}}
 
  // v5035: Dormant mode auto-reopen check (unchanged)
  if(!schoolOpen){if(strlen(reopenDate)>=10){struct tm ti;if(getLocalTime(&ti)){char td[12];
    snprintf(td,sizeof(td),"%04d-%02d-%02d",ti.tm_year+1900,ti.tm_mon+1,ti.tm_mday);
    if(strcmp(td,reopenDate)>=0){schoolOpen=true;autoReopened=true;reopenDate[0]='\0';preferences.putBool("school_open",true);preferences.putString("reopen_date","");}}}}

  // v5036: Unified POST scheduler — runs for all states, outside the LED branches.
  //   - Telemetry (full sound data) when ws==LESSON or timetable is disabled AND awake
  //   - Heartbeat (health only) in every other state, INCLUDING when asleep
  // Every POST pulls config on the response, so admin changes propagate within
  // 60s-3min depending on whether Holly is in an active monitoring state.
  //
  // v5036 bugfix: `!inSleepMode` was in the outer gate, which silently killed
  // ALL communication whenever Holly went to sleep in a quiet classroom. The
  // device would stop check-ins entirely, the dashboard would flag it offline,
  // and remote config (including wake-up from sleep state) couldn't be pushed.
  // Now sleep only downgrades the payload to a heartbeat — the pipe stays open.
  {
    // v5081: schoolOpen now gates telemetry. Without this, a device
    // with schoolOpen=false during a school holiday would still log
    // sound_dB to the school data tab whenever the timetable's lesson
    // hours align with a real-world clock — empty-classroom readings
    // polluting reports. Heartbeats still fire (the dashboard needs
    // to see the device alive); telemetry suppressed.
    const bool inActiveMonitoring = schoolOpen && ((ws == LESSON) || (!useTimetable));
    // v5076: Fast-cadence boot window. For first 10 minutes after boot,
    // use accelerated intervals so operators see new devices immediately.
    // After the window, revert to standard 60s/180s.
    const bool inBootWindow = (nowMs - bootTimeMs) < FAST_CADENCE_DURATION_MS;
    const unsigned long lessonInt    = inBootWindow ? FAST_LESSON_INTERVAL_MS    : LESSON_INTERVAL_MS;
    const unsigned long heartbeatInt = inBootWindow ? FAST_HEARTBEAT_INTERVAL_MS : HEARTBEAT_INTERVAL_MS;
    const unsigned long interval  = inActiveMonitoring && !inSleepMode
                                      ? lessonInt
                                      : heartbeatInt;

    if (internetConnected && (nowMs - lastPostTime >= interval)) {
      temperature = bme.readTemperature();
      humidity    = bme.readHumidity();
      pressure    = bme.readPressure() / 100.0F;

      // v5028 rule preserved: only ONE POST per scheduler tick.
      // Deferred event POSTs (Holly Hop, Quiet Reward) take priority —
      // they need to fire promptly even on a heartbeat cycle.
      if (hopLogPending) {
        hopLogPending = false;
        logHollyHopEvent(hopLogLabel);
      } else if (rewardLogPending) {
        rewardLogPending = false;
        logQuietRewardEvent(rewardLogDb);
      } else if (inActiveMonitoring && !inSleepMode) {
        logToGoogleSheets(avgDB);      // full telemetry — awake + active window
      } else {
        postHeartbeat();               // health-only payload — asleep or out-of-hours
      }
      lastPostTime = nowMs;
    }
  }

  // v5035: Dormant LED mode (school closed) — runs after POST scheduler so
  // heartbeats still fire every 3 minutes while school is closed.
  if (!schoolOpen) {
    standbyCyanPulse();
    return;
  }

  // v5035: Active monitoring — either we're in a lesson, or the timetable
  // is disabled (which means Holly behaves as if always in lesson mode).
  if (ws == LESSON || !useTimetable) {
    if(sendCount>0&&useTimetable){int cm=hour*60+minute;updateTransitionTracking(avgDB,nowMs,cm);}

    // v5077: Sleep state machine suppressed entirely during the 14-day
    // learning window. Two reasons:
    //   1. The early-return on line 2981 was skipping Welford accumulation,
    //      so a Holly that fell asleep on day 1 hit the day-14 deadline
    //      with maybe 200 samples instead of 5000+, producing unreliable
    //      thresholds.
    //   2. The rainbow render fires when sleeping, which contradicted the
    //      "Charging" LED state staff/admins expect to see during learning.
    //
    // While learning is active: no sleep transitions, the charging
    // animation renders below, Welford counts every read.
    // Once learning completes: sleep returns to normal behavior.
    const bool isLearning = roomCalEnabled && !roomCalComplete;
    if (isLearning) {
      // Force-clear sleep state if we somehow entered it before this fix
      // landed (carries over from old firmware via NVS).
      if (inSleepMode) inSleepMode = false;
      silenceStart = 0;
    } else {
      // Sleep/wake (offset-based from mic floor) — normal post-learning behavior
      float sleepTh=micFloor+SLEEP_OFFSET_DB, wakeTh=micFloor+WAKE_OFFSET_DB;
      if(avgDB<sleepTh){if(silenceStart==0)silenceStart=nowMs;
        if(!inSleepMode&&(nowMs-silenceStart>=SILENCE_DURATION_MS))inSleepMode=true;}
      else{silenceStart=0;}
      if(avgDB>=wakeTh&&inSleepMode)inSleepMode=false;
      if(inSleepMode){renderSleepingRainbow();return;}
    }

    time_t nowEpoch=time(NULL);
    if(roomCalEnabled&&!roomCalComplete&&!hasValidTime(roomCalStartEpoch)&&hasValidTime(nowEpoch)){
      roomCalStartEpoch=nowEpoch;preferences.putLong("rc_start",(long)roomCalStartEpoch);
      Serial.printf("  Room cal STARTED (%lu days)\n",(unsigned long)ROOM_CAL_DAYS);
      // v5071: log learning start event
      char dbuf[32]; snprintf(dbuf,sizeof(dbuf),"%lu day window",(unsigned long)ROOM_CAL_DAYS);
      postEvent("learning_started","",dbuf);
    }
    if(calibrationWindowActive(nowEpoch)&&!rewardShowing&&deviceMode==MODE_NORMAL){
      static unsigned long lcms=0;if(millis()-lcms>=LESSON_INTERVAL_MS){updateRoomCalibration(avgDB);lcms=millis();}
      static unsigned long lcps=0;if(millis()-lcps>=600000UL){preferences.putULong("rc_count",calCount);preferences.putDouble("rc_mean",calMean);
        preferences.putDouble("rc_m2",calM2);preferences.putFloat("rc_min",calMinDb);preferences.putFloat("rc_max",calMaxDb);lcps=millis();}}
    finalizeRoomCalibrationIfDue(nowEpoch);
    if(roomCalEnabled&&!roomCalComplete){float cp=0.0f;
      if(hasValidTime(roomCalStartEpoch)&&hasValidTime(nowEpoch)){uint32_t el=(uint32_t)(nowEpoch-roomCalStartEpoch);cp=clampf((float)el/(float)ROOM_CAL_SECONDS*100.0f,0.0f,100.0f);}
      showBatteryCharging(cp);return;}

    // v5027: Reward gate — room must be occupied (avgDB > 30) and not sleeping
    if(!rewardShowing&&!inSleepMode&&deviceMode==MODE_NORMAL&&avgDB>=REWARD_MIN_DB){
      const float spikeDb=ACTIVE_TH.greenMax+REWARD_SPIKE_MARGIN_DB;const bool sg=(currentZone==Z_GREEN);
      if(nowMs-lessonStartMs<MIN_LESSON_BEFORE_REWARD_MS)quietStartMs=0;
      else{if(avgDB>=spikeDb){penaltyUntilMs=nowMs+SPIKE_PENALTY_MS;quietStartMs=0;}
        else if(nowMs<rewardCooldownUntilMs||nowMs<penaltyUntilMs)quietStartMs=0;
        else{if(sg){if(quietStartMs==0)quietStartMs=nowMs;
          if(nowMs-quietStartMs>=QUIET_REWARD_MS){rewardCooldownUntilMs=nowMs+REWARD_COOLDOWN_MS;
            rewardActiveForLog=true;rewardLogUntilMs=millis()+REWARD_LOG_WINDOW_MS;
            rewardLogPending=true;rewardLogDb=avgDB;rewardShowing=true;rewardStartMs=millis();quietStartMs=0;
            Serial.println("  Quiet reward triggered!");return;}}
          else quietStartMs=0;}}}
    if(rewardActiveForLog&&nowMs>=rewardLogUntilMs)rewardActiveForLog=false;
    static unsigned long llu=0;if(nowMs-llu>=LED_FRAME_MS){updateLEDs(ledDB);llu=nowMs;}
    return;
  }

  // v5035: Outside any active monitoring state. Timetable is enabled,
  // school is open, but we're not in a lesson window.
  // Per spec: LEDs OFF during assembly/break/lunch AND outside school hours
  // AND on weekends. Heartbeat POSTs handled above.
  strip.clear();
  strip.show();
}
