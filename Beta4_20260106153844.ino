/* 
==============================================
  Classroom Sounds Ltd - Holly_RDiQ5000
  A Chestnut Holdings Company
  Developed by Daniell Lee
  Version: 06/01/26  (FIXED LED BEHAVIOUR + ANTI-GAMING REWARD RULES)
  FIXES ADDED:
    ✅ MIC_DB_OFFSET (calibrate mic dB scale so thresholds behave)
    ✅ LED smoothing now updates ONLY on new sample tick (no twitch)
    ✅ Proper hysteresis across ALL zones (sticky behaviour)
    ✅ LED logic uses ledDB consistently
    ✅ Optional: slightly more forgiving Y2Y3 defaults (toggle below)
    ✅ Anti-gaming reward rules + cooldown + spike penalty
  Features:
    - SEND profile
    - Configurable timetable (via Wi-Fi portal)
    - Toggle: use timetable vs always-on (24/7 LESSON)
    - Quiet reward wand (120s green → 3s white swirl)
    - Reward logging (QUIET_REWARD, persistent for logs)
    - Holly Hop logging
    - No flashing red (steady only, softened for SEND)
    - Room calibration (adaptive thresholds with guardrails)
      - 5 days learning (LESSON only)
      - Portal toggle on/off
      - Auto-start once time is valid (NTP)
==============================================
*/

// === Core Libraries ===
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <driver/i2s.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

// ==== Compatibility guard for ESP-IDF version macros ====
#ifndef ESP_IDF_VERSION_MAJOR
  #define ESP_IDF_VERSION_MAJOR 3
#endif

// =====================
//   TUNING SWITCHES
// =====================

// If you want Y2Y3 to be more "forgiving" / greener more often, set to 1
#define FORGIVING_Y2Y3_DEFAULTS 1

// ✅ FIX: mic dB scale offset (tune 92–95 typically)
#define MIC_DB_OFFSET 93.0f

// ✅ FIX: LED smoothing alpha (lower = slower)
#define LED_SMOOTH_ALPHA 0.08f

// ✅ FIX: Hysteresis (sticky) margins
// Green/Amber and Amber/Deep are the ones that matter for "stay green"
#define HYS_GA_DEFAULT 0.7f   // Green ↔ Amber
#define HYS_AD_DEFAULT 0.9f   // Amber ↔ Deep
#define HYS_DR_DEFAULT 1.0f   // Deep ↔ Red

// =====================
//   ANTI-GAMING REWARD
// =====================

// Cooldown after a reward is granted (prevents farming)
static const unsigned long REWARD_COOLDOWN_MS = 3UL * 60UL * 1000UL; // 3 min

// Break margin above green threshold to RESET streak (prevents hover farming)
static const float REWARD_BREAK_MARGIN_DB = 1.5f;

// Spike penalty if they shout (locks rewards briefly)
static const unsigned long SPIKE_PENALTY_MS = 2UL * 60UL * 1000UL; // 2 min
static const float REWARD_SPIKE_MARGIN_DB = 8.0f;                 // greenMax + 8dB = spike

// Optional: prevent rewards in first X ms of a lesson
static const unsigned long MIN_LESSON_BEFORE_REWARD_MS = 2UL * 60UL * 1000UL; // 2 min

// === Identity & Hardware ===
Preferences preferences;
Adafruit_BME280 bme;
float temperature = 0.0;
float humidity    = 0.0;
float pressure    = 0.0;

char deviceName[32]     = "Holly_RDiQ1000";
char schoolName[32]     = "Unknown_School";
char yearGroupInput[32] = "Y2";
char yearBand[16]       = "Y2Y3";

#define LED_PIN          26
#define NUM_LEDS         120
#define STATUS_LED       27
#define HOLLY_MODE_BTN   13

// === Mic Pinout ===
#define MIC_SD_PIN   32   // DOUT from mic
#define MIC_WS_PIN   25   // LRCL/WS
#define MIC_SCK_PIN  33   // BCLK

// === Thresholds (dynamic by year band) ===
struct Thresholds { float greenMax; float amberMax; float redWarnDb; };
Thresholds ACTIVE_TH = {43, 48, 52};   // default

// === Timings & Behaviour ===
#define WARNING_DURATION_MS     12000      // (kept for future use)
#define WARNING_DELAY_MS         8000      // (kept for future use)
#define FLASH_INTERVAL_MS         700      // (unused – no flashing)
#define AVG_SAMPLE_COUNT          150      // ~30s @ 200ms
#define SAMPLE_INTERVAL_MS        200
#define SILENCE_DURATION_MS    300000
#define SLEEP_DB_THRESHOLD        30.0
#define WAKE_DB_THRESHOLD         23.0
#define DEBOUNCE_MS               120
#define BOOT_STANDBY_MS          8000      // 8s cyan boot animation

// === Quiet Reward Behaviour ===
static unsigned long quietStartMs    = 0;  // streak start
static unsigned long rewardStartMs   = 0;
static bool          rewardShowing   = false;
const unsigned long  QUIET_REWARD_MS    = 120000; // 120s sustained "good"
const unsigned long  REWARD_DURATION_MS = 3000;   // 3s magic-wand swirl

// Persistent reward flag for logging (so 60s logs can’t miss the 120s reward)
static bool          rewardActiveForLog    = false;
static unsigned long rewardLogUntilMs      = 0;
const unsigned long  REWARD_LOG_WINDOW_MS  = 130000; // 130s window for logs

// === Anti-gaming reward state ===
static unsigned long rewardCooldownUntilMs = 0;
static unsigned long penaltyUntilMs        = 0;
static bool          needsResetAboveGood   = false;  // must go above good once after reward
static unsigned long lessonStartMs         = 0;      // used for MIN_LESSON_BEFORE_REWARD_MS

// === Device & schedule ===
enum DeviceMode { MODE_NORMAL, MODE_HOLLY_HOP };
DeviceMode deviceMode = MODE_NORMAL;

const unsigned long HOLLY_HOP_DURATION_MS = 30UL * 60UL * 1000UL; // 30 min
unsigned long hollyHopEndAt = 0;

// --- Holly Hop staging (pink at both ends) ---
enum HopStage { HOP_NONE, HOP_ENTERING, HOP_EXITING };
HopStage hopStage = HOP_NONE;
bool hopPendingExit = false;          
unsigned long hopStageUntil = 0;      
const uint16_t HOP_STAGE_MS = 250;    

// === Globals ===
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

float dbHistory[AVG_SAMPLE_COUNT];
int currentIndex = 0;
bool internetConnected = false;
bool inSleepMode = false;
unsigned long silenceStart = 0;
unsigned long lastSampleTime = 0;

// ✅ FIX: smoothed dB used ONLY for LED logic (not for logging)
static float ledDB = 35.0f;
static bool  hasNewSampleForLed = false;   // ✅ FIX: gate smoothing to sample tick

// Runtime timetable toggle (portal-driven)
bool useTimetable = true;   // true = follow timetable, false = always LESSON 24/7

// Button handling
static bool lastButtonState = HIGH;
static unsigned long buttonPressStart = 0;
static bool wipeTriggered = false;
static unsigned long lastToggleAt = 0;

