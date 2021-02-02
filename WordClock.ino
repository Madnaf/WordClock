#include <FastLED.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <Time.h>
#include <TimeLib.h>
#include <TimedAction.h>
#include <Timezone.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <EasyNTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <RemoteDebug.h>

#define DEBUG true
#define LED_PIN D1
#define LIGHT_SENSOR_PIN A0
#define NUM_LEDS 110
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

const char* host = "wordclock-ota";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "admin";
const char* filename = "/config.json";
const size_t capacity = JSON_OBJECT_SIZE(2) + 100; // https://arduinojson.org/v6/assistant/

CRGB leds[NUM_LEDS];
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiManager wifiManager;
WiFiUDP ntpUDP;
EasyNTPClient timeClient(ntpUDP, "europe.pool.ntp.org"); //TODO: Konfigurierbar machen?
RemoteDebug Debug;

/*--------------------------------------------------
  Configuration
*/
struct Config
{
  char hexColor[20];
  int minBrightness, maxBrightness;
};
Config config;

/*--------------------------------------------------
  Color
*/
uint32_t decColor;

/*--------------------------------------------------
  Brightness
*/
int light, lightLevel;
// Min/Max LDR
int lightLow = 0, lightHigh = 1024; 
// Min/Max Helligkeit
int ledMin = 0, ledMax = 255;

/*--------------------------------------------------
  Time
*/
time_t localTime;
uint8_t h;
uint8_t m;
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120}; // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};   // Central European Standard Time
Timezone CE(CEST, CET);

/*--------------------------------------------------*/
void setup() {
  delay(3000); //power up safety delay
  Serial.begin(115200);
  if (DEBUG)Debug.println("setup");

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);

  wifiManager.autoConnect("WordClockAP");

  Debug.begin("192.168.1.16");

  if (!SPIFFS.begin()) {
    Debug.println("Failed to mount file system");
    return;
  } else {
    if (DEBUG)Debug.println("SPIFFS successfully mounted");
  }

  // Config File laden
  loadConfig();

  // Registriere Webserver Routen
  server.on("/", handleRoot);
  server.on("/restart", handleRestart);
  server.on("/reset", handleReset);
  server.on("/update", handleUpdate);
  server.on("/color", handleColor);
  server.on("/brightness", handleBrightness);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/dimm", handleDimm);
  server.on("/loadTime", handleLoadTime);
  server.on("/test", handleTest);
  server.on("/test2", handleTest2);
  server.onNotFound(handleNotFound);

  // Setup OTA
  httpUpdater.setup(&server, update_path, update_username, update_password);

  server.begin();
  Debug.println("HTTP server started");

  setupSuccess();
  if (DEBUG)Debug.println("setup done");
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
  if (DEBUG)Debug.println("dimm");
  // Lese LDR
  lightLevel = analogRead(LIGHT_SENSOR_PIN);
  if (DEBUG)Debug.println(lightLevel);
  
  if (lightLevel != NULL) {
    // Mappe LDR Range auf LED Range
    light = map(lightLevel, lightLow, lightHigh, config.minBrightness, config.maxBrightness);
    // Neuen Helligkeits-Wert setzen
    FastLED.setBrightness(light);
    if (DEBUG)Debug.println("dimm done " + String(light));
  } else {
    FastLED.setBrightness(config.maxBrightness);
    if (DEBUG)Debug.println("dimm could not read lightLevel");
  }
}

/*--------------------------------------------------
  Setze die Farbe der LEDs
*/
void setColor() {
  if (DEBUG)Debug.println("setColor");
  //  FastLED.setBrightness(ledOff);
  //Just for fun - lasse alle LEDs einmal in der neuen Farbe faden
  //Experimentell, keine Ahnung ob das tut :D
  //  for (int i = 0; i <= NUM_LEDS; i++) {
  //    leds[i] = decColor;
  //    leds[i].fadeLightBy(255);
  //    leds[i].fadeToBlackBy(0);
  //    delay(200);
  //  }
  //  FastLED.setBrightness(light || userMax);
  if (DEBUG)Debug.println("setColor done");
}

/*--------------------------------------------------
  Lade die Zeit und speichere Stunden und Minuten in
  globalen Variablen ab.
*/
void loadTime() {
  if (DEBUG)Debug.println("loadTime");
  TimeChangeRule *tcr;
  time_t localTime = CE.toLocal(timeClient.getUnixTime(), &tcr);
  h = hour(localTime); //TODO: 0-23 oder 0-11?
  m = minute(localTime);
  if ((h > 0) && (m > 0))
  {
    setTime(localTime);
    calcLedState(h, m); //siehe Unterprogramm WordClockLedController
    if (DEBUG)Debug.println("loadTime done");
  }
}

