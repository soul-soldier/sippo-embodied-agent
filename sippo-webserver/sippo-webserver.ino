#include <WiFiNINA.h>
#include <Adafruit_NeoPixel.h>
#include "HX711.h"
#include "sippo-secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

int status = WL_IDLE_STATUS;
WiFiServer server(80);

const int LED_RING_PIN = 3;
const int LED_RING_PIXEL_COUNT = 16;
const int LED_RING_BRIGHTNESS = 50;

Adafruit_NeoPixel ledRing(
  LED_RING_PIXEL_COUNT,
  LED_RING_PIN,
  NEO_GRB + NEO_KHZ800);

// ------------------------------------------------------------
// Scale / HX711 configuration
// ------------------------------------------------------------

// Wiring:
// HX711 DT  -> Arduino pin 4
// HX711 SCK -> Arduino pin 5
// HX711 VCC -> 5V or 3.3V, depending on your module/board
// HX711 GND -> GND
const int HX711_DT_PIN = 4;
const int HX711_SCK_PIN = 5;

// Calibrated with the 341 g bottle:
// raw-ish value around 143548 / 341 g = ~421
const float SCALE_CALIBRATION_FACTOR = 421.0;

// Read the scale periodically, not on every loop iteration.
// This keeps the webserver responsive.
const unsigned long SCALE_READ_INTERVAL_MS = 300UL;
const unsigned long SCALE_STARTUP_WAIT_MS = 5000UL;
const int SCALE_READINGS_PER_SAMPLE = 3;

// Exponential smoothing for display/debug output.
// 0.0 = no movement, 1.0 = no smoothing.
const float SCALE_SMOOTHING_ALPHA = 0.25;

// ------------------------------------------------------------
// Real bottle / sip detection configuration
// ------------------------------------------------------------

// Your measured empty bottle weight.
// This is the bottle without water, but including the physical bottle itself.
const float EMPTY_BOTTLE_WEIGHT_GRAMS = 341.0;

// TODO: Replace this with the measured water capacity of your bottle.
// Fill the bottle to the level you consider "full", place it on the scale,
// then calculate: fullWeight - EMPTY_BOTTLE_WEIGHT_GRAMS.
// Because 1 g water is approximately 1 ml, this is also the capacity in ml.
const int BOTTLE_CAPACITY_ML = 750;

// Below this gross weight we assume the bottle is not currently on the scale.
// This prevents a bottle lift from being counted as a huge sip.
const float BOTTLE_PRESENT_MIN_WEIGHT_GRAMS = 180.0;

// The bottle must be back on the platform and stable for this long before
// we allow low/empty warnings. This prevents a lift from looking like empty.
const unsigned long BOTTLE_PRESENT_STABLE_FOR_EMPTY_MS = 2500UL;

// Sip detection: after the bottle is lifted and placed back, a lower weight
// means water was consumed. Values are grams ~= ml.
const float SIP_DETECTION_MIN_DROP_GRAMS = 50.0;
const float SIP_DETECTION_MAX_DROP_GRAMS = 750.0;

// Refill detection: after the bottle is lifted and placed back, a higher weight
// by this amount means refill.
const float REFILL_DETECTION_MIN_RISE_GRAMS = 250.0;

// Trigger the empty/low warning when estimated remaining water is below this.
const float EMPTY_BOTTLE_WARNING_WATER_ML = 60.0;

// Wait after the bottle returns before committing a sip/refill.
// This avoids counting mechanical wobble while the platform settles.
// The UI can still get an early, non-committing sip candidate before this ends.
const unsigned long BOTTLE_RETURN_SETTLE_MS = 1500UL;

// After the bottle returns, wait only a short moment, then do one quick
// non-committing check. If the weight is clearly lower, the frontend may show
// the happy reaction immediately, while the Arduino still waits until
// BOTTLE_RETURN_SETTLE_MS before updating totalDrankMl.
const unsigned long EARLY_SIP_FEEDBACK_AFTER_RETURN_MS = 350UL;
const float EARLY_SIP_FEEDBACK_MIN_DROP_GRAMS = 35.0;

// Avoid duplicate events from the same physical action.
const unsigned long SCALE_EVENT_COOLDOWN_MS = 3000UL;

HX711 scale;

bool scaleReady = false;
bool scaleTared = false;

float lastWeightGrams = 0.0;
float smoothedWeightGrams = 0.0;
bool smoothedWeightInitialized = false;

unsigned long lastScaleReadAt = 0;

// Scale-derived bottle state.
bool scaleAutoEventsEnabled = true;
bool bottlePresent = false;
bool previousBottlePresent = false;
bool pendingBottleReturnEvaluation = false;
bool pendingBottleReturnEarlyFeedbackChecked = false;
bool lastKnownBottleWeightInitialized = false;

float estimatedWaterMl = 0.0;
int estimatedBottleFillPercent = 0;
float lastKnownBottleWeightGrams = 0.0;
float weightBeforeBottleLiftGrams = 0.0;
float scaleDeltaSinceLastBottleWeightGrams = 0.0;

unsigned long bottleReturnedAt = 0;
unsigned long bottlePresentSinceAt = 0;
unsigned long bottleAbsentSinceAt = 0;
unsigned long lastScaleEventAt = 0;

unsigned long scaleEventCounter = 0;
bool wizardEmptyOverride = false;

const char* lastScaleEventName = "none";

// Forward declarations for scale helper overloads.
void setupScale();
void updateScaleReading();
void updateScaleReading(bool forceRead);
void tareScale();
void updateBottleEstimateFromScale();
void resetScaleEventTracking();
void acceptCurrentBottleWeightAsBaseline();
bool canShowEmptyWarningNow(unsigned long now);
void setScaleEvent(const char* eventName);
void applySipDetected(int amountMl);
void applyRefillDetected();
void applyEmptyDetected();