// Wi-Fi resiliency
unsigned long lastWifiRetry = 0;
const unsigned long WIFI_RETRY_MS = 10000;

// === Logging Configuration ===
const char* GOOGLE_SHEETS_URL = "https://script.google.com/macros/s/AKfycbz0EkrgGAsmvoMrEOgei-IA40HUNH_UIf20gD3pCg5FTYFwh7sSXoD54rCR1XGb8_8b/exec";
unsigned long lastPostTime = 0;
unsigned long postInterval = 60000;

// === Timetable States ===
enum WindowState { OFF, LESSON, ASSEMBLY, BREAK, LUNCH };

// === Colour Zones & Hysteresis ===
enum ColorZone { Z_GREEN, Z_AMBER, Z_DEEP, Z_RED };
ColorZone currentZone = Z_GREEN;

// Track last window state to detect LESSON transitions
static WindowState lastWs = OFF;

// ===== Master Switch (compile-time) =====
#define TIMETABLE_ENABLED 1   // 1 = timetable ON, 0 = force LESSON for all times

// === Room Calibration (adaptive thresholds with guardrails) ===
static bool   roomCalEnabled    = false;  // portal toggle
static bool   roomCalComplete   = false;  // true after compute
static time_t roomCalStartEpoch = 0;      // epoch seconds (start of learning)

// Running stats (Welford) sampled during LESSON (~1/min)
static uint32_t calCount = 0;
static double   calMean  = 0.0;
static double   calM2    = 0.0;
static float    calMinDb = 9999.0f;
static float    calMaxDb = -9999.0f;

// Stored calibrated thresholds (persisted)
static Thresholds CAL_TH = {43, 48, 52};

// 5 days learning (as requested)
static const uint32_t ROOM_CAL_DAYS    = 5;
static const uint32_t ROOM_CAL_SECONDS = ROOM_CAL_DAYS * 24UL * 60UL * 60UL;

static inline bool hasValidTime(time_t t) { return t > 1700000000; } // ~Nov 2023

// === Prototypes ===
void setupI2SMic();
void logToGoogleSheets(float avgDB);
void logHollyHopEvent(const char* eventLabel);
void logQuietRewardEvent(float avgDB);
float readMicDB();
void updateRollingAverage(float newVal);
float calculateAverage();
void clearDBHistory();
void updateLEDs(float dbForLeds);
void fillStrip(uint32_t color);
void breathingAmber();
void breathingBlue();
void standbyCyanPulse();
WindowState getWindowState(int wday, int h, int m);
void connectToWiFi();
String getCurrentLEDState(float db);
void printDateTimeDebug(bool showMonitoring = false);
uint32_t wheel(byte pos);
void renderSleepingRainbow();
ColorZone classifyZone(float dB);
ColorZone applyStickyHysteresis(ColorZone current, float dB);
inline void showPink();
void showQuietReward(unsigned long nowMs);
bool isSendClassroom();
int parseTimeToMins(const char* hhmm);

// Room calibration helpers
struct Guardrails { float gMin, gMax; float aMin, aMax; float rMin, rMax; };
Guardrails guardrailsForBand(const char* band);
static inline float clampf(float v, float lo, float hi);
Thresholds normalizeThresholds(Thresholds th);
Thresholds applyGuardrails(Thresholds th, const char* band);
void resetRoomCalibration(time_t nowEpoch);
void updateRoomCalibration(float avgDB);
static inline bool calibrationWindowActive(time_t nowEpoch);
Thresholds computeCalibratedThresholdsFromStats(const char* band);
void finalizeRoomCalibrationIfDue(time_t nowEpoch);

