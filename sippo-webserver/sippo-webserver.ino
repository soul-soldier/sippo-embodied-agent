#include <WiFiNINA.h>
#include "sippo-secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

int status = WL_IDLE_STATUS;
WiFiServer server(80);

const int PIN_RED = 5;
const int PIN_GREEN = 6;
const int PIN_BLUE = 9;

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

  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BLUE, OUTPUT);

  setRGB(0, 0, 0);
  setupSippoState();

  while (!Serial)
    ;

  enable_WiFi();
  connect_WiFi();

  server.begin();
  printWifiStatus();

  applyMoodOutput();
}

void loop() {
  // Later, real sensor adapters will be called here.
  // pollSensorAdapters();

  updateSippoStateMachine();

  WiFiClient client = server.available();

  if (client) {
    handleClient(client);
  }
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
  analogWrite(PIN_RED, redValue);
  analogWrite(PIN_GREEN, greenValue);
  analogWrite(PIN_BLUE, blueValue);
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
      setRGB(0, 119, 255);
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
      sippo.temporaryMoodUntil = now + REFILL_ANIMATION_MS;
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
      return "#0077FF";

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