// ------------------------------------------------------------
// Sippo configuration
// ------------------------------------------------------------

const int DAILY_GOAL_ML = 2000;
const int SIP_AMOUNT_ML = 500;

// Demo timings.
// For the Wizard-of-Oz prototype these are intentionally short.
// Later you can replace them with realistic values, e.g. 30/60/90 minutes.
const unsigned long REMINDER_1_AFTER_MS = 30UL * 1000UL;
const unsigned long REMINDER_2_AFTER_MS = 60UL * 1000UL;
const unsigned long REMINDER_3_AFTER_MS = 90UL * 1000UL;

const unsigned long HAPPY_ANIMATION_MS = 5000UL;
const unsigned long REFILL_ANIMATION_MS = 4000UL;
const unsigned long GOAL_ANIMATION_MS = 8000UL;

// Bottle-low warning animation.
// Non-blocking: uses millis(), so Wi-Fi/API requests still work while it pulses.
const int EMPTY_WARNING_RED = 255;
const int EMPTY_WARNING_GREEN = 55;
const int EMPTY_WARNING_BLUE = 0;
const unsigned long EMPTY_WARNING_PULSE_MS = 1000UL;

// ------------------------------------------------------------
// Sippo state machine types
// ------------------------------------------------------------

enum SippoMode {
  MODE_AWAKE,
  MODE_SLEEPING
};

enum SippoMood {
  MOOD_CONTENT,
  MOOD_HAPPY,
  MOOD_SAD_1,
  MOOD_SAD_2,
  MOOD_SAD_3,
  MOOD_SLEEPING,
  MOOD_GOAL,
  MOOD_REFILL,
  MOOD_EMPTY
};

enum SippoEvent {
  EV_SIP_DETECTED,
  EV_REFILL_DETECTED,
  EV_BOTTLE_EMPTY_OR_LOW,
  EV_SLEEP_MODE_STARTED,
  EV_SLEEP_MODE_ENDED,
  EV_DAILY_RESET,
  EV_FORCE_REMINDER_1,
  EV_FORCE_REMINDER_2,
  EV_FORCE_REMINDER_3
};

struct SippoState {
  SippoMode mode;
  SippoMood mood;

  int reminderLevel;
  int totalDrankMl;
  int bottleFillPercent;

  bool goalReached;

  unsigned long lastSipAt;
  unsigned long temporaryMoodUntil;
};

SippoState sippo;

// ------------------------------------------------------------
// Setup / loop
// ------------------------------------------------------------

void setup() {
  Serial.begin(9600);

  ledRing.begin();
  ledRing.setBrightness(LED_RING_BRIGHTNESS);
  ledRing.clear();
  ledRing.show();

  testLedRing();
  setRGB(0, 0, 0);
  setupSippoState();

  while (!Serial)
    ;

  setupScale();

  enable_WiFi();
  connect_WiFi();

  server.begin();
  printWifiStatus();

  applyMoodOutput();
}

void loop() {
  updateScaleReading();
  updateBottleEstimateFromScale();

  // Real scale events live here now. The Wizard-of-Oz HTTP buttons still
  // call dispatchSippoEvent(...) directly and remain available as fallback.
  pollSensorAdapters();

  updateSippoStateMachine();

  WiFiClient client = server.available();

  if (client) {
    handleClient(client);
  }
}

// ------------------------------------------------------------
// Scale / HX711 helpers
// ------------------------------------------------------------

void setupScale() {
  Serial.println(F("Setting up HX711 scale..."));

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);

  unsigned long startedWaitingAt = millis();

  while (!scale.is_ready() && millis() - startedWaitingAt < SCALE_STARTUP_WAIT_MS) {
    Serial.println(F("HX711 not ready yet. Waiting..."));
    delay(500);
  }

  if (!scale.is_ready()) {
    Serial.println(F("HX711 not ready. Webserver will continue without live scale readings."));
    Serial.println(F("Check VCC, GND, DT, SCK and the selected pins."));
    scaleReady = false;
    scaleTared = false;
    return;
  }

  scaleReady = true;
  scale.set_scale(SCALE_CALIBRATION_FACTOR);

  Serial.println(F("HX711 ready."));
  Serial.println(F("Taring scale... leave the platform empty."));
  delay(1000);
  scale.tare(20);

  scaleTared = true;
  smoothedWeightInitialized = false;
  lastScaleReadAt = 0;

  updateScaleReading(true);
  updateBottleEstimateFromScale();
  resetScaleEventTracking();

  Serial.println(F("Scale tare complete."));
}

void updateScaleReading() {
  updateScaleReading(false);
}

void updateScaleReading(bool forceRead) {
  unsigned long now = millis();

  if (!forceRead && now - lastScaleReadAt < SCALE_READ_INTERVAL_MS) {
    return;
  }

  if (!scale.is_ready()) {
    scaleReady = false;
    return;
  }

  scaleReady = true;

  if (!scaleTared) {
    return;
  }

  lastWeightGrams = scale.get_units(SCALE_READINGS_PER_SAMPLE);

  if (!smoothedWeightInitialized) {
    smoothedWeightGrams = lastWeightGrams;
    smoothedWeightInitialized = true;
  } else {
    smoothedWeightGrams =
      (SCALE_SMOOTHING_ALPHA * lastWeightGrams) + ((1.0 - SCALE_SMOOTHING_ALPHA) * smoothedWeightGrams);
  }

  lastScaleReadAt = millis();
}

void tareScale() {
  Serial.println(F("Manual scale tare requested."));

  if (!scale.is_ready()) {
    Serial.println(F("Cannot tare: HX711 is not ready."));
    scaleReady = false;
    scaleTared = false;
    return;
  }

  scaleReady = true;
  scale.set_scale(SCALE_CALIBRATION_FACTOR);

  Serial.println(F("Taring scale... leave the platform empty."));
  scale.tare(20);

  scaleTared = true;
  smoothedWeightInitialized = false;
  lastScaleReadAt = 0;

  updateScaleReading(true);
  updateBottleEstimateFromScale();
  resetScaleEventTracking();

  Serial.println(F("Manual scale tare complete."));
}