// === Time Debug Helpers ===
const char* WEEKDAY[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

// === Timetable (configurable via Wi-Fi portal) ===
char t_day_start[6]     = "08:50";
char t_break1_start[6]  = "10:30";
char t_break1_end[6]    = "10:45";
char t_lunch_start[6]   = "12:00";
char t_lunch_end[6]     = "13:00";
char t_day_end[6]       = "15:20";

// ===== Year-band helpers =====
// All bands are forgiving relative to normal classroom behaviour

Thresholds thresholdsForBand(const char* band) {
  if (!band) return {46, 49, 51};   // default = Y2Y3

  String b = String(band);
  b.toUpperCase();

  if (b == "RY1")   return {50, 53, 56};
  if (b == "Y2Y3")  return {49, 52, 55};
  if (b == "Y4Y6")  return {46, 49, 51};
  if (b == "SEND")  return {52, 56, 60};

  return {46, 49, 51};
}

void computeYearBandFromInput(const char* input, char* outBand, size_t outLen) {
  String in = String(input ? input : "");
  in.toUpperCase();
  in.replace(" ", "");

  if (in.indexOf("SEND") >= 0) {
    strlcpy(outBand, "SEND", outLen);
    return;
  }

  in.replace("YR", "R");
  in.replace("RECEPTION", "R");
  in.replace("Y", "");
  if (in.length() == 0) { strlcpy(outBand, "Y2Y3", outLen); return; }

  int maxYear = -1; bool hasReception = false; int start = 0;
  while (true) {
    int comma = in.indexOf(',', start);
    String token = (comma == -1) ? in.substring(start) : in.substring(start, comma);
    token.trim();
    if (token == "R") {
      hasReception = true;
      if (maxYear < 1) maxYear = 1;
    } else if (token.length() > 0 && isDigit(token[0])) {
      int yr = token.toInt();
      if (yr > maxYear) maxYear = yr;
    }
    if (comma == -1) break;
    start = comma + 1;
  }

  if (hasReception || maxYear <= 1)      strlcpy(outBand, "RY1",  outLen);
  else if (maxYear <= 3)                 strlcpy(outBand, "Y2Y3", outLen);
  else                                   strlcpy(outBand, "Y4Y6", outLen);
}

bool isSendClassroom() {
  return (strcmp(yearBand, "SEND") == 0);
}

// === Room calibration guardrails ===
Guardrails guardrailsForBand(const char* band) {
  String b = String(band ? band : ""); b.toUpperCase();
  if (b == "SEND") return { 42, 55,   46, 60,   50, 66 };
  if (b == "RY1")  return { 40, 52,   44, 56,   48, 60 };
  if (b == "Y2Y3") return { 38, 52,   42, 56,   46, 60 };
  if (b == "Y4Y6") return { 40, 54,   45, 58,   49, 62 };
  return            { 39, 52,   44, 56,   48, 60 };
}

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

Thresholds normalizeThresholds(Thresholds th) {
  const float GAP_GA = 2.0f;
  const float GAP_AR = 2.0f;
  if (th.amberMax < th.greenMax + GAP_GA) th.amberMax = th.greenMax + GAP_GA;
  if (th.redWarnDb < th.amberMax + GAP_AR) th.redWarnDb = th.amberMax + GAP_AR;
  return th;
}

Thresholds applyGuardrails(Thresholds th, const char* band) {
  Guardrails gr = guardrailsForBand(band);
  th.greenMax  = clampf(th.greenMax,  gr.gMin, gr.gMax);
  th.amberMax  = clampf(th.amberMax,  gr.aMin, gr.aMax);
  th.redWarnDb = clampf(th.redWarnDb, gr.rMin, gr.rMax);
  return normalizeThresholds(th);
}

void resetRoomCalibration(time_t nowEpoch) {
  roomCalComplete = false;
  roomCalStartEpoch = hasValidTime(nowEpoch) ? nowEpoch : 0;

  calCount = 0;
  calMean  = 0.0;
  calM2    = 0.0;
  calMinDb = 9999.0f;
  calMaxDb = -9999.0f;

  CAL_TH = thresholdsForBand(yearBand);
  CAL_TH = applyGuardrails(CAL_TH, yearBand);

  preferences.putBool("rc_complete", roomCalComplete);
  preferences.putLong("rc_start", (long)roomCalStartEpoch);

  preferences.putULong("rc_count", calCount);
  preferences.putDouble("rc_mean", calMean);
  preferences.putDouble("rc_m2",   calM2);
  preferences.putFloat("rc_min",   calMinDb);
  preferences.putFloat("rc_max",   calMaxDb);

  preferences.putFloat("rc_g", CAL_TH.greenMax);
  preferences.putFloat("rc_a", CAL_TH.amberMax);
  preferences.putFloat("rc_r", CAL_TH.redWarnDb);

  Serial.println("🧪 Room calibration reset.");
}

void updateRoomCalibration(float avgDB) {
  if (!isfinite(avgDB)) return;

  calCount++;
  double x = (double)avgDB;
  double delta = x - calMean;
  calMean += delta / (double)calCount;
  double delta2 = x - calMean;
  calM2 += delta * delta2;

  if (avgDB < calMinDb) calMinDb = avgDB;
  if (avgDB > calMaxDb) calMaxDb = avgDB;
}

static inline bool calibrationWindowActive(time_t nowEpoch) {
  if (!roomCalEnabled) return false;
  if (roomCalComplete) return false;
  if (!hasValidTime(nowEpoch)) return false;
  if (!hasValidTime(roomCalStartEpoch)) return false;
  return (uint32_t)(nowEpoch - roomCalStartEpoch) < ROOM_CAL_SECONDS;
}

Thresholds computeCalibratedThresholdsFromStats(const char* band) {
  if (calCount < 60) {
    Thresholds th = thresholdsForBand(band);
    return applyGuardrails(th, band);
  }

  double variance = (calCount > 1) ? (calM2 / (double)(calCount - 1)) : 0.0;
  if (variance < 0.0) variance = 0.0;
  double sd = sqrt(variance);

  Thresholds th;
  th.greenMax  = (float)(calMean + 0.90 * sd);
  th.amberMax  = (float)(calMean + 1.60 * sd);
  th.redWarnDb = (float)(calMean + 2.40 * sd);

  if (sd < 1.0) {
    th.greenMax  = (float)(calMean + 1.0);
    th.amberMax  = (float)(calMean + 3.0);
    th.redWarnDb = (float)(calMean + 6.0);
  }

  return applyGuardrails(th, band);
}

void finalizeRoomCalibrationIfDue(time_t nowEpoch) {
  if (!roomCalEnabled) return;
  if (roomCalComplete) return;
  if (!hasValidTime(nowEpoch) || !hasValidTime(roomCalStartEpoch)) return;

  uint32_t elapsed = (uint32_t)(nowEpoch - roomCalStartEpoch);
  if (elapsed < ROOM_CAL_SECONDS) return;

  CAL_TH = computeCalibratedThresholdsFromStats(yearBand);

  roomCalComplete = true;
  preferences.putBool("rc_complete", true);

  preferences.putFloat("rc_g", CAL_TH.greenMax);
  preferences.putFloat("rc_a", CAL_TH.amberMax);
  preferences.putFloat("rc_r", CAL_TH.redWarnDb);

  preferences.putULong("rc_count", calCount);
  preferences.putDouble("rc_mean", calMean);
  preferences.putDouble("rc_m2",   calM2);
  preferences.putFloat("rc_min",   calMinDb);
  preferences.putFloat("rc_max",   calMaxDb);

  Serial.printf("✅ Room calibration COMPLETE. CAL_TH=(G≤%.1f, A≤%.1f, Rwarn=%.1f), n=%lu, mean=%.2f\n",
                CAL_TH.greenMax, CAL_TH.amberMax, CAL_TH.redWarnDb,
                (unsigned long)calCount,
                (float)calMean);

  ACTIVE_TH = CAL_TH;
}

// ===== Rainbow Helpers =====
uint32_t wheel(byte pos) {
  pos = 255 - pos;
  if (pos < 85) return strip.Color(255 - pos * 3, 0, pos * 3);
  else if (pos < 170) {
    pos -= 85;
    return strip.Color(0, pos * 3, 255 - pos * 3);
  } else {
    pos -= 170;
    return strip.Color(pos * 3, 255 - pos * 3, 0);
  }
}

void renderSleepingRainbow() {
  static unsigned long lastUpdate = 0;
  static uint16_t hueOffset = 0;
  const uint16_t speedMs = 20;
  if (millis() - lastUpdate < speedMs) return;
  lastUpdate = millis();
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t wheelPos = ((i * 256) / NUM_LEDS + hueOffset) & 0xFF;
    strip.setPixelColor(i, wheel(wheelPos));
  }
  strip.show();
  hueOffset++;
}

// ===== Holly Hop helpers (enter/exit with staging) =====
inline void enterHollyHop(unsigned long nowMs) {
  if (deviceMode == MODE_HOLLY_HOP) return;
  deviceMode    = MODE_HOLLY_HOP;
  hollyHopEndAt = nowMs + HOLLY_HOP_DURATION_MS;
  strip.setBrightness(150);
  hopStage       = HOP_ENTERING;
  hopPendingExit = false;
  hopStageUntil  = nowMs + HOP_STAGE_MS;
  showPink();
  logHollyHopEvent("START");
}

inline void beginExitHollyHop(unsigned long nowMs, const char* why) {
  if (deviceMode != MODE_HOLLY_HOP) return;
  hopStage       = HOP_EXITING;
  hopPendingExit = true;
  hopStageUntil  = nowMs + HOP_STAGE_MS;
  showPink();
  Serial.printf("Holly Hop staging exit (%s)\n", why);
}

inline void finishExitHollyHop() {
  deviceMode     = MODE_NORMAL;
  hopPendingExit = false;
  hopStage       = HOP_NONE;
  hollyHopEndAt  = 0;
  strip.setBrightness(100);
  logHollyHopEvent("END");
  Serial.println("Holly Hop Mode ended.");
}

