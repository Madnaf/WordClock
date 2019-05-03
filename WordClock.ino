#include <FastLED.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <Time.h>
#include <TimeLib.h>
#include <TimedAction.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <EasyNTPClient.h>
#include <ESP8266HTTPUpdateServer.h>

#define DEBUG true
#define LED_PIN D1 //5 //TODO correct Pin please
#define LIGHT_SENSOR_PIN 0 //TODO correct Pin please
#define NUM_LEDS 110
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

const char* host = "wordclock-ota";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "admin";

CRGB leds[NUM_LEDS];
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiManager wifiManager;
WiFiUDP ntpUDP;
EasyNTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 7200); //TODO: Konfigurierbar machen?

char rgbColor[20];
uint32_t hexColor;
//TODO: globale Variable für maximale Helligkeit und konfigurierbar?

long unixTime;
uint8_t h;
uint8_t m;

int light, lightLevel, muchLight = 0, noLight = 1023;

//min and max Werte für Helligkeit
int ledOff = 0, ledMax = 255;
//max Wert gemäss Usersetting
int userMax = 180; //TODO: konfigurierbar über WebGUI?

/*--------------------------------------------------*/
void setup() {
  delay(3000); //power up safety delay
  Serial.begin(115200);
  if (DEBUG)Serial.println("setup");

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);

  wifiManager.autoConnect("WordClockAP");

  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  } else {
    if (DEBUG)Serial.println("SPIFFS successfully mounted");
  }

  loadColor();

  //Registriere Webserver Routen
  server.on("/", handleRoot);
  server.on("/restart", handleRestart);
  server.on("/reset", handleReset);
  server.on("/update", handleUpdate);
  server.on("/color", handleColor);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/dimm", handleDimm);
  server.on("/loadTime", handleLoadTime);
  server.on("/test", handleTest);
  server.on("/test2", handleTest2);
  server.onNotFound(handleNotFound);

  //Setup OTA
  httpUpdater.setup(&server, update_path, update_username, update_password);

  server.begin();
  Serial.println("HTTP server started");

  setupSuccess();
  if (DEBUG)Serial.println("setup done");
}

/*--------------------------------------------------
  Blinke bei erfolgreichem Boot Vorgang
*/
void setupSuccess() {
  FastLED.setBrightness(100);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Red;
    FastLED.show();
    delay(20);
    // Now turn the LED off, then pause
    leds[i] = CRGB::Black;
    FastLED.show();
    delay(20);
  }
}

/*--------------------------------------------------
  Setze die Helligkeit der LEDs basierend auf Phototransistor
*/
void dimm() {
  if (DEBUG)Serial.println("dimm");
  //lese Fotodiode
  lightLevel = analogRead(LIGHT_SENSOR_PIN);

  if (lightLevel != NULL && lightLevel > 20) {
    //Passe den Input Range auf einen Output Range an
    //TODO: Welches ist der Output Range?
    light = map(lightLevel, muchLight, noLight, ledOff, userMax || ledMax);
    //und stelle sicher, dass die Werte nicht ausserhalb der erlaubten sind
    light = constrain(lightLevel, ledOff, ledMax);
    //Dann setze den neuen Wert
    FastLED.setBrightness(light);
    if (DEBUG)Serial.println("dimm done " + String(light));
  } else {
    FastLED.setBrightness(userMax);
    if (DEBUG)Serial.println("dimm could not read lightLevel");
  }
}

/*--------------------------------------------------
  Setze die Farbe der LEDs
*/
void setColor() {
  if (DEBUG)Serial.println("setColor");
  //  FastLED.setBrightness(ledOff);
  //Just for fun - lasse alle LEDs einmal in der neuen Farbe faden
  //Experimentell, keine Ahnung ob das tut :D
  //  for (int i = 0; i <= NUM_LEDS; i++) {
  //    leds[i] = hexColor;
  //    leds[i].fadeLightBy(255);
  //    leds[i].fadeToBlackBy(0);
  //    delay(200);
  //  }
  //  FastLED.setBrightness(light || userMax);
  if (DEBUG)Serial.println("setColor done");
}