void updateBottleEstimateFromScale() {
  if (!scaleReady || !scaleTared || !smoothedWeightInitialized) {
    return;
  }

  // Use the latest raw-ish calibrated reading for presence detection.
  // The smoothed value lags behind during a lift, which can otherwise make a
  // removed bottle look like a very low/empty bottle for a moment.
  bottlePresent = lastWeightGrams >= BOTTLE_PRESENT_MIN_WEIGHT_GRAMS;

  if (!bottlePresent) {
    // Bottle is probably lifted/removed.
    // IMPORTANT: keep the last known water/fill values instead of forcing 0.
    // Otherwise every normal sip lift would briefly look like an empty bottle.
    return;
  }

  float currentGrossWeightGrams = smoothedWeightGrams;
  float waterMl = currentGrossWeightGrams - EMPTY_BOTTLE_WEIGHT_GRAMS;

  if (waterMl < 0.0) {
    waterMl = 0.0;
  }

  if (waterMl > BOTTLE_CAPACITY_ML) {
    waterMl = BOTTLE_CAPACITY_ML;
  }

  estimatedWaterMl = waterMl;
  estimatedBottleFillPercent = constrain(
    (int)((estimatedWaterMl * 100.0 / BOTTLE_CAPACITY_ML) + 0.5),
    0,
    100);

  // The physical scale is now the best source for bottle fill,
  // but only while the bottle is actually present.
  sippo.bottleFillPercent = estimatedBottleFillPercent;
}

void resetScaleEventTracking() {
  previousBottlePresent = bottlePresent;
  pendingBottleReturnEvaluation = false;
  pendingBottleReturnEarlyFeedbackChecked = false;
  lastKnownBottleWeightInitialized = false;
  lastKnownBottleWeightGrams = 0.0;
  weightBeforeBottleLiftGrams = 0.0;
  scaleDeltaSinceLastBottleWeightGrams = 0.0;
  bottleReturnedAt = 0;
  bottlePresentSinceAt = bottlePresent ? millis() : 0;
  bottleAbsentSinceAt = bottlePresent ? 0 : millis();
  lastScaleEventAt = 0;
  wizardEmptyOverride = false;
  setScaleEvent("scale_reset");

  if (bottlePresent) {
    acceptCurrentBottleWeightAsBaseline();
  }
}

void acceptCurrentBottleWeightAsBaseline() {
  if (!bottlePresent || !smoothedWeightInitialized) {
    return;
  }

  lastKnownBottleWeightGrams = smoothedWeightGrams;
  lastKnownBottleWeightInitialized = true;
  scaleDeltaSinceLastBottleWeightGrams = 0.0;
}

bool canShowEmptyWarningNow(unsigned long now) {
  if (wizardEmptyOverride) {
    return true;
  }

  // Without a reliable scale, fall back to the old behavior.
  if (!scaleAutoEventsEnabled || !scaleReady || !scaleTared) {
    return true;
  }

  // With scale auto detection enabled, "empty" only makes sense while the
  // bottle is actually on the platform. A lifted bottle must never look empty.
  if (!bottlePresent) {
    return false;
  }

  // Important: after the bottle returns, there is a settle/evaluation window.
  // During that window we may already know the bottle is low, but we still have
  // not decided whether this action was a sip or a refill. Do not show the
  // empty warning before the sip reward has had a chance to start.
  if (pendingBottleReturnEvaluation) {
    return false;
  }

  // Also require the bottle to sit stably for a short moment. This avoids the
  // UI flickering into the empty warning while the platform is still wobbling.
  if (bottlePresentSinceAt == 0) {
    return false;
  }

  if (now - bottlePresentSinceAt < BOTTLE_PRESENT_STABLE_FOR_EMPTY_MS) {
    return false;
  }

  return true;
}

void setScaleEvent(const char* eventName) {
  lastScaleEventName = eventName;
  scaleEventCounter++;
}

// ------------------------------------------------------------
// Wi-Fi helpers
// ------------------------------------------------------------

void printWifiStatus() {
  Serial.print(F("SSID: "));
  Serial.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  Serial.print(F("IP Address: "));
  Serial.println(ip);

  long rssi = WiFi.RSSI();
  Serial.print(F("signal strength (RSSI): "));
  Serial.print(rssi);
  Serial.println(F(" dBm"));

  Serial.print(F("To see this page in action, open a browser to http://"));
  Serial.println(ip);
}

void enable_WiFi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("Communication with WiFi module failed!"));

    while (true)
      ;
  }

  String fv = WiFi.firmwareVersion();

  if (fv < "1.0.0") {
    Serial.println(F("Please upgrade the firmware"));
  }
}

void connect_WiFi() {
  WiFi.setHostname("sippo");

  while (status != WL_CONNECTED) {
    Serial.print(F("Attempting to connect to SSID: "));
    Serial.println(ssid);

    status = WiFi.begin(ssid, pass);

    delay(10000);
  }
}

// ------------------------------------------------------------
// RGB output
// ------------------------------------------------------------

void setRGB(int redValue, int greenValue, int blueValue) {
  // Compatibility wrapper:
  // The rest of the Sippo code can keep using setRGB(r, g, b),
  // but instead of driving three PWM pins, we now fill the whole NeoPixel ring.
  ledRing.fill(ledRing.Color(redValue, greenValue, blueValue));
  ledRing.show();
}