// ===== Time Parsing Helper =====
int parseTimeToMins(const char* hhmm) {
  if (!hhmm) return -1;
  int len = strlen(hhmm);
  if (len < 4 || len > 5) return -1;
  int h = 0, m = 0;
  if (sscanf(hhmm, "%d:%d", &h, &m) != 2) return -1;
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

// ===== Timetable Windows (configurable + always-on toggle) =====
WindowState getWindowState(int wday, int h, int m) {
  if (!useTimetable) return LESSON;
  if (TIMETABLE_ENABLED == 0) return LESSON;
  if (wday < 1 || wday > 5) return OFF;

  int mins = h * 60 + m;

  int dayStart    = parseTimeToMins(t_day_start);
  int break1Start = parseTimeToMins(t_break1_start);
  int break1End   = parseTimeToMins(t_break1_end);
  int lunchStart  = parseTimeToMins(t_lunch_start);
  int lunchEnd    = parseTimeToMins(t_lunch_end);
  int dayEnd      = parseTimeToMins(t_day_end);

  bool configOk =
    (dayStart    >= 0) &&
    (break1Start >= 0) &&
    (break1End   >= 0) &&
    (lunchStart  >= 0) &&
    (lunchEnd    >= 0) &&
    (dayEnd      >= 0) &&
    (dayStart    < break1Start) &&
    (break1Start < break1End) &&
    (break1End   <= lunchStart) &&
    (lunchStart  < lunchEnd) &&
    (lunchEnd    < dayEnd);

  if (!configOk) {
    if (mins >= 530 && mins < 625) return LESSON;
    if (mins >= 625 && mins < 645) return ASSEMBLY;
    if (mins >= 645 && mins < 660) return BREAK;
    if (mins >= 660 && mins < 720) return LESSON;
    if (mins >= 720 && mins < 780) return LUNCH;
    if (mins >= 780 && mins < 920) return LESSON;
    return OFF;
  }

  if (mins < dayStart || mins >= dayEnd) return OFF;
  if (mins < break1Start)                return LESSON;
  if (mins < break1End)                  return BREAK;
  if (mins < lunchStart)                 return LESSON;
  if (mins < lunchEnd)                   return LUNCH;
  return LESSON;
}

// ===== Colour Zone classifier =====
ColorZone classifyZone(float dB) {
  if (dB <= ACTIVE_TH.greenMax) return Z_GREEN;
  if (dB <= ACTIVE_TH.amberMax) return Z_AMBER;
  if (dB <= ACTIVE_TH.redWarnDb) return Z_DEEP;
  return Z_RED;
}

// ✅ FIX: sticky hysteresis across zones
ColorZone applyStickyHysteresis(ColorZone current, float dB) {
  const float hysGA = isSendClassroom() ? 1.2f : HYS_GA_DEFAULT;
  const float hysAD = isSendClassroom() ? 1.5f : HYS_AD_DEFAULT;
  const float hysDR = HYS_DR_DEFAULT;

  switch (current) {
    case Z_GREEN:
      return (dB > ACTIVE_TH.greenMax + hysGA) ? Z_AMBER : Z_GREEN;

    case Z_AMBER:
      if (dB < ACTIVE_TH.greenMax - hysGA) return Z_GREEN;
      if (dB > ACTIVE_TH.amberMax + hysAD) return Z_DEEP;
      return Z_AMBER;

    case Z_DEEP:
      if (dB < ACTIVE_TH.amberMax - hysAD) return Z_AMBER;
      if (dB > ACTIVE_TH.redWarnDb + hysDR) return Z_RED;
      return Z_DEEP;

    case Z_RED:
    default:
      return (dB < ACTIVE_TH.redWarnDb - hysDR) ? Z_DEEP : Z_RED;
  }
}

// ===== Mic / I2S =====
void setupI2SMic() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {};
#if (ESP_IDF_VERSION_MAJOR >= 4)
  pin_config.mck_io_num   = I2S_PIN_NO_CHANGE;
#endif
  pin_config.bck_io_num   = MIC_SCK_PIN;
  pin_config.ws_io_num    = MIC_WS_PIN;
  pin_config.data_out_num = I2S_PIN_NO_CHANGE;
  pin_config.data_in_num  = MIC_SD_PIN;

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

float readMicDB() {
  int32_t samples[512];
  size_t bytesRead = 0;

  const TickType_t timeoutTicks = pdMS_TO_TICKS(10);
  i2s_read(I2S_NUM_0, (char*)samples, sizeof(samples), &bytesRead, timeoutTicks);

  int count = bytesRead / sizeof(int32_t);
  if (count <= 0) return calculateAverage();

  double sum = 0.0;
  for (int i = 0; i < count; i++) {
    float s = (float)samples[i] / 2147483648.0f;
    sum += (double)s * (double)s;
  }
  float rms = sqrt(sum / (double)count);
  if (rms < 1e-9f) rms = 1e-9f;

  float db = 20.0f * log10f(rms) + MIC_DB_OFFSET;

  if (db < 20.0f) db = 20.0f;
  if (db > 90.0f) db = 90.0f;

  if (!isfinite(db)) db = calculateAverage();
  return db;
}

// ===== Rolling Average =====
void updateRollingAverage(float newVal) {
  if (!isfinite(newVal)) return;
  dbHistory[currentIndex] = newVal;
  currentIndex = (currentIndex + 1) % AVG_SAMPLE_COUNT;
}

float calculateAverage() {
  float total = 0;
  for (int i = 0; i < AVG_SAMPLE_COUNT; i++) total += dbHistory[i];
  return total / AVG_SAMPLE_COUNT;
}

void clearDBHistory() {
  for (int i = 0; i < AVG_SAMPLE_COUNT; i++) dbHistory[i] = 35.0f;
}

// ===== LED Behaviours =====
void updateLEDs(float dbForLeds) {
  const float gMax  = ACTIVE_TH.greenMax;
  const float aMax  = ACTIVE_TH.amberMax;
  const float rWarn = ACTIVE_TH.redWarnDb;

  uint8_t r = 0, g = 0;

  if (currentZone == Z_RED) {
    r = 255;
    g = isSendClassroom() ? 80 : 0;
  }
  else if (dbForLeds <= gMax) {
    r = 0;
    g = 255;
  }
  else if (dbForLeds <= aMax) {
    float t = (dbForLeds - gMax) / (aMax - gMax);
    t = clampf(t, 0.0f, 1.0f);
    r = (uint8_t)(t * 180.0f);
    g = (uint8_t)(255.0f - t * 155.0f);
  }
  else if (dbForLeds <= rWarn) {
    float t = (dbForLeds - aMax) / (rWarn - aMax);
    t = clampf(t, 0.0f, 1.0f);
    r = (uint8_t)(180.0f + t * (255.0f - 180.0f));
    g = (uint8_t)(100.0f - t * 100.0f);
  } else {
    r = 255;
    g = isSendClassroom() ? 80 : 0;
  }

  fillStrip(strip.Color(r, g, 0));
}

void fillStrip(uint32_t color) { 
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
  strip.show(); 
}

void breathingBlue() {
  static int brightness = 0, delta = 2; 
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 20) {
    brightness += delta;
    if (brightness >= 255) { brightness = 255; delta = -abs(delta); }
    if (brightness <= 0)   { brightness = 0;   delta =  abs(delta); }
    uint8_t b = (uint8_t)brightness;
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(0, 0, b));
    strip.show();
    lastUpdate = millis();
  }
}