/*--------------------------------------------------
  Lade die Zeit und speichere Stunden und Minuten in
  globalen Variablen ab.
*/
void loadTime() {
  if (DEBUG)Serial.println("loadTime");
  unixTime = timeClient.getUnixTime();
  //TODO: Automatische Sommer-/Winterzeit
  //timeClient.setTimeOffset();
  h = hour(unixTime); //TODO: 0-23 oder 0-11?
  m = minute(unixTime);
  setTime(unixTime);
  calcLedState(h, m); //siehe Unterprogramm WordClockLedController
  if (DEBUG)Serial.println("loadTime done");
}

/*--------------------------------------------------
  Lese die Farbe aus dem Speicher (SPIFFS) und speichere
  sie in globaler Variable ab.
*/
void loadColor() {
  if (DEBUG)Serial.println("loadColor");
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonDocument<200> jsonDoc;
  DeserializationError jsonError = deserializeJson(jsonDoc, buf.get());

  if (jsonError) {
    Serial.println("Failed to parse config file");
    return;
  }
  String temp = jsonDoc["rgbColor"];

  temp.toCharArray(rgbColor, 20);
  //übersetze den String vom Colorpicker in hex Wert
  hexColor = (uint32_t)strtol(rgbColor + 1, NULL, 16);

  Serial.println("Color loaded");
  Serial.println(rgbColor);
  if (DEBUG)Serial.println("loadColor done");
}

/*--------------------------------------------------
  Speichere die Farbe im Speicher (SPIFFS) als
  String gemäss Colorpicker
*/
void saveColor(String rgbColor) {
  if (DEBUG)Serial.println("saveColor" + rgbColor);
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["rgbColor"] = rgbColor;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  serializeJson(jsonDoc, configFile);
  if (DEBUG)Serial.println("saveColor done");
}

/*--------------------------------------------------
  Handle für die Farbwahl Route des Webservers
*/
void handleColor() {
  if (DEBUG)Serial.println("handleColor");
  String pick = server.arg("pick");
  Serial.println(pick);
  saveColor(pick);
  loadColor();
  loadTime();

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");

  if (DEBUG)Serial.println("handleColor done");
}

/*--------------------------------------------------
  Handle für die Restart Route des Webservers
*/
void handleRestart() {
  if (DEBUG)Serial.println("handleRestart");
  server.send(200, "text/plain", "Restart initiated");
  delay(500);
  ESP.restart();
}

/*--------------------------------------------------
  Handle für die Reset Route des Webservers
*/
void handleReset() {
  if (DEBUG)Serial.println("handleReset");
  server.send(200, "text/plain", "Reset initiated");
  delay(500);
  wifiManager.resetSettings();
  delay(2000);
  ESP.restart();
}

/*--------------------------------------------------
  Handle für die Update Route des Webservers
*/
void handleUpdate() {
  if (DEBUG)Serial.println("handleUpdate");
  server.sendHeader("Location", update_path);
  server.send(302, "text/plane", "");
  if (DEBUG)Serial.println("handleUpdate");
}

/*--------------------------------------------------
  Handle für die On Route des Webservers
*/
void handleOn() {
  if (DEBUG)Serial.println("handleOn");
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  FastLED.setBrightness(30);
  fill_solid(leds, NUM_LEDS, CRGB(hexColor));
  FastLED.show();
  if (DEBUG)Serial.println("handleOn done");
}

/*--------------------------------------------------
  Handle für die Off Route des Webservers
*/
void handleOff() {
  if (DEBUG)Serial.println("handleOff");
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  if (DEBUG)Serial.println("handleOff done");
}

/*--------------------------------------------------
  Handle für die Dimm Route des Webservers
*/
void handleDimm() {
  if (DEBUG)Serial.println("handleDimm");
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  dimm();
  if (DEBUG)Serial.println("handleDimm done");
}

/*--------------------------------------------------
  Handle für die loadTime Route des Webservers
*/
void handleLoadTime() {
  if (DEBUG)Serial.println("handleLoadTime");
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  loadTime();
  if (DEBUG)Serial.println("handleLoadTime done");
}