void testLedRing() {
  // Startup test: one white pixel walks around the ring once.
  for (int i = 0; i < LED_RING_PIXEL_COUNT; i++) {
    ledRing.clear();
    ledRing.setPixelColor(i, ledRing.Color(255, 255, 255));
    ledRing.show();
    delay(70);
  }

  // Then briefly show red, green, blue so you can spot wrong color order.
  ledRing.fill(ledRing.Color(255, 0, 0));
  ledRing.show();
  delay(300);

  ledRing.fill(ledRing.Color(0, 255, 0));
  ledRing.show();
  delay(300);

  ledRing.fill(ledRing.Color(0, 0, 255));
  ledRing.show();
  delay(300);

  ledRing.clear();
  ledRing.show();
}

void applyEmptyBottleWarningOutput() {
  unsigned long phase = millis() % EMPTY_WARNING_PULSE_MS;

  // Triangle wave: dim -> bright -> dim.
  // Minimum is not 0, so the warning feels like pulsing instead of disappearing.
  int pulse;

  if (phase < EMPTY_WARNING_PULSE_MS / 2) {
    pulse = map(phase, 0, EMPTY_WARNING_PULSE_MS / 2, 35, 255);
  } else {
    pulse = map(phase, EMPTY_WARNING_PULSE_MS / 2, EMPTY_WARNING_PULSE_MS, 255, 35);
  }

  int red = (EMPTY_WARNING_RED * pulse) / 255;
  int green = (EMPTY_WARNING_GREEN * pulse) / 255;
  int blue = (EMPTY_WARNING_BLUE * pulse) / 255;

  setRGB(red, green, blue);
}

void applyMoodOutput() {
  switch (sippo.mood) {
    case MOOD_CONTENT:
      setRGB(0, 201, 204);
      break;

    case MOOD_HAPPY:
      setRGB(1, 255, 0);
      break;

    case MOOD_SAD_1:
      setRGB(255, 213, 0);
      break;

    case MOOD_SAD_2:
      setRGB(255, 122, 0);
      break;

    case MOOD_SAD_3:
      setRGB(255, 42, 0);
      break;

    case MOOD_SLEEPING:
      setRGB(10, 0, 40);
      break;

    case MOOD_GOAL:
      setRGB(1, 255, 0);
      break;

    case MOOD_REFILL:
      setRGB(0, 201, 204);
      break;

    case MOOD_EMPTY:
      applyEmptyBottleWarningOutput();
      break;
  }
}

// ------------------------------------------------------------
// Sippo state machine
// ------------------------------------------------------------

void setupSippoState() {
  wizardEmptyOverride = false;

  sippo.mode = MODE_AWAKE;
  sippo.mood = MOOD_CONTENT;

  sippo.reminderLevel = 0;
  sippo.totalDrankMl = 0;
  sippo.bottleFillPercent = 100;

  sippo.goalReached = false;

  sippo.lastSipAt = millis();
  sippo.temporaryMoodUntil = 0;
}

SippoMood moodForReminderLevel(int level) {
  if (level <= 0) {
    return MOOD_CONTENT;
  }

  if (level == 1) {
    return MOOD_SAD_1;
  }

  if (level == 2) {
    return MOOD_SAD_2;
  }

  return MOOD_SAD_3;
}

void updateSippoStateMachine() {
  unsigned long now = millis();

  if (sippo.mode == MODE_SLEEPING) {
    sippo.mood = MOOD_SLEEPING;
    applyMoodOutput();
    return;
  }

  // Temporary moods are used for short animations:
  // happy, refill reaction, goal celebration.
  if (now < sippo.temporaryMoodUntil) {
    applyMoodOutput();
    return;
  }

  // Empty / low bottle is not just a short reaction.
  // With scale auto detection active, only show this after the bottle is back
  // on the platform and the sip/refill comparison window has finished.
  // Otherwise the UI can show "empty" before the sip reward.
  bool canShowEmptyWarning = canShowEmptyWarningNow(now);

  if (sippo.bottleFillPercent <= 10 && canShowEmptyWarning) {
    sippo.mood = MOOD_EMPTY;
    applyMoodOutput();
    return;
  }

  unsigned long sinceSip = now - sippo.lastSipAt;

  int timedReminderLevel = 0;

  if (sinceSip >= REMINDER_3_AFTER_MS) {
    timedReminderLevel = 3;
  } else if (sinceSip >= REMINDER_2_AFTER_MS) {
    timedReminderLevel = 2;
  } else if (sinceSip >= REMINDER_1_AFTER_MS) {
    timedReminderLevel = 1;
  }

  // Time can only increase the reminder level.
  // Drinking resets it back to 0.
  if (timedReminderLevel > sippo.reminderLevel) {
    sippo.reminderLevel = timedReminderLevel;
  }

  sippo.mood = moodForReminderLevel(sippo.reminderLevel);
  applyMoodOutput();
}


void applySipDetected(int amountMl) {
  unsigned long now = millis();

  if (amountMl <= 0) {
    amountMl = SIP_AMOUNT_ML;
  }

  sippo.mode = MODE_AWAKE;
  wizardEmptyOverride = false;
  sippo.lastSipAt = now;
  sippo.reminderLevel = 0;

  sippo.totalDrankMl += amountMl;

  if (bottlePresent && smoothedWeightInitialized) {
    sippo.bottleFillPercent = estimatedBottleFillPercent;
  } else {
    int fillDropPercent = max(1, (int)((long)amountMl * 100L / BOTTLE_CAPACITY_ML));
    sippo.bottleFillPercent = max(0, sippo.bottleFillPercent - fillDropPercent);
  }

  if (sippo.totalDrankMl >= DAILY_GOAL_ML) {
    sippo.goalReached = true;
    sippo.mood = MOOD_GOAL;
    sippo.temporaryMoodUntil = now + GOAL_ANIMATION_MS;
  } else {
    sippo.mood = MOOD_HAPPY;
    sippo.temporaryMoodUntil = now + HAPPY_ANIMATION_MS;
  }
}