void standbyCyanPulse() {
  static int brightness = 0, delta = 3;
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 12) {
    brightness += delta;
    if (brightness >= 255) { brightness = 255; delta = -abs(delta); }
    if (brightness <= 0)   { brightness = 0;   delta =  abs(delta); }
    uint8_t g = (uint8_t)brightness;
    uint8_t b = (uint8_t)brightness;
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(0, g, b));
    strip.show();
    lastUpdate = millis();
  }
}

// Pink staging helper
inline void showPink() { fillStrip(strip.Color(255, 20, 147)); }

// Quiet reward helper: white "magic wand" swirl around the ring
void showQuietReward(unsigned long nowMs) {
  static int head = 0;
  static unsigned long lastFrame = 0;
  const uint16_t FRAME_MS = 20;
  const int TAIL_LEN = 20;

  if (nowMs - lastFrame < FRAME_MS) return;
  lastFrame = nowMs;

  head = (head + 1) % NUM_LEDS;

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t baseG = 40;
    uint8_t r = 0, g = baseG, b = 0;

    int dist = i - head;
    if (dist < 0) dist += NUM_LEDS;

    if (dist == 0) {
      r = 255; g = 255; b = 255;
    } else if (dist > 0 && dist < TAIL_LEN) {
      float t = 1.0f - (float)dist / (float)TAIL_LEN;
      uint8_t intensity = (uint8_t)(t * 200.0f);
      r = intensity; g = intensity; b = intensity;
    }

    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

String getCurrentLEDState(float db) {
  if (deviceMode == MODE_HOLLY_HOP) {
    if (hopStage == HOP_ENTERING || hopStage == HOP_EXITING) return "Holly Hop (staging)";
    return "Holly Hop (override)";
  }
  if (rewardShowing) return "Reward (Quiet)";
  if (inSleepMode)   return "Sleeping (Rainbow)";
  if (currentZone == Z_RED)  return "Red";
  if (db > ACTIVE_TH.amberMax) return "Deep Amber";
  if (db > ACTIVE_TH.greenMax) return "Amber";
  return "Green";
}

// ===== Debug Printer =====
void printDateTimeDebug(bool showMonitoring) {
  struct tm ti;
  if (!getLocalTime(&ti)) {
    Serial.println("⛔ getLocalTime() failed");
    return;
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
  Serial.printf("🕒 %s | wday=%d (%s)\n", buf, ti.tm_wday, WEEKDAY[ti.tm_wday]);
  if (showMonitoring) {
    int mins = ti.tm_hour * 60 + ti.tm_min;
    WindowState ws = getWindowState(ti.tm_wday, ti.tm_hour, ti.tm_min);
    const char* s = (ws==LESSON?"LESSON":ws==ASSEMBLY?"ASSEMBLY":ws==BREAK?"BREAK":ws==LUNCH?"LUNCH":"OFF");
    Serial.printf("📊 state=%s | minsSinceMidnight=%d\n", s, mins);
  }
}

// ===== Google Sheets Logging =====
void logToGoogleSheets(float avgDB) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(GOOGLE_SHEETS_URL);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(768);
  doc["device"]        = deviceName;
  doc["school"]        = schoolName;
  doc["year_groups"]   = yearGroupInput;
  doc["year_band"]     = yearBand;
  doc["th_green_max"]  = ACTIVE_TH.greenMax;
  doc["th_amber_max"]  = ACTIVE_TH.amberMax;
  doc["th_red_warn"]   = ACTIVE_TH.redWarnDb;
  doc["sound_dB"]      = avgDB;
  doc["ip_address"]    = WiFi.localIP().toString();
  doc["mac_address"]   = WiFi.macAddress();
  doc["temperature_C"] = temperature;
  doc["humidity_percent"] = humidity;
  doc["pressure_hPa"]  = pressure;
  doc["led_state"]     = getCurrentLEDState(ledDB);   // ✅ uses ledDB
  doc["holly_hop"]     = (deviceMode == MODE_HOLLY_HOP);
  doc["quiet_reward"]  = rewardActiveForLog ? "Yes" : "";

  doc["room_cal_enabled"]  = roomCalEnabled ? "Yes" : "";
  doc["room_cal_complete"] = roomCalComplete ? "Yes" : "";
  doc["room_cal_days"]     = (int)ROOM_CAL_DAYS;

  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char timestamp[30];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
  doc["timestamp"] = timestamp;

  String payload;
  serializeJson(doc, payload);
  int responseCode = http.POST(payload);
  Serial.printf("📡 Google POST (periodic): %d\n", responseCode);
  http.end();
}

void logHollyHopEvent(const char* eventLabel) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(GOOGLE_SHEETS_URL);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(768);
  doc["device"]        = deviceName;
  doc["school"]        = schoolName;
  doc["event_type"]    = "HOLLY_HOP";
  doc["event"]         = eventLabel;
  doc["year_band"]     = yearBand;
  doc["ip_address"]    = WiFi.localIP().toString();
  doc["mac_address"]   = WiFi.macAddress();
  doc["temperature_C"] = temperature;
  doc["humidity_percent"] = humidity;
  doc["pressure_hPa"]  = pressure;
  doc["led_state"]     = "Holly Hop";

  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char timestamp[30];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
  doc["timestamp"] = timestamp;

  String payload;
  serializeJson(doc, payload);
  int responseCode = http.POST(payload);
  Serial.printf("📡 Google POST (Holly Hop %s): %d\n", eventLabel, responseCode);
  http.end();
}

void logQuietRewardEvent(float avgDB) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(GOOGLE_SHEETS_URL);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(768);
  doc["device"]        = deviceName;
  doc["school"]        = schoolName;
  doc["event_type"]    = "QUIET_REWARD";
  doc["event"]         = "TRIGGER";
  doc["year_band"]     = yearBand;
  doc["sound_dB"]      = avgDB;
  doc["ip_address"]    = WiFi.localIP().toString();
  doc["mac_address"]   = WiFi.macAddress();
  doc["temperature_C"] = temperature;
  doc["humidity_percent"] = humidity;
  doc["pressure_hPa"]  = pressure;
  doc["led_state"]     = "Reward (Quiet)";
  doc["quiet_reward"]  = "Yes";

  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char timestamp[30];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
  doc["timestamp"] = timestamp;

  String payload;
  serializeJson(doc, payload);
  int responseCode = http.POST(payload);
  Serial.printf("📡 Google POST (Quiet Reward): %d\n", responseCode);
  http.end();
}

