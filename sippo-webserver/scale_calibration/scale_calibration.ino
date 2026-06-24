#include "HX711.h"

// ------------------------------------------------------------
// Sippo scale calibration sketch
// ------------------------------------------------------------
//
// Dieser Sketch ist nur zum Kalibrieren deiner Waegezelle gedacht.
// Es ist absichtlich getrennt vom eigentlichen sippo-webserver.ino,
// damit du morgen die Waage testen kannst, ohne die ganze Sippo-Logik
// gleichzeitig debuggen zu muessen.
//
// Vorgehen morgen mit Hardware:
//
// 1. HX711-Library installieren:
//    Arduino IDE > Library Manager > "HX711" suchen und installieren.
//
// 2. Waegezelle mit HX711-Verstaerker verbinden.
//    Die vier Kabel der Waegezelle kommen an E+, E-, A+ und A-.
//    Die Farben koennen je nach Waegezelle abweichen, also pruefe
//    am besten das Datenblatt oder die Beschriftung deines Moduls.
//
// 3. HX711 mit dem Arduino verbinden:
//    HX711 VCC  -> 5V oder 3.3V, je nach Board/Modul
//    HX711 GND  -> GND
//    HX711 DT   -> Arduino Pin HX711_DT_PIN
//    HX711 SCK  -> Arduino Pin HX711_SCK_PIN
//
// 4. Falls du andere Pins verwendest, unten HX711_DT_PIN und
//    HX711_SCK_PIN anpassen.
//
// 5. Sketch einmal hochladen und Serial Monitor oeffnen:
// So öffnet man den Serial Monitor in der Arduino IDE:
//    - erstmal IDE öffnen, sketch öffnen und board und port wählen
//    - Sketch hochladen
//    - dann entweder rachts auf das Lupensymbol klicken
//    - oder über Menüleiste: Werkzeuge > Serieller Monitor
//    dann Baudrate einstellen:
//    Baudrate: 9600
//    Line ending: "No line ending" ist okay
//
// 6. Waage leer lassen und Reset druecken.
//    Beim Start wird scale.tare() ausgefuehrt. Das aktuelle Gewicht
//    wird also als 0 g gesetzt.
//
// 7. Ein bekanntes Gewicht auflegen, z. B. 500 g oder 1000 g.
//    Der Serial Monitor zeigt fortlaufend Messwerte an.
//
// 8. calibrationFactor direkt im Serial Monitor anpassen.
//    Du musst den Sketch dafuer NICHT jedes Mal neu hochladen.
//
//    Sende im Serial Monitor einzelne Zeichen:
//    +  -> Faktor leicht erhoehen
//    -  -> Faktor leicht verringern
//    a  -> Faktor stark erhoehen
//    z  -> Faktor stark verringern
//    t  -> Tara erneut setzen, also aktuelles Gewicht als 0 g
//    h  -> Hilfe erneut anzeigen
//
//    Wichtig:
//    Nach dem Tippen eines Zeichens im Serial Monitor musst du Enter
//    oder den Senden-Button druecken, damit der Arduino das Zeichen
//    bekommt.
//
// 9. Passe den Faktor so lange an, bis der angezeigte Wert
//    ungefaehr deinem bekannten Gewicht in Gramm entspricht.
//
//    Beispiel:
//    Bekanntes Gewicht: 500 g
//    Anzeige: 250 g
//    Faktor ist etwa halb so stark -> Betrag des Faktors vergroessern.
//
//    Anzeige: -500 g
//    Vorzeichen ist falsch -> Faktor mit anderem Vorzeichen probieren.
//    Das geht in diesem Sketch ueber viele + / - Eingaben oder schneller,
//    indem du START_CALIBRATION_FACTOR unten mit anderem Vorzeichen setzt
//    und das Sketch einmal neu hochlaedst.
//
// 10. Den finalen calibrationFactor aus dem Serial Monitor notieren.
//    Diesen Wert brauchst du spaeter in sippo-webserver.ino.
//
// 11. Danach zusaetzlich sinnvolle Sippo-Werte notieren:
//     - Gewicht leerer Flaschenhalter / leere Plattform
//     - Gewicht mit leerer Flasche
//     - Gewicht mit voller Flasche
//     - typische Gewichtsabnahme nach einem Schluck
//
// Diese Werte helfen spaeter, Events zu erkennen:
// - Gewicht sinkt deutlich      -> EV_SIP_DETECTED
// - Gewicht steigt deutlich     -> EV_REFILL_DETECTED
// - Gewicht ist sehr niedrig    -> EV_BOTTLE_EMPTY_OR_LOW

