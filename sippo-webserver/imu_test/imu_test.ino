#include <Arduino_LSM6DS3.h>

// ------------------------------------------------------------
// Sippo IMU test sketch
// ------------------------------------------------------------
//
// Dieses Sketch ist nur zum Testen deiner integrierten IMU gedacht.
// Es ist getrennt vom eigentlichen sippo-webserver.ino, damit du erst
// pruefen kannst, ob die IMU Werte liefert und welche Bewegungen du
// spaeter fuer Sippo verwenden kannst.
//
// Annahme:
// Dieses Sketch nutzt die Library Arduino_LSM6DS3.
// Sie passt z. B. zu Boards mit integrierter LSM6DS3-IMU.
//
// Falls dein Board eine andere IMU hat, kompiliert dieses Sketch evtl.
// nicht. Dann brauchen wir den genauen Board-/Sensornamen und tauschen
// die Library aus.
//
// Vorgehen mit Hardware:
//
// 1. Arduino IDE oeffnen.
//
// 2. Library installieren:
//    Arduino IDE > Library Manager > "Arduino_LSM6DS3" suchen
//    und installieren.
//
// 3. Dieses Sketch oeffnen:
//    sippo-webserver/imu_test/imu_test.ino
//
// 4. Board und Port auswaehlen:
//    Tools > Board
//    Tools > Port
//
// 5. Sketch hochladen.
//
// 6. Serial Monitor oeffnen:
//    Tools > Serial Monitor
//    Baudrate: 9600
//
// 7. Arduino ruhig liegen lassen.
//    Du solltest Werte fuer Acceleration und Gyroscope sehen.
//
// 8. Arduino bewegen, kippen und vorsichtig schuetteln.
//    Im Serial Monitor erscheinen einfache Hinweise:
//    - MOVING
//    - TILTED
//    - SHAKE
//
// Fuer Sippo spaeter interessant:
//
// - Kippen plus danach weniger Gewicht:
//   -> EV_SIP_DETECTED
//
// - Starke Bewegung im Schlafmodus:
//   -> EV_SLEEP_MODE_ENDED
//
// - Lange keine Bewegung:
//   -> optional EV_SLEEP_MODE_STARTED
//
// Wichtig:
// Die IMU alleine erkennt nicht sicher, ob wirklich getrunken wurde.
// Sie ist am besten als Zusatzsignal zur Waage.

// ------------------------------------------------------------
// Detection thresholds
// ------------------------------------------------------------

// Acceleration wird von Arduino_LSM6DS3 in g ausgegeben.
// Bei ruhig liegendem Board liegt die Gesamtbeschleunigung grob bei 1 g.
const float MOVEMENT_DELTA_THRESHOLD = 0.18;
const float SHAKE_DELTA_THRESHOLD = 0.75;

// Gyroscope wird in Grad pro Sekunde ausgegeben.
const float TILT_GYRO_THRESHOLD = 80.0;

// Ausgabeintervall.
const unsigned long PRINT_INTERVAL_MS = 250;

float lastAccelerationMagnitude = 1.0;
unsigned long lastPrintAt = 0;

float absoluteValue(float value)
{
  if (value < 0) {
    return -value;
  }

  return value;
}

float vectorMagnitude(float x, float y, float z)
{
  return sqrt(x * x + y * y + z * z);
}

void setup()
{
  Serial.begin(9600);
  delay(1000);

  Serial.println();
  Serial.println(F("Sippo IMU test"));
  Serial.println(F("--------------"));

  if (!IMU.begin()) {
    Serial.println(F("Failed to initialize IMU."));
    Serial.println(F("Check whether your Arduino board has an LSM6DS3 IMU."));
    Serial.println(F("If your board uses another IMU, use the matching library."));

    while (true) {
      delay(1000);
    }
  }

  Serial.println(F("IMU initialized."));
  Serial.println(F("Move, tilt, or gently shake the Arduino."));
  Serial.println();
}

void loop()
{
  float ax = 0.0;
  float ay = 0.0;
  float az = 0.0;
  float gx = 0.0;
  float gy = 0.0;
  float gz = 0.0;

  bool hasAcceleration = IMU.accelerationAvailable();
  bool hasGyroscope = IMU.gyroscopeAvailable();

  if (hasAcceleration) {
    IMU.readAcceleration(ax, ay, az);
  }

  if (hasGyroscope) {
    IMU.readGyroscope(gx, gy, gz);
  }

  unsigned long now = millis();

  if (now - lastPrintAt < PRINT_INTERVAL_MS) {
    return;
  }

  lastPrintAt = now;

  float accelerationMagnitude = vectorMagnitude(ax, ay, az);
  float accelerationDelta = absoluteValue(accelerationMagnitude - lastAccelerationMagnitude);
  float gyroMagnitude = vectorMagnitude(gx, gy, gz);

  bool isMoving = accelerationDelta >= MOVEMENT_DELTA_THRESHOLD;
  bool isShaking = accelerationDelta >= SHAKE_DELTA_THRESHOLD;
  bool isTilting = gyroMagnitude >= TILT_GYRO_THRESHOLD;

  Serial.print(F("Acceleration g | x: "));
  Serial.print(ax, 3);
  Serial.print(F(" y: "));
  Serial.print(ay, 3);
  Serial.print(F(" z: "));
  Serial.print(az, 3);

  Serial.print(F(" | Gyro dps | x: "));
  Serial.print(gx, 1);
  Serial.print(F(" y: "));
  Serial.print(gy, 1);
  Serial.print(F(" z: "));
  Serial.print(gz, 1);

  Serial.print(F(" | accelDelta: "));
  Serial.print(accelerationDelta, 3);

  Serial.print(F(" | gyroMagnitude: "));
  Serial.print(gyroMagnitude, 1);

  if (isMoving) {
    Serial.print(F(" | MOVING"));
  }

  if (isTilting) {
    Serial.print(F(" | TILTED"));
  }

  if (isShaking) {
    Serial.print(F(" | SHAKE"));
  }

  Serial.println();

  lastAccelerationMagnitude = accelerationMagnitude;
}