// ===== Wi-Fi Connect (with Year Group & Timetable capture & resiliency) =====
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.setBreakAfterConfig(true);
  wm.setConnectTimeout(45);

  String storedName   = preferences.getString("deviceName", deviceName);
  String storedSchool = preferences.getString("schoolName", schoolName);
  String storedYG     = preferences.getString("yearGroupInput", yearGroupInput);

  String storedDayStart    = preferences.getString("t_day_start",    t_day_start);
  String storedBreak1Start = preferences.getString("t_break1_start", t_break1_start);
  String storedBreak1End   = preferences.getString("t_break1_end",   t_break1_end);
  String storedLunchStart  = preferences.getString("t_lunch_start",  t_lunch_start);
  String storedLunchEnd    = preferences.getString("t_lunch_end",    t_lunch_end);
  String storedDayEnd      = preferences.getString("t_day_end",      t_day_end);

  String storedUseTimetableStr = preferences.getBool("use_timetable", useTimetable) ? "1" : "0";
  String storedRoomCalStr      = preferences.getBool("rc_enabled", roomCalEnabled) ? "1" : "0";

  storedName.toCharArray(deviceName, sizeof(deviceName));
  storedSchool.toCharArray(schoolName, sizeof(schoolName));
  storedYG.toCharArray(yearGroupInput, sizeof(yearGroupInput));

  storedDayStart.toCharArray(t_day_start, sizeof(t_day_start));
  storedBreak1Start.toCharArray(t_break1_start, sizeof(t_break1_start));
  storedBreak1End.toCharArray(t_break1_end, sizeof(t_break1_end));
  storedLunchStart.toCharArray(t_lunch_start, sizeof(t_lunch_start));
  storedLunchEnd.toCharArray(t_lunch_end, sizeof(t_lunch_end));
  storedDayEnd.toCharArray(t_day_end, sizeof(t_day_end));

  char useTimetableStr[3]; storedUseTimetableStr.toCharArray(useTimetableStr, sizeof(useTimetableStr));
  char roomCalStr[3];      storedRoomCalStr.toCharArray(roomCalStr, sizeof(roomCalStr));

  WiFiManagerParameter custom_device_name("device", "Device ID", deviceName, 32);
  WiFiManagerParameter custom_school_name("school", "School Name", schoolName, 32);
  WiFiManagerParameter custom_year_group(
    "year_groups",
    "Year group(s). Use R or numbers, commas for multiple (e.g. 2,3). Add SEND for SEND profile.",
    yearGroupInput, 32
  );

  WiFiManagerParameter param_use_timetable("use_timetable", "Use timetable? (1 = timetable, 0 = always on)", useTimetableStr, 3);
  WiFiManagerParameter param_room_cal("room_cal", "Room calibration? (1 = enable 5-day learning, 0 = disable)", roomCalStr, 3);

  WiFiManagerParameter param_day_start("t_day_start", "Start of day (HH:MM, e.g. 08:50)", t_day_start, 6);
  WiFiManagerParameter param_break1_start("t_break1_start", "First break start (HH:MM)", t_break1_start, 6);
  WiFiManagerParameter param_break1_end("t_break1_end", "First break end (HH:MM)", t_break1_end, 6);
  WiFiManagerParameter param_lunch_start("t_lunch_start", "Lunch start (HH:MM)", t_lunch_start, 6);
  WiFiManagerParameter param_lunch_end("t_lunch_end", "Lunch end (HH:MM)", t_lunch_end, 6);
  WiFiManagerParameter param_day_end("t_day_end", "End of day (HH:MM)", t_day_end, 6);

  wm.addParameter(&custom_device_name);
  wm.addParameter(&custom_school_name);
  wm.addParameter(&custom_year_group);
  wm.addParameter(&param_use_timetable);
  wm.addParameter(&param_room_cal);
  wm.addParameter(&param_day_start);
  wm.addParameter(&param_break1_start);
  wm.addParameter(&param_break1_end);
  wm.addParameter(&param_lunch_start);
  wm.addParameter(&param_lunch_end);
  wm.addParameter(&param_day_end);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("🟦 Boot: CONNECTING — standby cyan for 8s (Wi-Fi setup/autoConnect)...");
    unsigned long glowStart = millis();
    while (millis() - glowStart < BOOT_STANDBY_MS) {
      standbyCyanPulse();
      delay(1);
    }
  }

  if (wm.autoConnect("Holly_SetMeUp")) {
    internetConnected = true;
    digitalWrite(STATUS_LED, HIGH);
  } else {
    internetConnected = (WiFi.status() == WL_CONNECTED);
    digitalWrite(STATUS_LED, internetConnected ? HIGH : LOW);
  }

  strncpy(deviceName, custom_device_name.getValue(), sizeof(deviceName) - 1);
  strncpy(schoolName, custom_school_name.getValue(), sizeof(schoolName) - 1);
  strncpy(yearGroupInput, custom_year_group.getValue(), sizeof(yearGroupInput) - 1);

  strncpy(t_day_start,    param_day_start.getValue(),    sizeof(t_day_start)    - 1);
  strncpy(t_break1_start, param_break1_start.getValue(), sizeof(t_break1_start) - 1);
  strncpy(t_break1_end,   param_break1_end.getValue(),   sizeof(t_break1_end)   - 1);
  strncpy(t_lunch_start,  param_lunch_start.getValue(),  sizeof(t_lunch_start)  - 1);
  strncpy(t_lunch_end,    param_lunch_end.getValue(),    sizeof(t_lunch_end)    - 1);
  strncpy(t_day_end,      param_day_end.getValue(),      sizeof(t_day_end)      - 1);

  const char* useVal = param_use_timetable.getValue();
  useTimetable = (useVal && useVal[0] == '1');

  const char* rcVal = param_room_cal.getValue();
  bool newRoomCalEnabled = (rcVal && rcVal[0] == '1');

  computeYearBandFromInput(yearGroupInput, yearBand, sizeof(yearBand));

  time_t nowEpoch = time(NULL);

  if (newRoomCalEnabled && !roomCalEnabled) {
    roomCalEnabled = true;
    preferences.putBool("rc_enabled", true);
    resetRoomCalibration(nowEpoch);
  }
  else if (!newRoomCalEnabled && roomCalEnabled) {
    roomCalEnabled = false;
    preferences.putBool("rc_enabled", false);
    ACTIVE_TH = applyGuardrails(thresholdsForBand(yearBand), yearBand);
    Serial.println("🧪 Room calibration DISABLED — reverted to band thresholds.");
  }
  else {
    roomCalEnabled = newRoomCalEnabled;
    preferences.putBool("rc_enabled", roomCalEnabled);
  }

  Thresholds bandTh = thresholdsForBand(yearBand);
  bandTh = applyGuardrails(bandTh, yearBand);

  // load stored CAL_TH
  Thresholds bandDef = thresholdsForBand(yearBand);
  CAL_TH.greenMax  = preferences.getFloat("rc_g", bandDef.greenMax);
  CAL_TH.amberMax  = preferences.getFloat("rc_a", bandDef.amberMax);
  CAL_TH.redWarnDb = preferences.getFloat("rc_r", bandDef.redWarnDb);
  CAL_TH = applyGuardrails(CAL_TH, yearBand);

  roomCalComplete    = preferences.getBool("rc_complete", false);
  roomCalStartEpoch  = (time_t)preferences.getLong("rc_start", 0);

  if (roomCalEnabled && roomCalComplete) ACTIVE_TH = CAL_TH;
  else ACTIVE_TH = bandTh;

  preferences.putString("deviceName", deviceName);
  preferences.putString("schoolName", schoolName);
  preferences.putString("yearGroupInput", yearGroupInput);
  preferences.putString("yearBand", yearBand);

  preferences.putString("t_day_start",    t_day_start);
  preferences.putString("t_break1_start", t_break1_start);
  preferences.putString("t_break1_end",   t_break1_end);
  preferences.putString("t_lunch_start",  t_lunch_start);
  preferences.putString("t_lunch_end",    t_lunch_end);
  preferences.putString("t_day_end",      t_day_end);

  preferences.putBool("use_timetable", useTimetable);

  Serial.printf("🧮 band=%s → th=(G≤%.1f, A≤%.1f, Rwarn=%.1f)\n",
                yearBand, ACTIVE_TH.greenMax, ACTIVE_TH.amberMax, ACTIVE_TH.redWarnDb);
}