/*--------------------------------------------------
  Handle für die Test Route des Webservers
*/
void handleTest() {
  if (DEBUG)Serial.println("handleTest");

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");

  FastLED.setBrightness(30);
  fill_solid(leds, NUM_LEDS, CRGB(hexColor));
  FastLED.show();

  delay(3000);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  if (DEBUG)Serial.println("handleTest done");
}

/*--------------------------------------------------
  Handle für die Test Route 2 des Webservers
*/
void handleTest2() {
  if (DEBUG)Serial.println("handleTest2");

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Red;
  }
  FastLED.show();
  delay(3000);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");

  if (DEBUG)Serial.println("handleTest2 done");
}

/*--------------------------------------------------
  Handle für die Root Route des Webservers
*/
void handleRoot() {
  if (DEBUG)Serial.println("handleRoot");
  char temp[2048];

  long  fh = ESP.getFreeHeap();
  char  fhc[20];
  ltoa(fh, fhc, 10);

  long fss = ESP.getFreeSketchSpace();
  char fssc[20];
  ltoa(fss, fssc, 10);

  long ss = ESP.getSketchSize();
  char ssc[20];
  ltoa(ss, ssc, 10);

  snprintf(temp, 2048,
           "<html><head>\
 <meta charset='utf-8'/><meta http-equiv='refresh' content='60'/>\
 <link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css'>\
 <title>WordClock</title>\
 <script>function conf(path){if(window.confirm('Sure?')){load(path)}}\
         function load(path){window.location.href=path}</script>\
</head><body>\
 <div class='container'>\
  <div class='jumbotron d-flex align-items-center'>\
   <h1>Es ist %d Uhr %d</h1>\
  </div>\
  <form action='/color'><div class='form-inline'>\
    <input class='form-control' type='color' name='pick' value='%s'>\
    <input class='btn btn-success' type='submit' value='Set' />\
   </div></form>\
  <p><button class='btn btn-success confirm' onclick=load('/restart');>Restart</button>\
  <button class='btn btn-warning confirm' onclick=conf('/reset');>Reset</button>\
  <button class='btn btn-danger confirm' onclick=conf('/update');>Update</button></p>\
  <p><button class='btn btn-success confirm' onclick=load('/on');>On</button>\
  <button class='btn btn-success confirm' onclick=load('/off');>Off</button>\
  <button class='btn btn-success confirm' onclick=load('/dimm');>Dimm</button>\
  <button class='btn btn-success confirm' onclick=load('/loadTime');>Load Time</button></p>\
  <p><button class='btn btn-success confirm' onclick=conf('/test');>Test</button>\
  <button class='btn btn-success confirm' onclick=load('/test2');>Test2</button></p>\
  <p class='lead'>Heap %s</p>\
  <p class='lead'>FreeSketchSpace %s</p>\
  <p class='lead'>SketchSize %s</p>\
  <p class='lead'>Light LDR(%d)/LED(%d)</p>\
 </div>\
 <footer class='page-footer font-small blue pt-4'>\
  <div class='footer-copyright text-center py-3'>© 2019 Copyright <a href='https://www.nafcom.ch'> nafcom.ch</a></div>\
 </footer>\
</body></html>",
           h, m, rgbColor, fhc, fssc, ssc, lightLevel, light
          );
  server.send(200, "text/html", temp);
  if (DEBUG)Serial.println("handleRoot done");
}

/*--------------------------------------------------
  Handle für nicht definierte Route
  TODO: Was genau will ich hier?
*/
void handleNotFound() {
  if (DEBUG)Serial.println("handleNotFound");
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  if (DEBUG)Serial.println("handleNotFound done");
}

/*--------------------------------------------------*/
//Call loadTime every 60 seconds
TimedAction loadTimeAction = TimedAction(60000, loadTime);
//Call dimm every 10 Seconds
TimedAction dimmAction = TimedAction(10000, dimm);

/*--------------------------------------------------*/
void loop() {
  loadTimeAction.check();
  dimmAction.check();
  server.handleClient();
  delay(200);
}
