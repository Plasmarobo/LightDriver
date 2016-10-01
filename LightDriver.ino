#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <LPD8806.h>
#include <SPI.h>
#include <Thread.h>
#include <ThreadController.h>

const char* ssid = "";//SET THIS
const char* password = "";//SET THIS
const char* uuid = "8e973cac-d3af-4dd4-8754-4e98e399549d";

int nLEDs = 160*3;
int sCL = 14;
int mOSI = 13;
LPD8806 strip = LPD8806(nLEDs, mOSI, sCL);
Thread lightControl = Thread();
unsigned long updateRate = 10;
unsigned long pattern_timer = 0;
unsigned char pattern_state = 0;

void updateLights() {
  unsigned char color = 0;
  unsigned char min_color = 10;
  unsigned char max_color = 127;
  unsigned char addend = max_color - min_color;
  float cycle_percent = ((float)(millis() - pattern_timer))/3000.0f;
  if (cycle_percent > 1.0f) {
    cycle_percent = 1.0f;
  }
  switch(pattern_state) {
    case 0:
      color = addend * cycle_percent;
      if (cycle_percent >= 1.0f) {
        pattern_state = 1;
        pattern_timer = millis();
      }
      break;
    case 1:
      color = addend;
      if (cycle_percent >= 0.3f) {
        pattern_state = 2;
        pattern_timer = millis();
      }
      break;
    case 2:
      color = addend * (1.0f - cycle_percent);
      if (cycle_percent >= 1.0f) {
        pattern_state = 3;
        pattern_timer = millis();
      }
      break;
    case 3:
      if (cycle_percent >= 0.1f) {
        pattern_state = 0;
        pattern_timer = millis();
      }
      break;
    default:
      break;
  }
  for(int i = 0; i < nLEDs; ++i) { 
    strip.setPixelColor(i, strip.Color(min_color + color,  0,  0));
  }
  strip.show();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("LightDriver");

  // No authentication by default
  ArduinoOTA.setPassword((const char *)"gravity");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  pattern_timer = millis();
  strip.begin();
  strip.show();

  lightControl.enabled = true;
  lightControl.setInterval(updateRate);
  lightControl.onRun(updateLights);
}

void loop() {
  ArduinoOTA.handle();
  if (lightControl.shouldRun()){
    lightControl.run();
  }
}

