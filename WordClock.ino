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
const size_t capacity = 256; // JSON_OBJECT_SIZE(8); // https://arduinojson.org/v6/assistant/

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
  int lowBrightness, mediumBrightness, highBrightness;
  int lowBoundary, highBoundary;
};
Config config;

/*--------------------------------------------------
  Color
*/
uint32_t decColor;

/*--------------------------------------------------
  Brightness
*/
const int lightLevelSteps = 8;
int lightLevels[lightLevelSteps];

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

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);

  wifiManager.autoConnect("WordClockAP");

  Debug.begin("192.168.1.16");
  
  if (!SPIFFS.begin()) {
    Debug.println("Failed to mount file system");
    return;
  } else {
    Debug.println("SPIFFS successfully mounted");
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
  server.on("/boundaries", handleBoundaries);
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
  setupSuccess();
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
  // copy the value of the following step to the previous
  for (int i = 0; i < lightLevelSteps - 1; i++ ) {
    lightLevels[i] = lightLevels[i+1];
  }
  // overwrite the last one with the sensor
  lightLevels[lightLevelSteps - 1] = analogRead(LIGHT_SENSOR_PIN);

  int lightSum = 0;
  for (int i = 0; i< lightLevelSteps; i++)
  {
    lightSum += lightLevels[i];
  }

  int lightAverage = lightSum / lightLevelSteps;

  int brightness = 0;
  if (lightAverage < 10) {
    brightness = config.lowBrightness;
  } else if (lightAverage > 20) {
    brightness = config.highBrightness;
  } else {
    brightness = config.mediumBrightness;
  }
  FastLED.setBrightness(brightness);
  Debug.println("dimm brightness=" + String(brightness) + " lightAverage=" + String(lightAverage) + " lightSum=" + String(lightSum) + " currentLight=" + String(lightLevels[lightLevelSteps - 1]));
}

/*--------------------------------------------------
  Setze die Farbe der LEDs
*/
void setColor() {
  Debug.println("setColor");
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
  Debug.println("setColor done");
}

/*--------------------------------------------------
  Lade die Zeit und speichere Stunden und Minuten in
  globalen Variablen ab.
*/
void loadTime() {
  Debug.println("loadTime");
  TimeChangeRule *tcr;
  time_t localTime = CE.toLocal(timeClient.getUnixTime(), &tcr);
  uint8_t tempH = hour(localTime);
  uint8_t tempM = minute(localTime);
  if ((tempH > 0) && (tempM > 0))
  {
    setTime(localTime);
  }
}

/*
  Recalc LED state
*/
void recalcLedState() {
  h = hour();
  m = minute();
  calcLedState(h, m);
}

/*--------------------------------------------------
  Lese die Farbe aus dem Speicher (SPIFFS) und speichere
  sie in globaler Variable ab.
*/
void loadConfig() {
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
  config.lowBrightness = jsonDoc["lowBr"];
  Debug.println("lowBrightness loaded " + String(config.lowBrightness));
  config.mediumBrightness = jsonDoc["medBr"];
  Debug.println("mediumBrightness loaded " + String(config.mediumBrightness));
  config.highBrightness = jsonDoc["highBr"];
  Debug.println("highBrightness loaded " + String(config.highBrightness));

  // Boundaries
  config.lowBoundary = jsonDoc["lowBo"];
  Debug.println("lowBoundary loaded " + String(config.lowBoundary));
  config.highBoundary = jsonDoc["highBo"];
  Debug.println("highBoundary loaded " + String(config.highBoundary));

  configFile.close();
}

/*--------------------------------------------------
  Speichere die Farbe im Speicher (SPIFFS) als
  String gemäss Colorpicker
*/
void saveConfig() {
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
  jsonDoc["lowBr"] = config.lowBrightness;
  Debug.println("lowBrightness saved " + String(config.lowBrightness));
  jsonDoc["medBr"] = config.mediumBrightness;
  Debug.println("mediumBrightness saved " + String(config.mediumBrightness));
  jsonDoc["highBr"] = config.highBrightness;
  Debug.println("highBrightness saved " + String(config.highBrightness));

  // Boundaries
  jsonDoc["lowBo"] = config.lowBoundary;
  Debug.println("lowBoundary saved " + String(config.lowBoundary));
  jsonDoc["highBo"] = config.highBoundary;
  Debug.println("highBoundary saved " + String(config.highBoundary));

  // Serialize JSON to file
  if (serializeJson(jsonDoc, configFile) == 0) {
    Debug.println("Failed to write to config file");
  }

  configFile.close();
}

/*--------------------------------------------------
  Handle für die Farbwahl Route des Webservers
*/
void handleColor() {
  String valColor = server.arg("setColor");
  valColor.toCharArray(config.hexColor, 20);
  saveConfig();
  loadConfig();

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
}