// ===== Arduino Setup =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n==============================================");
  Serial.println("  Holly_RDiQ5000 (FIXED LED SMOOTH + HYSTERESIS + ANTI-GAMING)");
  Serial.println("==============================================");

  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info){
    if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.printf("✅ Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
    } else if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("⚠️  Wi-Fi disconnected (reason %d). Reconnecting...\n", info.wifi_sta_disconnected.reason);
      WiFi.begin();
    }
  });

  preferences.begin("classroom", false);

  // Load stored settings
  String storedName   = preferences.getString("deviceName", deviceName);
  String storedSchool = preferences.getString("schoolName", schoolName);
  String storedYG     = preferences.getString("yearGroupInput", "Y2");
  String storedBand   = preferences.getString("yearBand", "Y2Y3");
  bool storedUseTimetable = preferences.getBool("use_timetable", true);
  useTimetable = storedUseTimetable;

  roomCalEnabled     = preferences.getBool("rc_enabled", false);
  roomCalComplete    = preferences.getBool("rc_complete", false);
  roomCalStartEpoch  = (time_t)preferences.getLong("rc_start", 0);

  calCount = preferences.getULong("rc_count", 0);
  calMean  = preferences.getDouble("rc_mean", 0.0);
  calM2    = preferences.getDouble("rc_m2",   0.0);
  calMinDb = preferences.getFloat("rc_min",  9999.0f);
  calMaxDb = preferences.getFloat("rc_max",  -9999.0f);

  if (storedName.length() > 0)   storedName.toCharArray(deviceName, sizeof(deviceName));
  if (storedSchool.length() > 0) storedSchool.toCharArray(schoolName, sizeof(schoolName));
  storedYG.toCharArray(yearGroupInput, sizeof(yearGroupInput));
  storedBand.toCharArray(yearBand, sizeof(yearBand));

  // Active thresholds selection
  ACTIVE_TH = applyGuardrails(thresholdsForBand(yearBand), yearBand);

  // Load stored CAL_TH
  Thresholds bandDef = thresholdsForBand(yearBand);
  CAL_TH.greenMax  = preferences.getFloat("rc_g", bandDef.greenMax);
  CAL_TH.amberMax  = preferences.getFloat("rc_a", bandDef.amberMax);
  CAL_TH.redWarnDb = preferences.getFloat("rc_r", bandDef.redWarnDb);
  CAL_TH = applyGuardrails(CAL_TH, yearBand);

  if (roomCalEnabled && roomCalComplete) ACTIVE_TH = CAL_TH;

  Serial.printf("🧮 Year input=\"%s\" → band=%s → th=(G≤%.1f, A≤%.1f, Rwarn=%.1f)\n",
              yearGroupInput, yearBand, ACTIVE_TH.greenMax, ACTIVE_TH.amberMax, ACTIVE_TH.redWarnDb);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  pinMode(HOLLY_MODE_BTN, INPUT_PULLUP);

  strip.begin();
  strip.setBrightness(100);
  strip.show();

  connectToWiFi();

  configTzTime("GMT0BST,M3.5.0/1,M10.5.0/2", "pool.ntp.org", "time.nist.gov");
  Serial.println("⏳ Waiting for NTP time sync...");

  setupI2SMic();
  if (!bme.begin(0x76)) Serial.println("❌ Could not find BME280 sensor!");
  else Serial.println("✅ BME280 sensor initialized.");

  clearDBHistory();

  // seed LED smoothing baseline once we have a baseline average
  ledDB = calculateAverage();

  // initialise lesson start so MIN_LESSON guard doesn't block forever on boot
  lessonStartMs = millis();
}