void applyRefillDetected() {
  unsigned long now = millis();

  sippo.mode = MODE_AWAKE;
  wizardEmptyOverride = false;

  if (bottlePresent && smoothedWeightInitialized) {
    sippo.bottleFillPercent = estimatedBottleFillPercent;
  } else {
    sippo.bottleFillPercent = 100;
  }

  sippo.mood = MOOD_REFILL;
  sippo.temporaryMoodUntil = now + REFILL_ANIMATION_MS;
}

void applyEmptyDetected() {
  sippo.mode = MODE_AWAKE;
  sippo.bottleFillPercent = min(sippo.bottleFillPercent, 5);
  sippo.mood = MOOD_EMPTY;
  sippo.temporaryMoodUntil = 0;
}

void dispatchSippoEvent(SippoEvent event) {
  unsigned long now = millis();

  switch (event) {
    case EV_SIP_DETECTED:
      // Wizard-of-Oz fallback: fixed sip amount.
      applySipDetected(SIP_AMOUNT_ML);
      setScaleEvent("wizard_sip");
      break;

    case EV_REFILL_DETECTED:
      // Manual refill button is intentionally visual-only in the frontend.
      // The scale remains the source of truth for bottle weight/fill.
      // Keep this backend route as a safe no-op for older frontends.
      setScaleEvent("wizard_refill_visual_only");
      break;

    case EV_BOTTLE_EMPTY_OR_LOW:
      // Manual empty/low button is intentionally visual-only in the frontend.
      // Do NOT change bottleFillPercent here; otherwise lifting the bottle or
      // demo-clicking this button would corrupt the scale-owned bottle state.
      setScaleEvent("wizard_empty_visual_only");
      break;

    case EV_SLEEP_MODE_STARTED:
      sippo.mode = MODE_SLEEPING;
      sippo.mood = MOOD_SLEEPING;
      sippo.temporaryMoodUntil = 0;
      break;

    case EV_SLEEP_MODE_ENDED:
      sippo.mode = MODE_AWAKE;
      sippo.lastSipAt = now;
      sippo.reminderLevel = 0;
      sippo.mood = MOOD_CONTENT;
      sippo.temporaryMoodUntil = 0;
      break;

    case EV_DAILY_RESET:
      setupSippoState();
      break;

    case EV_FORCE_REMINDER_1:
      sippo.mode = MODE_AWAKE;
      sippo.reminderLevel = 1;
      sippo.mood = MOOD_SAD_1;
      sippo.temporaryMoodUntil = 0;
      break;

    case EV_FORCE_REMINDER_2:
      sippo.mode = MODE_AWAKE;
      sippo.reminderLevel = 2;
      sippo.mood = MOOD_SAD_2;
      sippo.temporaryMoodUntil = 0;
      break;

    case EV_FORCE_REMINDER_3:
      sippo.mode = MODE_AWAKE;
      sippo.reminderLevel = 3;
      sippo.mood = MOOD_SAD_3;
      sippo.temporaryMoodUntil = 0;
      break;
  }

  applyMoodOutput();
}

// ------------------------------------------------------------
// Text mappings for frontend JSON
// ------------------------------------------------------------

const char* modeToString(SippoMode mode) {
  switch (mode) {
    case MODE_AWAKE:
      return "awake";

    case MODE_SLEEPING:
      return "sleeping";

    default:
      return "unknown";
  }
}

const char* moodToString(SippoMood mood) {
  switch (mood) {
    case MOOD_CONTENT:
      return "content";

    case MOOD_HAPPY:
      return "happy";

    case MOOD_SAD_1:
      return "sad1";

    case MOOD_SAD_2:
      return "sad2";

    case MOOD_SAD_3:
      return "sad3";

    case MOOD_SLEEPING:
      return "sleeping";

    case MOOD_GOAL:
      return "goal";

    case MOOD_REFILL:
      return "refill";

    case MOOD_EMPTY:
      return "empty";

    default:
      return "unknown";
  }
}

const char* moodToHex(SippoMood mood) {
  switch (mood) {
    case MOOD_CONTENT:
      return "#00C9CC";

    case MOOD_HAPPY:
      return "#01FF00";

    case MOOD_SAD_1:
      return "#FFD500";

    case MOOD_SAD_2:
      return "#FF7A00";

    case MOOD_SAD_3:
      return "#FF2A00";

    case MOOD_SLEEPING:
      return "#14002D";

    case MOOD_GOAL:
      return "#01FF00";

    case MOOD_REFILL:
      return "#00C9CC";

    case MOOD_EMPTY:
      return "#FF3700";

    default:
      return "#00C9CC";
  }
}

// ------------------------------------------------------------
// HTTP helpers
// ------------------------------------------------------------

void sendCorsHeaders(WiFiClient& client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json"));
  client.println(F("Access-Control-Allow-Origin: *"));
  client.println(F("Access-Control-Allow-Methods: GET, OPTIONS"));
  client.println(F("Access-Control-Allow-Headers: Content-Type"));
  client.println(F("Connection: close"));
  client.println();
}

void sendNotFound(WiFiClient& client) {
  client.println(F("HTTP/1.1 404 Not Found"));
  client.println(F("Content-Type: application/json"));
  client.println(F("Access-Control-Allow-Origin: *"));
  client.println(F("Access-Control-Allow-Methods: GET, OPTIONS"));
  client.println(F("Access-Control-Allow-Headers: Content-Type"));
  client.println(F("Connection: close"));
  client.println();
  client.println(F("{\"status\":\"error\",\"message\":\"Route not found\"}"));
}