/*--------------------------------------------------
  Handle für die Helligkeit Route des Webservers
*/
void handleBrightness() {
  config.lowBrightness = server.arg("setLowBrightness").toInt();
  config.mediumBrightness = server.arg("setMediumBrightness").toInt();
  config.highBrightness = server.arg("setHighBrightness").toInt();
  
  saveConfig();
  loadConfig();
  dimm();

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
}

/*--------------------------------------------------
  Handle for upper and lower brightness boundaries
*/
void handleBoundaries() {
  config.lowBoundary = server.arg("setLowBoundary").toInt();
  config.highBoundary = server.arg("setHighBoundary").toInt();
  
  saveConfig();
  loadConfig();
  dimm();

  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
}

/*--------------------------------------------------
  Handle für die Restart Route des Webservers
*/
void handleRestart() {
  server.send(200, "text/plain", "Restart initiated");
  delay(500);
  ESP.restart();
}

/*--------------------------------------------------
  Handle für die Reset Route des Webservers
*/
void handleReset() {
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
  server.sendHeader("Location", update_path);
  server.send(302, "text/plane", "");
}

/*--------------------------------------------------
  Handle für die On Route des Webservers
*/
void handleOn() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  FastLED.setBrightness(30);
  fill_solid(leds, NUM_LEDS, CRGB(decColor));
  FastLED.show();
}

/*--------------------------------------------------
  Handle für die Off Route des Webservers
*/
void handleOff() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

/*--------------------------------------------------
  Handle für die Dimm Route des Webservers
*/
void handleDimm() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  dimm();
}

/*--------------------------------------------------
  Handle für die loadTime Route des Webservers
*/
void handleLoadTime() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");
  loadTime();
}

/*--------------------------------------------------
  Handle für die Test Route des Webservers
*/
void handleTest() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plane", "");

  FastLED.setBrightness(30);
  fill_solid(leds, NUM_LEDS, CRGB(decColor));
  FastLED.show();

  delay(3000);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

/*--------------------------------------------------
  Handle für die Test Route 2 des Webservers
*/
void handleTest2() {
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
}

/*--------------------------------------------------
  Handle für die Root Route des Webservers
*/
void handleRoot() {
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
 <div class='jumbotron d-flex align-items-center'><h1>Es ist %d Uhr %d (System Time %d:%d:%d)</h1></div>\
  <form action='/color'><div class='form-inline'>\
   <input class='form-control' style='width: 80px;' type='color' name='setColor' value='%s'>\
   <input class='btn btn-success' type='submit' value='Set Color'/>\
  </div></form>\
  <form action='/brightness'><div class='form-inline'>\
   <input class='form-control' style='width: 80px;' type='number' name='setLowBrightness' min='0' max='255' value='%d'>\
   <input class='form-control' style='width: 80px;' type='number' name='setMediumBrightness' min='0' max='255' value='%d'>\
   <input class='form-control' style='width: 80px;' type='number' name='setHighBrightness' min='0' max='255' value='%d'>\
   <input class='btn btn-success' type='submit' value='Set Brightness'/>\
  </div></form>\
  <form action='/boundaries'><div class='form-inline'>\
   <input class='form-control' style='width: 80px;' type='number' name='setLowBoundary' min='0' max='255' value='%d'>\
   <input class='form-control' style='width: 80px;' type='number' name='setHighBoundary' min='0' max='255' value='%d'>\
   <input class='btn btn-success' type='submit' value='Set Boundaries'/>\
  </div></form>\
  <p><button class='btn btn-success confirm' onclick=load('/restart');>Restart</button>\
  <button class='btn btn-warning confirm' onclick=conf('/reset');>Reset</button>\
  <button class='btn btn-danger confirm' onclick=conf('/update');>Update</button></p>\
  <p><button class='btn btn-success confirm' onclick=load('/loadTime');>Load Time</button></p>\
 </div>\
 <footer class='page-footer font-small blue pt-4'>\
  <div class='footer-copyright text-center py-3'>© 2019 Copyright <a href='https://www.nafcom.ch'>nafcom.ch</a></div>\
 </footer>\
</body></html>\
  ", h, m, hour(), minute(), second(), config.hexColor, config.lowBrightness, config.mediumBrightness, config.highBrightness, config.lowBoundary, config.highBoundary);

  server.send(200, "text/html", html);
}

/*--------------------------------------------------
  Handle für nicht definierte Route
  TODO: Was genau will ich hier?
*/
void handleNotFound() {
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
}

/*--------------------------------------------------*/
// Call loadTime every 5 minutes
TimedAction loadTimeAction = TimedAction(300000, loadTime);
// Call dimm every 10 Seconds
TimedAction dimmAction = TimedAction(2000, dimm);
// Call recalc every 5 seconds
TimedAction recalcLedStateAction = TimedAction(5000, recalcLedState);

/*--------------------------------------------------*/
void loop() {
  loadTimeAction.check();
  recalcLedStateAction.check();
  dimmAction.check();
  server.handleClient();
  Debug.handle();
  delay(200);
}