// ===== Main Loop =====
void loop() {
  bool currentButtonState = digitalRead(HOLLY_MODE_BTN);
  unsigned long nowMs = millis();

  // --- Button handling (Holly Hop + long-hold reset) ---
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    buttonPressStart = 0;
    wipeTriggered = false;
  }

  if (currentButtonState == LOW && lastButtonState == HIGH) {
    if (nowMs - lastToggleAt >= DEBOUNCE_MS) {
      buttonPressStart = nowMs;
      wipeTriggered    = false;
      lastToggleAt     = nowMs;

      if (deviceMode == MODE_HOLLY_HOP) beginExitHollyHop(nowMs, "manual");
      else enterHollyHop(nowMs);
    }
  }

  if (currentButtonState == LOW && !wipeTriggered && buttonPressStart != 0) {
    if (nowMs - buttonPressStart >= 20000) {
      wipeTriggered = true;
      WiFiManager wm;
      wm.resetSettings();
      ESP.restart();
    }
  }

  lastButtonState = currentButtonState;

  // Mic sample (200ms)
  if (nowMs - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    float db = readMicDB();
    updateRollingAverage(db);
    lastSampleTime = nowMs;
    hasNewSampleForLed = true;            // ✅ FIX
  }

  // Timetable
  time_t tnow = time(NULL);
  struct tm* timeinfo = localtime(&tnow);
  int hour  = timeinfo ? timeinfo->tm_hour  : 0;
  int minute= timeinfo ? timeinfo->tm_min   : 0;
  int wday  = timeinfo ? timeinfo->tm_wday  : 0;
  WindowState ws = getWindowState(wday, hour, minute);

  // Detect LESSON transitions (used for anti-gaming)
  if (ws == LESSON && lastWs != LESSON) {
    lessonStartMs = nowMs;
    quietStartMs  = 0;                  // reset streak on lesson start
    // NOTE: cooldown/penalty intentionally NOT reset here (prevents break farming)
  }
  lastWs = ws;

  // ===== Normal path =====
  float avgDB = calculateAverage();

  // ✅ FIX: LED smoothing only when a new sample arrived
  if (hasNewSampleForLed) {
    ledDB = (1.0f - LED_SMOOTH_ALPHA) * ledDB + (LED_SMOOTH_ALPHA) * avgDB;
    hasNewSampleForLed = false;
  }

  // ✅ FIX: sticky hysteresis for zones using ledDB
  currentZone = applyStickyHysteresis(currentZone, ledDB);

  // Wi-Fi watchdog
  if (WiFi.status() != WL_CONNECTED) {
    if (nowMs - lastWifiRetry >= WIFI_RETRY_MS) {
      Serial.println("📶 Wi-Fi down — retrying begin()...");
      WiFi.disconnect(false);
      WiFi.begin();
      lastWifiRetry = nowMs;
    }
  }

  // Debug every 5s
  static unsigned long lastTimePrint = 0;
  if (nowMs - lastTimePrint >= 5000) {
    printDateTimeDebug(true);
    Serial.printf("🧪 State=%d | LED=%s | sleep=%s | wifi=%s | avgDB=%.1f | ledDB=%.1f | band=%s | th=(G≤%.1f,A≤%.1f,RW=%.1f) | useTimetable=%s | roomCal=%s/%s | cooldown=%s | penalty=%s\n",
                  ws, getCurrentLEDState(ledDB).c_str(),
                  inSleepMode ? "YES" : "NO",
                  (WiFi.status()==WL_CONNECTED) ? "ON" : "OFF",
                  avgDB,
                  ledDB,
                  yearBand, ACTIVE_TH.greenMax, ACTIVE_TH.amberMax, ACTIVE_TH.redWarnDb,
                  useTimetable ? "YES" : "ALWAYS ON",
                  roomCalEnabled ? "ON" : "OFF",
                  roomCalComplete ? "DONE" : "LEARN",
                  (nowMs < rewardCooldownUntilMs) ? "YES" : "NO",
                  (nowMs < penaltyUntilMs) ? "YES" : "NO");
    lastTimePrint = nowMs;
  }

  // STATUS_LED mirrors Wi-Fi
  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (wifiNow != internetConnected) {
    internetConnected = wifiNow;
    digitalWrite(STATUS_LED, wifiNow ? HIGH : LOW);
  }

  // Timetable behaviour
  if (ws == LESSON) {
    // Periodic logging (lesson only) - log avgDB (raw rolling average)
    if (internetConnected && !inSleepMode && (nowMs - lastPostTime >= postInterval)) {
      temperature = bme.readTemperature();
      humidity    = bme.readHumidity();
      pressure    = bme.readPressure() / 100.0F;
      logToGoogleSheets(avgDB);
      lastPostTime = nowMs;
    }

    // Sleep detection (use avgDB)
    if (avgDB < SLEEP_DB_THRESHOLD) {
      if (silenceStart == 0) silenceStart = nowMs;
      if (!inSleepMode && (nowMs - silenceStart >= SILENCE_DURATION_MS)) inSleepMode = true;
    } else {
      silenceStart = 0;
    }
    if (avgDB >= WAKE_DB_THRESHOLD && inSleepMode) inSleepMode = false;

    if (inSleepMode) {
      renderSleepingRainbow();
      return;
    }

    // === Room Calibration (LESSON only, ~1/min) ===
    time_t nowEpoch = time(NULL);

    // Auto-start calibration window once time is valid
    if (roomCalEnabled && !roomCalComplete && !hasValidTime(roomCalStartEpoch) && hasValidTime(nowEpoch)) {
      roomCalStartEpoch = nowEpoch;
      preferences.putLong("rc_start", (long)roomCalStartEpoch);
      Serial.printf("🧪 Room calibration STARTED (auto) at epoch=%ld (learning %lu days)\n",
                    (long)roomCalStartEpoch, (unsigned long)ROOM_CAL_DAYS);
    }

    if (calibrationWindowActive(nowEpoch)) {
      if (!rewardShowing && deviceMode == MODE_NORMAL) {
        static unsigned long lastCalSampleMs = 0;
        if (millis() - lastCalSampleMs >= postInterval) { // 60s
          updateRoomCalibration(avgDB);
          lastCalSampleMs = millis();

          preferences.putULong("rc_count", calCount);
          preferences.putDouble("rc_mean", calMean);
          preferences.putDouble("rc_m2",   calM2);
          preferences.putFloat("rc_min",   calMinDb);
          preferences.putFloat("rc_max",   calMaxDb);
        }
      }
    }

    finalizeRoomCalibrationIfDue(nowEpoch);

    if (roomCalEnabled && roomCalComplete) {
      CAL_TH = applyGuardrails(CAL_TH, yearBand);
      ACTIVE_TH = CAL_TH;
    }

    // =========================================================
    // === Quiet Reward Logic (ANTI-GAMING + COOLDOWN + PENALTY) ===
    // =========================================================
    // Reward logic only in normal mode (no Holly Hop) and when not already showing a reward
    if (!rewardShowing && deviceMode == MODE_NORMAL) {

      const float goodDb  = ACTIVE_TH.greenMax;
      const float breakDb = goodDb + REWARD_BREAK_MARGIN_DB;
      const float spikeDb = goodDb + REWARD_SPIKE_MARGIN_DB;

      // Optional: minimum lesson time before rewards are possible
      if (nowMs - lessonStartMs < MIN_LESSON_BEFORE_REWARD_MS) {
        quietStartMs = 0;
      } else {

        // Spike penalty: loud shout locks rewards briefly
        if (avgDB >= spikeDb) {
          penaltyUntilMs = nowMs + SPIKE_PENALTY_MS;
          quietStartMs = 0;
          needsResetAboveGood = true;
        } else {

          // If they've gone above good at any point, we can allow streak rebuilding
          if (avgDB > goodDb) needsResetAboveGood = false;

          // Cooldown/penalty lockouts
          if (nowMs < rewardCooldownUntilMs || nowMs < penaltyUntilMs) {
            quietStartMs = 0;
          } else {

            // Must go above good once after a reward (prevents immediate repeat farming)
            if (needsResetAboveGood) {
              if (avgDB > goodDb) {
                needsResetAboveGood = false;
              } else {
                quietStartMs = 0;
              }
            }

            // If allowed, build sustained "good" streak with hysteresis
            if (!needsResetAboveGood) {
              if (avgDB <= goodDb) {
                if (quietStartMs == 0) quietStartMs = nowMs;

                if (nowMs - quietStartMs >= QUIET_REWARD_MS) {
                  // Grant reward
                  rewardCooldownUntilMs = nowMs + REWARD_COOLDOWN_MS;
                  needsResetAboveGood   = true;

                  rewardActiveForLog = true;
                  rewardLogUntilMs   = nowMs + REWARD_LOG_WINDOW_MS;

                  logQuietRewardEvent(avgDB);

                  rewardShowing = true;
                  rewardStartMs = nowMs;
                  quietStartMs  = 0;
                  return;
                }
              }
              else if (avgDB >= breakDb) {
                // Break streak only if clearly above the break threshold
                quietStartMs = 0;
              }
              // else: between goodDb and breakDb, we do NOT reset streak (anti-hover)
            }
          }
        }
      }
    }

    // Reward animation display
    if (rewardShowing) {
      unsigned long t = millis();
      if (t - rewardStartMs < REWARD_DURATION_MS) {
        showQuietReward(t);
        return;
      } else {
        rewardShowing = false;
        quietStartMs  = 0;
      }
    }

    if (rewardActiveForLog && (long)(nowMs - rewardLogUntilMs) >= 0) {
      rewardActiveForLog = false;
    }

    // ✅ LED output uses ledDB (smoothed + hysteresis)
    updateLEDs(ledDB);
    return;
  }
  else if (ws == ASSEMBLY || ws == BREAK || ws == LUNCH) {
    breathingBlue();
    return;
  }

  // OFF
  strip.clear();
  strip.show();
}
