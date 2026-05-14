#include <WiFiNINA.h>
#include "sippo-secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;    // your network password between the " "
int keyIndex = 0;             // your network key Index number (needed only for WEP)
int status = WL_IDLE_STATUS;  //connection status
WiFiServer server(80);        //server socket

const int PIN_RED = 5;
const int PIN_GREEN = 6;
const int PIN_BLUE = 9;

void setup() {
  Serial.begin(9600);

  // pinMode(ledPin, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BLUE, OUTPUT);
  setRGB(0, 0, 0);

  while (!Serial)
    ;

  enable_WiFi();
  connect_WiFi();

  server.begin();
  printWifiStatus();
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    handleClient(client);
  }
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");

  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
}

void enable_WiFi() {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true)
      ;
  }

  String fv = WiFi.firmwareVersion();
  if (fv < "1.0.0") {
    Serial.println("Please upgrade the firmware");
  }
}

void connect_WiFi() {
  // attempt to connect to Wifi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
}

void setRGB(int redValue, int greenValue, int blueValue) {
  analogWrite(PIN_RED, redValue);
  analogWrite(PIN_GREEN, greenValue);
  analogWrite(PIN_BLUE, blueValue);
}

void sendCorsHeaders(WiFiClient& client, const char* contentType = "application/json") {
  client.println("HTTP/1.1 200 OK");
  client.print("Content-Type: ");
  client.println(contentType);
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Connection: close");
  client.println();
}

void sendNotFound(WiFiClient& client) {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Connection: close");
  client.println();
  client.println("{\"status\":\"error\",\"message\":\"Route not found\"}");
}

void handleClient(WiFiClient& client) {
  Serial.println("new client");

  client.setTimeout(1000);

  String requestLine = client.readStringUntil('\r');
  client.readStringUntil('\n');

  Serial.print("Request: ");
  Serial.println(requestLine);

  // Read and ignore the rest of the HTTP headers
  while (client.connected()) {
    String headerLine = client.readStringUntil('\n');

    if (headerLine == "\r" || headerLine.length() == 0) {
      break;
    }
  }

  // Browser CORS preflight support
  if (requestLine.startsWith("OPTIONS ")) {
    sendCorsHeaders(client);
    client.println("{\"status\":\"ok\"}");
  }

  else if (requestLine.startsWith("GET /api/color/cyan")) {
    setRGB(0, 201, 204);

    sendCorsHeaders(client);
    client.println("{\"status\":\"ok\",\"color\":\"cyan\"}");
  }

  else if (requestLine.startsWith("GET /api/color/pink")) {
    setRGB(252, 0, 183);

    sendCorsHeaders(client);
    client.println("{\"status\":\"ok\",\"color\":\"pink\"}");
  }

  else if (requestLine.startsWith("GET /api/color/green")) {
    setRGB(1, 255, 0);

    sendCorsHeaders(client);
    client.println("{\"status\":\"ok\",\"color\":\"green\"}");
  }

  else if (requestLine.startsWith("GET /api/off")) {
    setRGB(0, 0, 0);

    sendCorsHeaders(client);
    client.println("{\"status\":\"ok\",\"color\":\"off\"}");
  }

  else if (requestLine.startsWith("GET / ")) {
    sendCorsHeaders(client);
    client.println("{\"status\":\"ok\",\"message\":\"Sippo Arduino RGB API is running\"}");
  }

  else {
    sendNotFound(client);
  }

  client.stop();
  Serial.println("client disconnected");
}