void sendStateJson(WiFiClient& client, const char* message) {
  sendCorsHeaders(client);

  // Keep HTTP responses fast. Sensor reads happen continuously in loop(),
  // and this function only returns the latest cached values.
  int goalPercent = min(
    100,
    (int)((long)sippo.totalDrankMl * 100L / DAILY_GOAL_ML));

  client.print(F("{\"status\":\"ok\""));

  client.print(F(",\"message\":\""));
  client.print(message);
  client.print(F("\""));

  client.print(F(",\"mode\":\""));
  client.print(modeToString(sippo.mode));
  client.print(F("\""));

  client.print(F(",\"mood\":\""));
  client.print(moodToString(sippo.mood));
  client.print(F("\""));

  client.print(F(",\"colorHex\":\""));
  client.print(moodToHex(sippo.mood));
  client.print(F("\""));

  client.print(F(",\"reminderLevel\":"));
  client.print(sippo.reminderLevel);

  client.print(F(",\"totalDrankMl\":"));
  client.print(sippo.totalDrankMl);

  client.print(F(",\"dailyGoalMl\":"));
  client.print(DAILY_GOAL_ML);

  client.print(F(",\"goalPercent\":"));
  client.print(goalPercent);

  client.print(F(",\"bottleFillPercent\":"));
  client.print(sippo.bottleFillPercent);

  client.print(F(",\"goalReached\":"));
  client.print(sippo.goalReached ? F("true") : F("false"));

  client.print(F(",\"scaleReady\":"));
  client.print(scaleReady ? F("true") : F("false"));

  client.print(F(",\"scaleTared\":"));
  client.print(scaleTared ? F("true") : F("false"));

  client.print(F(",\"weightGrams\":"));
  client.print(lastWeightGrams, 1);

  client.print(F(",\"smoothedWeightGrams\":"));
  client.print(smoothedWeightGrams, 1);

  client.print(F(",\"scaleCalibrationFactor\":"));
  client.print(SCALE_CALIBRATION_FACTOR, 1);

  client.print(F(",\"scaleLastReadAgeMs\":"));
  client.print(lastScaleReadAt == 0 ? 0 : millis() - lastScaleReadAt);

  client.print(F(",\"scaleAutoEventsEnabled\":"));
  client.print(scaleAutoEventsEnabled ? F("true") : F("false"));

  client.print(F(",\"bottlePresent\":"));
  client.print(bottlePresent ? F("true") : F("false"));

  client.print(F(",\"estimatedWaterMl\":"));
  client.print(estimatedWaterMl, 1);

  client.print(F(",\"estimatedBottleFillPercent\":"));
  client.print(estimatedBottleFillPercent);

  client.print(F(",\"emptyBottleWeightGrams\":"));
  client.print(EMPTY_BOTTLE_WEIGHT_GRAMS, 1);

  client.print(F(",\"bottleCapacityMl\":"));
  client.print(BOTTLE_CAPACITY_ML);

  client.print(F(",\"lastKnownBottleWeightGrams\":"));
  client.print(lastKnownBottleWeightGrams, 1);

  client.print(F(",\"scaleDeltaSinceLastBottleWeightGrams\":"));
  client.print(scaleDeltaSinceLastBottleWeightGrams, 1);

  client.print(F(",\"scaleEventCounter\":"));
  client.print(scaleEventCounter);

  client.print(F(",\"lastScaleEvent\":\""));
  client.print(lastScaleEventName);
  client.print(F("\""));

  client.print(F(",\"emptyWarningEligible\":"));
  client.print(canShowEmptyWarningNow(millis()) ? F("true") : F("false"));

  client.print(F(",\"pendingBottleReturnEvaluation\":"));
  client.print(pendingBottleReturnEvaluation ? F("true") : F("false"));

  client.println(F("}"));
}

// ------------------------------------------------------------
// WIZARD-OF-OZ INPUT LAYER / HTTP-Request Handling
// Current source of Sippo events: React buttons.
// Later this can stay as a debug/test interface, but dispatchEvent will be called by another function
// ------------------------------------------------------------
bool waitForScaleReady(unsigned long timeoutMs) {
  unsigned long startedAt = millis();

  while (millis() - startedAt < timeoutMs) {
    if (scale.is_ready()) {
      return true;
    }

    delay(10);
  }

  return false;
}