// ------------------------------------------------------------
// Pin configuration
// ------------------------------------------------------------

const int HX711_DT_PIN = 3;
const int HX711_SCK_PIN = 2;

// Startwert. Dieser Wert ist fast sicher noch nicht perfekt.
// Du kannst ihn morgen live ueber den Serial Monitor veraendern.
//
// Hinweis:
// Bei vielen Setups muss der Faktor negativ sein. Wenn deine Werte
// beim Auflegen eines Gewichts negativ werden, aendere das Vorzeichen.
const float START_CALIBRATION_FACTOR = -7050.0;

// Wie viele Einzelmessungen pro Anzeige gemittelt werden.
// Hoeher = stabiler, aber langsamer.
const int READINGS_PER_SAMPLE = 10;

// Schrittweiten fuer die Anpassung im Serial Monitor.
// Wenn sich der Wert kaum bewegt, kannst du diese Zahlen groesser machen.
const float SMALL_FACTOR_STEP = 10.0;
const float LARGE_FACTOR_STEP = 100.0;

float calibrationFactor = START_CALIBRATION_FACTOR;

HX711 scale;

void printHelp()
{
  Serial.println();
  Serial.println(F("Serial Monitor commands"));
  Serial.println(F("-----------------------"));
  Serial.println(F("+  increase calibration factor slightly"));
  Serial.println(F("-  decrease calibration factor slightly"));
  Serial.println(F("a  increase calibration factor strongly"));
  Serial.println(F("z  decrease calibration factor strongly"));
  Serial.println(F("t  tare again, current weight becomes 0 g"));
  Serial.println(F("h  show this help"));
  Serial.println();
}

void applyCalibrationFactor()
{
  scale.set_scale(calibrationFactor);
}

void handleSerialCommand(char command)
{
  switch (command)
  {
  case '+':
    calibrationFactor += SMALL_FACTOR_STEP;
    applyCalibrationFactor();
    break;

  case '-':
    calibrationFactor -= SMALL_FACTOR_STEP;
    applyCalibrationFactor();
    break;

  case 'a':
  case 'A':
    calibrationFactor += LARGE_FACTOR_STEP;
    applyCalibrationFactor();
    break;

  case 'z':
  case 'Z':
    calibrationFactor -= LARGE_FACTOR_STEP;
    applyCalibrationFactor();
    break;

  case 't':
  case 'T':
    Serial.println(F("Taring... leave the scale empty."));
    scale.tare();
    Serial.println(F("Tare complete."));
    break;

  case 'h':
  case 'H':
    printHelp();
    break;

  case '\n':
  case '\r':
    break;

  default:
    Serial.println(F("Unknown command. Send h for help."));
    break;
  }
}

void setup()
{
  Serial.begin(9600);

  // Manche Boards brauchen einen Moment, bis der Serial Monitor bereit ist.
  delay(1000);

  Serial.println();
  Serial.println(F("Sippo scale calibration"));
  Serial.println(F("-----------------------"));
  Serial.println(F("Leave the scale empty while tare is running."));

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);

  if (!scale.is_ready())
  {
    Serial.println(F("HX711 not ready."));
    Serial.println(F("Check VCC, GND, DT, SCK and the selected pins."));
    Serial.println(F("The sketch will continue trying to read values."));
  }

  applyCalibrationFactor();

  // Tara: aktuelles Gewicht wird als 0 g gesetzt.
  // Wichtig: Beim Einschalten/Reset darf noch kein Testgewicht aufliegen.
  scale.tare();

  Serial.println(F("Tare complete."));
  Serial.println(F("Place a known weight on the scale now."));
  Serial.println(F("Adjust calibrationFactor until the shown value matches grams."));
  printHelp();
}

void loop()
{
  while (Serial.available() > 0)
  {
    char command = Serial.read();
    handleSerialCommand(command);
  }

  if (!scale.is_ready())
  {
    Serial.println(F("HX711 not ready. Check wiring."));
    delay(1000);
    return;
  }

  float weightGrams = scale.get_units(READINGS_PER_SAMPLE);

  Serial.print(F("Weight: "));
  Serial.print(weightGrams, 1);
  Serial.print(F(" g"));

  Serial.print(F(" | calibrationFactor: "));
  Serial.println(calibrationFactor, 1);

  delay(500);
}
