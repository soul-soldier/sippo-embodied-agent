#include <WiFiNINA.h>
#include "sippo-secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;    // your network password between the " "
int keyIndex = 0;             // your network key Index number (needed only for WEP)
int status = WL_IDLE_STATUS;  //connection status
WiFiServer server(80);        //server socket

WiFiClient client = server.available();

// int ledPin = 2;
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
  client = server.available();

  if (client) {
    printWEB();
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

void printWEB() {

  if (client) {                    // if you get a client,
    Serial.println("new client");  // print a message out the serial port
    String currentLine = "";       // make a String to hold incoming data from the client
    while (client.connected()) {   // loop while the client's connected
      if (client.available()) {    // if there's bytes to read from the client,
        char c = client.read();    // read a byte, then
        Serial.write(c);           // print it out the serial monitor
        if (c == '\n') {           // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {

            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            client.print("<h1>RGB LED Control</h1>");
            client.print("<a href=\"/C1\">Cyan #00C9CC</a><br>");
            client.print("<a href=\"/C2\">Pink #F7788A</a><br>");
            client.print("<a href=\"/C3\">Green #34A853</a><br>");
            client.print("<a href=\"/OFF\">Off</a><br><br>");

            int randomReading = analogRead(A1);
            client.print("Random reading from analog pin: ");
            client.print(randomReading);




            // The HTTP response ends with another blank line:
            client.println();
            // break out of the while loop:
            break;
          } else {  // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        if (currentLine.endsWith("GET /C1")) {
          // color code #00C9CC
          setRGB(0, 201, 204);
        }

        if (currentLine.endsWith("GET /C2")) {
          // color code #F7788A
          setRGB(252, 0, 183);
        }

        if (currentLine.endsWith("GET /C3")) {
          // color code #34A853
          setRGB(1, 255, 0);
        }

        if (currentLine.endsWith("GET /OFF")) {
          setRGB(0, 0, 0);
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }
}