void handleClient(WiFiClient& client) {
  Serial.println(F("new client"));

  client.setTimeout(200);

  String requestLine = client.readStringUntil('\r');
  client.readStringUntil('\n');

  Serial.print(F("Request: "));
  Serial.println(requestLine);

  // Read and ignore the rest of the HTTP headers.
  while (client.connected()) {
    String headerLine = client.readStringUntil('\n');

    if (headerLine == "\r" || headerLine.length() == 0) {
      break;
    }
  }

  if (requestLine.startsWith("OPTIONS ")) {
    sendCorsHeaders(client);
    client.println(F("{\"status\":\"ok\"}"));
  }

  else if (requestLine.startsWith("GET /api/state")) {
    updateSippoStateMachine();
    sendStateJson(client, "Current Sippo state");
  }

  else if (requestLine.startsWith("GET /api/scale/tare")) {
    Serial.println(F("Scale tare requested. Waiting for HX711..."));

    // The HX711 may be temporarily not-ready if the loop just read it.
    // Do not fail immediately; wait a few seconds for a fresh sample.
    if (!waitForScaleReady(3000)) {
      scaleReady = false;
      setScaleEvent("scale_tare_failed");
      sendStateJson(client, "Scale tare failed - HX711 not ready");
      return;
    }

    Serial.println(F("Taring scale. Platform should be empty."));

    bool previousScaleAutoEventsEnabled = scaleAutoEventsEnabled;
    scaleAutoEventsEnabled = false;

    scaleReady = true;
    scale.set_scale(SCALE_CALIBRATION_FACTOR);

    scale.tare(20);

    scaleTared = true;

    lastWeightGrams = 0.0;
    smoothedWeightGrams = 0.0;
    smoothedWeightInitialized = false;
    lastScaleReadAt = 0;

    bottlePresent = false;
    previousBottlePresent = false;

    pendingBottleReturnEvaluation = false;
    pendingBottleReturnEarlyFeedbackChecked = false;

    lastKnownBottleWeightInitialized = false;
    lastKnownBottleWeightGrams = 0.0;
    weightBeforeBottleLiftGrams = 0.0;
    scaleDeltaSinceLastBottleWeightGrams = 0.0;

    bottleReturnedAt = 0;
    bottlePresentSinceAt = 0;
    bottleAbsentSinceAt = millis();

    wizardEmptyOverride = false;

    // Read once after tare if possible, then reset the detection state.
    updateScaleReading(true);
    updateBottleEstimateFromScale();
    resetScaleEventTracking();

    scaleAutoEventsEnabled = previousScaleAutoEventsEnabled;

    setScaleEvent("scale_tared");

    Serial.println(F("Scale tare complete."));
    sendStateJson(client, "Scale tared");
  }

  else if (requestLine.startsWith("GET /api/scale/auto/on")) {
    scaleAutoEventsEnabled = true;
    wizardEmptyOverride = false;
    setScaleEvent("scale_auto_on");
    if (bottlePresent) {
      acceptCurrentBottleWeightAsBaseline();
    }
    sendStateJson(client, "Scale auto detection enabled");
  }

  else if (requestLine.startsWith("GET /api/scale/auto/off")) {
    scaleAutoEventsEnabled = false;
    pendingBottleReturnEvaluation = false;
    pendingBottleReturnEarlyFeedbackChecked = false;
    setScaleEvent("scale_auto_off");
    sendStateJson(client, "Scale auto detection disabled");
  }

  else if (requestLine.startsWith("GET /api/scale/baseline")) {
    wizardEmptyOverride = false;
    updateScaleReading(true);
    updateBottleEstimateFromScale();
    acceptCurrentBottleWeightAsBaseline();
    setScaleEvent("baseline_set");
    sendStateJson(client, "Current bottle weight accepted as baseline");
  }

  else if (requestLine.startsWith("GET /api/event/sip")) {
    dispatchSippoEvent(EV_SIP_DETECTED);
    sendStateJson(client, "Sip detected");
  }

  else if (requestLine.startsWith("GET /api/event/refill")) {
    dispatchSippoEvent(EV_REFILL_DETECTED);
    sendStateJson(client, "Refill button is visual-only; scale keeps bottle state");
  }

  else if (requestLine.startsWith("GET /api/event/empty")) {
    dispatchSippoEvent(EV_BOTTLE_EMPTY_OR_LOW);
    sendStateJson(client, "Empty button is visual-only; scale keeps bottle state");
  }

  else if (requestLine.startsWith("GET /api/event/sleep")) {
    dispatchSippoEvent(EV_SLEEP_MODE_STARTED);
    sendStateJson(client, "Sleep mode started");
  }

  else if (requestLine.startsWith("GET /api/event/wake")) {
    dispatchSippoEvent(EV_SLEEP_MODE_ENDED);
    sendStateJson(client, "Sleep mode ended");
  }

  else if (requestLine.startsWith("GET /api/event/reset")) {
    dispatchSippoEvent(EV_DAILY_RESET);
    sendStateJson(client, "Daily state reset");
  }

  else if (requestLine.startsWith("GET /api/event/reminder1")) {
    dispatchSippoEvent(EV_FORCE_REMINDER_1);
    sendStateJson(client, "Reminder level 1 forced");
  }

  else if (requestLine.startsWith("GET /api/event/reminder2")) {
    dispatchSippoEvent(EV_FORCE_REMINDER_2);
    sendStateJson(client, "Reminder level 2 forced");
  }

  else if (requestLine.startsWith("GET /api/event/reminder3")) {
    dispatchSippoEvent(EV_FORCE_REMINDER_3);
    sendStateJson(client, "Reminder level 3 forced");
  }

  else if (requestLine.startsWith("GET / ")) {
    updateSippoStateMachine();
    sendStateJson(client, "Sippo Arduino state API is running");
  }

  else {
    sendNotFound(client);
  }

  client.stop();
  Serial.println(F("client disconnected"));
}

// ------------------------------------------------------------
// Future sensor adapter placeholder
// ------------------------------------------------------------

