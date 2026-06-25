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

// Keep brightness conservative for the first test.
// You can increase this later, but full white at 16 pixels can draw a lot of current.
const int LED_RING_BRIGHTNESS = 40;

Adafruit_NeoPixel ledRing(
  LED_RING_PIXEL_COUNT,
  LED_RING_PIN,
  NEO_GRB + NEO_KHZ800
);

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
const unsigned long SCALE_READ_INTERVAL_MS = 500UL;
const unsigned long SCALE_STARTUP_WAIT_MS = 5000UL;
const int SCALE_READINGS_PER_SAMPLE = 3;

// Exponential smoothing for display/debug output.
// 0.0 = no movement, 1.0 = no smoothing.
const float SCALE_SMOOTHING_ALPHA = 0.25;

HX711 scale;

bool scaleReady = false;
bool scaleTared = false;

float lastWeightGrams = 0.0;
float smoothedWeightGrams = 0.0;
bool smoothedWeightInitialized = false;

unsigned long lastScaleReadAt = 0;

// Forward declarations for scale helper overloads.
void setupScale();
void updateScaleReading();
void updateScaleReading(bool forceRead);
void tareScale();

// ------------------------------------------------------------
// Sippo configuration
// ------------------------------------------------------------

const int DAILY_GOAL_ML = 2000;
const int SIP_AMOUNT_ML = 120;

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
  // Later, real sensor adapters will be called here.
  // pollSensorAdapters();

  // For now the scale is read-only and only exposed through /api/state.
  // Automatic Sippo events from the scale will be added after we observe
  // stable real-world bottle/full/sip/refill values.
  updateScaleReading();

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
      (SCALE_SMOOTHING_ALPHA * lastWeightGrams) +
      ((1.0 - SCALE_SMOOTHING_ALPHA) * smoothedWeightGrams);
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

  Serial.println(F("Manual scale tare complete."));
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
  // Keep warning until a refill event resets bottleFillPercent.
  if (sippo.bottleFillPercent <= 10) {
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

void dispatchSippoEvent(SippoEvent event) {
  unsigned long now = millis();

  switch (event) {
    case EV_SIP_DETECTED:
      sippo.mode = MODE_AWAKE;
      sippo.lastSipAt = now;
      sippo.reminderLevel = 0;

      sippo.totalDrankMl += SIP_AMOUNT_ML;
      sippo.bottleFillPercent = max(0, sippo.bottleFillPercent - 8);

      if (sippo.totalDrankMl >= DAILY_GOAL_ML) {
        sippo.goalReached = true;
        sippo.mood = MOOD_GOAL;
        sippo.temporaryMoodUntil = now + GOAL_ANIMATION_MS;
      } else {
        sippo.mood = MOOD_HAPPY;
        sippo.temporaryMoodUntil = now + HAPPY_ANIMATION_MS;
      }
      break;

    case EV_REFILL_DETECTED:
      sippo.mode = MODE_AWAKE;
      sippo.bottleFillPercent = 100;
      sippo.mood = MOOD_REFILL;
      sippo.temporaryMoodUntil = now + REFILL_ANIMATION_MS;
      break;

    case EV_BOTTLE_EMPTY_OR_LOW:
      sippo.mode = MODE_AWAKE;
      sippo.bottleFillPercent = 5;
      sippo.mood = MOOD_EMPTY;
      sippo.temporaryMoodUntil = 0;
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

  int goalPercent = min(
    100,
    (int)((long)sippo.totalDrankMl * 100L / DAILY_GOAL_ML)
  );

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

  updateScaleReading();

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

  client.println(F("}"));
}

// ------------------------------------------------------------
// WIZARD-OF-OZ INPUT LAYER / HTTP-Request Handling
// Current source of Sippo events: React buttons.
// Later this can stay as a debug/test interface, but dispatchEvent will be called by another function
// ------------------------------------------------------------

void handleClient(WiFiClient& client) {
  Serial.println(F("new client"));

  client.setTimeout(1000);

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
    tareScale();

    if (scaleReady && scaleTared) {
      sendStateJson(client, "Scale tared");
    } else {
      sendStateJson(client, "Scale tare failed - HX711 not ready");
    }
  }

  else if (requestLine.startsWith("GET /api/event/sip")) {
    dispatchSippoEvent(EV_SIP_DETECTED);
    sendStateJson(client, "Sip detected");
  }

  else if (requestLine.startsWith("GET /api/event/refill")) {
    dispatchSippoEvent(EV_REFILL_DETECTED);
    sendStateJson(client, "Refill detected");
  }

  else if (requestLine.startsWith("GET /api/event/empty")) {
    dispatchSippoEvent(EV_BOTTLE_EMPTY_OR_LOW);
    sendStateJson(client, "Bottle empty or low");
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
  // Later, real hardware detection should call the same event dispatcher
  // that the current Wizard-of-Oz web buttons use.
  //
  // The HX711 scale is already read in updateScaleReading(), but for now
  // it is intentionally read-only. After collecting stable values for:
  // - empty platform
  // - empty bottle
  // - full bottle
  // - one sip
  // - refill
  // we can compare smoothedWeightGrams against thresholds here.

  // Example:
  //
  // if (detectSipFromWeightAndTilt()) {
  //   dispatchSippoEvent(EV_SIP_DETECTED);
  // }
  //
  // if (detectRefillAfterBottleLift()) {
  //   dispatchSippoEvent(EV_REFILL_DETECTED);
  // }
  //
  // if (detectLongPress()) {
  //   dispatchSippoEvent(EV_SLEEP_MODE_STARTED);
  // }
  //
  // if (detectShakeWhileSleeping()) {
  //   dispatchSippoEvent(EV_SLEEP_MODE_ENDED);
  // }
}