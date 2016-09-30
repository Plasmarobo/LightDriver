#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <LPD8806.h>
#include <SPI.h>
#include <Thread.h>
#include <ThreadController.h>

const char* ssid = ;//SET THIS
const char* password = ;//SET THIS

int nLEDs = 266;
int sCL = 14;
int mOSI = 13;
LPD8806 strip = LPD8806(nLEDs, mOSI, sCL);
Thread lightControl = Thread();
int updateRate = 500;

void updateLights() {
  int i = random(0, nLEDs);
  strip.setPixelColor(i, strip.Color(127,  0,  0));
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