void pollSensorAdapters() {
  if (!scaleAutoEventsEnabled) {
    return;
  }

  if (!scaleReady || !scaleTared || !smoothedWeightInitialized) {
    return;
  }

  if (sippo.mode == MODE_SLEEPING) {
    return;
  }

  unsigned long now = millis();

  // Use the non-smoothed current reading for "is the bottle on the platform?"
  // This makes lift/remove transitions much faster and avoids the smoothed value
  // slowly falling through "almost empty" while the bottle is already gone.
  bool currentBottlePresent = lastWeightGrams >= BOTTLE_PRESENT_MIN_WEIGHT_GRAMS;
  bottlePresent = currentBottlePresent;

  // Detect lift / return transitions first. A sip usually looks like:
  // bottle present -> bottle lifted away -> bottle placed back with lower weight.
  if (currentBottlePresent != previousBottlePresent) {
    if (!currentBottlePresent) {
      bottleAbsentSinceAt = now;
      bottlePresentSinceAt = 0;

      if (lastKnownBottleWeightInitialized) {
        weightBeforeBottleLiftGrams = lastKnownBottleWeightGrams;
      }

      pendingBottleReturnEvaluation = false;
      pendingBottleReturnEarlyFeedbackChecked = false;
      wizardEmptyOverride = false;
      setScaleEvent("bottle_lifted");
    } else {
      bottlePresentSinceAt = now;
      bottleAbsentSinceAt = 0;

      // The smoothing value still contains readings from while the bottle was
      // missing. Reset it to the current reading so the return comparison does
      // not look like a fake refill/sip because of smoothing lag.
      smoothedWeightGrams = lastWeightGrams;
      smoothedWeightInitialized = true;
      updateBottleEstimateFromScale();

      bottleReturnedAt = now;
      pendingBottleReturnEvaluation = true;
      pendingBottleReturnEarlyFeedbackChecked = false;
      setScaleEvent("bottle_returned_waiting");
    }

    previousBottlePresent = currentBottlePresent;
    return;
  }

  if (!currentBottlePresent) {
    // Bottle is not on the platform. Do not detect sip/refill/empty now.
    return;
  }

  if (!lastKnownBottleWeightInitialized) {
    // First time a bottle appears after startup/tare: treat it as the
    // baseline, not as a refill.
    pendingBottleReturnEvaluation = false;
    pendingBottleReturnEarlyFeedbackChecked = false;
    acceptCurrentBottleWeightAsBaseline();
    weightBeforeBottleLiftGrams = lastKnownBottleWeightGrams;
    bottlePresentSinceAt = now;
    setScaleEvent("baseline_initialized");
    return;
  }

  if (pendingBottleReturnEvaluation) {
    // Middle ground for perceived agency:
    // After a short delay, do one quick, non-committing check. If the bottle is
    // already clearly lighter, tell the frontend it may start the happy
    // reaction now. We still do NOT update drank ml or the baseline until the
    // full settle window below has passed.
    if (
      !pendingBottleReturnEarlyFeedbackChecked && now - bottleReturnedAt >= EARLY_SIP_FEEDBACK_AFTER_RETURN_MS) {
      pendingBottleReturnEarlyFeedbackChecked = true;

      updateScaleReading(true);
      float earlyDeltaGrams = lastWeightGrams - weightBeforeBottleLiftGrams;
      scaleDeltaSinceLastBottleWeightGrams = earlyDeltaGrams;

      if (
        earlyDeltaGrams <= -EARLY_SIP_FEEDBACK_MIN_DROP_GRAMS && earlyDeltaGrams >= -SIP_DETECTION_MAX_DROP_GRAMS) {
        setScaleEvent("scale_sip_candidate");
      }
    }

    if (now - bottleReturnedAt < BOTTLE_RETURN_SETTLE_MS) {
      return;
    }

    pendingBottleReturnEvaluation = false;
    pendingBottleReturnEarlyFeedbackChecked = false;

    // Force one fresh sample after the settle time, then compare against the
    // stable weight from before the bottle was lifted.
    updateScaleReading(true);
    smoothedWeightGrams = lastWeightGrams;
    updateBottleEstimateFromScale();

    float currentWeightGrams = smoothedWeightGrams;
    float deltaGrams = currentWeightGrams - weightBeforeBottleLiftGrams;
    scaleDeltaSinceLastBottleWeightGrams = deltaGrams;

    if (now - lastScaleEventAt < SCALE_EVENT_COOLDOWN_MS) {
      acceptCurrentBottleWeightAsBaseline();
      setScaleEvent("ignored_cooldown");
      return;
    }

    if (deltaGrams <= -SIP_DETECTION_MIN_DROP_GRAMS && deltaGrams >= -SIP_DETECTION_MAX_DROP_GRAMS) {
      int sipAmountMl = (int)((-deltaGrams) + 0.5);

      Serial.print(F("Scale sip detected, ml: "));
      Serial.println(sipAmountMl);

      wizardEmptyOverride = false;
      applySipDetected(sipAmountMl);
      acceptCurrentBottleWeightAsBaseline();
      lastScaleEventAt = now;
      setScaleEvent("scale_sip");
      applyMoodOutput();
      return;
    }

    if (deltaGrams >= REFILL_DETECTION_MIN_RISE_GRAMS) {
      Serial.print(F("Scale refill detected, rise g: "));
      Serial.println(deltaGrams, 1);

      wizardEmptyOverride = false;
      applyRefillDetected();
      acceptCurrentBottleWeightAsBaseline();
      lastScaleEventAt = now;
      setScaleEvent("scale_refill");
      applyMoodOutput();
      return;
    }

    // Bottle was lifted but came back with nearly the same weight.
    acceptCurrentBottleWeightAsBaseline();
    setScaleEvent("bottle_returned_no_change");
    return;
  }

  // Keep the baseline from drifting too far due to tiny noise, but do not follow
  // real sip-sized changes. This keeps long idle periods stable.
  float directDeltaGrams = smoothedWeightGrams - lastKnownBottleWeightGrams;
  scaleDeltaSinceLastBottleWeightGrams = directDeltaGrams;

  if (directDeltaGrams > -5.0 && directDeltaGrams < 5.0) {
    lastKnownBottleWeightGrams = smoothedWeightGrams;
  }

  // Only show the low/empty warning when the bottle is actually sitting on the
  // platform and has been stable there for a moment. This prevents every lift
  // from becoming an empty-bottle warning.
  bool bottleStableForEmptyWarning = canShowEmptyWarningNow(now);

  // If the sip that just happened made the bottle low/empty, do not interrupt
  // the reward animation. The state machine already keeps temporary moods
  // such as HAPPY/GOAL visible until temporaryMoodUntil. Only after that
  // window is over may the scale switch to the empty-bottle warning.
  bool rewardOrReactionStillPlaying = now < sippo.temporaryMoodUntil;

  if (
    bottleStableForEmptyWarning && !pendingBottleReturnEvaluation && !rewardOrReactionStillPlaying && estimatedWaterMl <= EMPTY_BOTTLE_WARNING_WATER_ML && sippo.mood != MOOD_EMPTY) {
    wizardEmptyOverride = false;
    applyEmptyDetected();
    setScaleEvent("scale_empty");
    applyMoodOutput();
  }
}