/*--------------------------------------------------
  Lese die Farbe aus dem Speicher (SPIFFS) und speichere
  sie in globaler Variable ab.
*/
void loadConfig() {
  if (DEBUG)Debug.println("loadConfig");
  File configFile = SPIFFS.open(filename, "r");
  if (!configFile) {
    Debug.println("Failed to open config file");
    return;
  }

  // Create JSON document
  StaticJsonDocument<capacity> jsonDoc;
  // Deserialize file to JSON document
  DeserializationError jsonError = deserializeJson(jsonDoc, configFile);

  if (jsonError) {
    Debug.println("Failed to parse config file");
    Debug.println(jsonError.c_str());
    return;
  }

  // Color
  String hexColor = jsonDoc["color"];
  hexColor.toCharArray(config.hexColor, 20);                   // Hex-Wert
  decColor = (uint32_t)strtol(config.hexColor + 1, NULL, 16);  // Dezimal-Wert
  Debug.println("Color loaded " + String(config.hexColor));

  // Brightness
  config.minBrightness = jsonDoc["minBrightness"];
  Debug.println("minBrightness loaded " + String(config.minBrightness));
  config.maxBrightness = jsonDoc["maxBrightness"];
  Debug.println("maxBrightness loaded " + String(config.maxBrightness));

  configFile.close();
  if (DEBUG)Debug.println("loadConfig done");
}

/*--------------------------------------------------
  Speichere die Farbe im Speicher (SPIFFS) als
  String gemäss Colorpicker
*/
void saveConfig() {
  if (DEBUG)Debug.println("saveConfig");
  File configFile = SPIFFS.open(filename, "w");
  if (!configFile) {
    Debug.println("Failed to open config file for writing");
    return;
  }

  // Create JSON document
  StaticJsonDocument<capacity> jsonDoc;

  // Color
  jsonDoc["color"] = config.hexColor;
  Debug.println("Color saved " + String(config.hexColor));

  // Brightness
  jsonDoc["minBrightness"] = config.minBrightness;
  Debug.println("minBrightness saved " + String(config.minBrightness));
  jsonDoc["maxBrightness"] = config.maxBrightness;
  Debug.println("maxBrightness saved " + String(config.maxBrightness));

  // Serialize JSON to file
  if (serializeJson(jsonDoc, configFile) == 0) {
    Debug.println("Failed to write to config file");
  }

  configFile.close();
  if (DEBUG)Debug.println("saveConfig done");
}

/*--------------------------------------------------
  Handle für die Farbwahl Route des Webservers
*/
void handleColor() {
  if (DEBUG)Debug.println("handleColor");
  String valColor = server.arg("setColor");
  Debug.println(valColor);
  valColor.toCharArray(config.hexColor, 20);
  saveConfig();
  loadConfig();
  loadTime();

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");

  if (DEBUG)Debug.println("handleColor done");
}

/*--------------------------------------------------
  Handle für die Helligkeit Route des Webservers
*/
void handleBrightness() {
  if (DEBUG)Debug.println("handleBrightness");
  String valMinBrightness = server.arg("setMinBrightness");
  Debug.println(valMinBrightness);
  config.minBrightness = valMinBrightness.toInt();
  String valMaxBrightness = server.arg("setMaxBrightness");
  Debug.println(valMaxBrightness);
  config.maxBrightness = valMaxBrightness.toInt();
  saveConfig();
  loadConfig();
  dimm();
  loadTime();

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");

  if (DEBUG)Debug.println("handleBrightness done");
}

/*--------------------------------------------------
  Handle für die Restart Route des Webservers
*/
void handleRestart() {
  if (DEBUG)Debug.println("handleRestart");
  server.send(200, "text/plain", "Restart initiated");
  delay(500);
  ESP.restart();
}

/*--------------------------------------------------
  Handle für die Reset Route des Webservers
*/
void handleReset() {
  if (DEBUG)Debug.println("handleReset");
  server.send(200, "text/plain", "Reset initiated");
  delay(500);
  wifiManager.resetSettings();
  SPIFFS.remove(filename);
  delay(2000);
  ESP.restart();
}

/*--------------------------------------------------
  Handle für die Update Route des Webservers
*/
void handleUpdate() {
  if (DEBUG)Debug.println("handleUpdate");
  server.sendHeader("Location", update_path);
  server.send(302, "text/plane", "");
  if (DEBUG)Debug.println("handleUpdate");
}

