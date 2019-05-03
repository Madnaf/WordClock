# WordClock
500x500mm

## Hardware
- Mikrocontroller: LOLIN(WEMOS) D1 mini
- LED-Band: WS2812B

## Software (Arduino IDE)
Download: https://www.arduino.cc/en/Main/Software

### Libraries
- ArduinoJson: https://github.com/bblanchon/ArduinoJson
- EasyNTPClient: https://github.com/aharshac/EasyNTPClient
- FastLED: https://github.com/FastLED/FastLED
- Time: https://github.com/PaulStoffregen/Time
- TimedAction: https://github.com/Glumgad/TimedAction
- WiFiManager: https://github.com/tzapu/WiFiManager

### Board Settings
- Board: LOLIN(WEMOS) D1 mini
- Flash Size: 4M (1M SPIFFS)

## Links
- https://www.arduino.cc/

## Schriftplatte
Das File SchriftplatteFont.svg ist die Grundlage. Hier kann die Schriftart und so weiter gewählt werden. Für die Bearbeitung wurde Inkscape verwendet. Wenn zufrieden, dann Objekt in Pfad umwandeln. Die Punkte am Rand bilden das Raster (16.65mm - LED Abstand des WS2812B ist 33mm) und können für die Ausrichtung verwendet werden (am Schluss natürlich löschen). Die Stege manuell einfügen wie gewünscht. Am Schluss müsste dann etwas wie SchriftplattePath.svg rauskommen.