/*--------------------------------------------------
  Handle für die On Route des Webservers
*/
void handleOn() {
  if (DEBUG)Debug.println("handleOn");
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  FastLED.setBrightness(30);
  fill_solid(leds, NUM_LEDS, CRGB(decColor));
  FastLED.show();
  if (DEBUG)Debug.println("handleOn done");
}

/*--------------------------------------------------
  Handle für die Off Route des Webservers
*/
void handleOff() {
  if (DEBUG)Debug.println("handleOff");
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  if (DEBUG)Debug.println("handleOff done");
}

/*--------------------------------------------------
  Handle für die Dimm Route des Webservers
*/
void handleDimm() {
  if (DEBUG)Debug.println("handleDimm");
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  dimm();
  if (DEBUG)Debug.println("handleDimm done");
}

/*--------------------------------------------------
  Handle für die loadTime Route des Webservers
*/
void handleLoadTime() {
  if (DEBUG)Debug.println("handleLoadTime");
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  loadTime();
  if (DEBUG)Debug.println("handleLoadTime done");
}

/*--------------------------------------------------
  Handle für die Test Route des Webservers
*/
void handleTest() {
  if (DEBUG)Debug.println("handleTest");

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");

  FastLED.setBrightness(30);
  fill_solid(leds, NUM_LEDS, CRGB(decColor));
  FastLED.show();

  delay(3000);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  if (DEBUG)Debug.println("handleTest done");
}

/*--------------------------------------------------
  Handle für die Test Route 2 des Webservers
*/
void handleTest2() {
  if (DEBUG)Debug.println("handleTest2");

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

  if (DEBUG)Debug.println("handleTest2 done");
}

/*--------------------------------------------------
  Handle für die Root Route des Webservers
*/
void handleRoot() {
  if (DEBUG)Debug.println("handleRoot");
  char html[2048];

  long fh = ESP.getFreeHeap();
  char fhc[20];
  ltoa(fh, fhc, 10);

  long fss = ESP.getFreeSketchSpace();
  char fssc[20];
  ltoa(fss, fssc, 10);

  long ss = ESP.getSketchSize();
  char ssc[20];
  ltoa(ss, ssc, 10);

  snprintf(html, 2048,"\
<html><head>\
 <meta charset='utf-8'/><meta http-equiv='refresh' content='60'/>\
 <link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css'>\
 <title>WordClock</title>\
 <script>function conf(path){if(window.confirm('Sure?')){load(path)}}\
 function load(path){window.location.href=path}</script>\
</head><body>\
 <div class='container'>\
 <div class='jumbotron d-flex align-items-center'><h1>Es ist %d Uhr %d</h1></div>\
  <form action='/color'><div class='form-inline'>\
   <input class='form-control' style='width: 80px;' type='color' name='setColor' value='%s'>\
   <input class='btn btn-success' type='submit' value='Set Color'/>\
  </div></form>\
  <form action='/brightness'><div class='form-inline'>\
   <input class='form-control' style='width: 80px;' type='number' name='setMinBrightness' min='0' max='255' value='%d'>\
   <input class='form-control' style='width: 80px;' type='number' name='setMaxBrightness' min='0' max='255' value='%d'>\
   <input class='btn btn-success' type='submit' value='Set Min/Max Brightness'/>\
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
  <div class='footer-copyright text-center py-3'>© 2019 Copyright <a href='https://www.nafcom.ch'>nafcom.ch</a></div>\
 </footer>\
</body></html>\
  ", h, m, config.hexColor, config.minBrightness, config.maxBrightness, fhc, fssc, ssc, lightLevel, light);

  server.send(200, "text/html", html);
  if (DEBUG)Debug.println("handleRoot done");
}

/*--------------------------------------------------
  Handle für nicht definierte Route
  TODO: Was genau will ich hier?
*/
void handleNotFound() {
  if (DEBUG)Debug.println("handleNotFound");
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
  if (DEBUG)Debug.println("handleNotFound done");
}

/*--------------------------------------------------*/
// Call loadTime every 60 seconds
TimedAction loadTimeAction = TimedAction(60000, loadTime);
// Call dimm every 10 Seconds
TimedAction dimmAction = TimedAction(10000, dimm);

/*--------------------------------------------------*/
void loop() {
  loadTimeAction.check();
  dimmAction.check();
  server.handleClient();
  Debug.handle();
  delay(200);
}
