#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <LPD8806.h>
#include <SPI.h>
#include <Thread.h>
#include <ThreadController.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

//MQTT SETUP
#define MQTT_SERVER      "192.168.2.1"
#define MQTT_SERVERPORT  1883                   // use 8883 for SSL

#define OTA_TIMEOUT 30000

const String ssid = "";//SET THIS
const String password = "";//SET THIS

int nLEDs = 32*15;
int sCL = 14;
int mOSI = 13;
LPD8806 strip = LPD8806(nLEDs, mOSI, sCL);

void mqtt_callback(char *topic, uint8_t* payload, uint32_t len);

WiFiClient wifi;
PubSubClient client(MQTT_SERVER, MQTT_SERVERPORT, mqtt_callback, wifi);

const char* MQTT_DATA = "lights/onoff/sub";
const char* MQTT_POWER = "lights/data/pub";

const float PHYSCIAL_MAX_POWER = 127.0f;
float led_power;

uint8_t colorMap(uint8_t color) {
  return (uint8_t) led_power * (((float) color) * (PHYSCIAL_MAX_POWER / 255.0f));
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
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
  if (client.connect("BedroomLights")){
    Serial.print("MQTT Connected!");
    client.subscribe("lights/onoff");
    client.subscribe("lights/data");
  } else {
    Serial.println("MQTT Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
}

void mqtt_callback(char *topic, uint8_t* payload, uint32_t len) {
  if (strcmp(topic, "lights/onoff") == 0) {
    if (len > 0) {
      uint8_t p = atoi((char*)payload);
      if (p > 0) {
        led_power = ((float)p) / 255.0f;
      } else {
        led_power = 0.0f;
        for(uint16_t i = 0; i < nLEDs; ++i) {
          strip.setPixelColor(i, 0, 0, 0);
        }
      }
      strip.show();
    }
  } else if (strcmp(topic, "lights/data") == 0) {
    if (len <= 9) {
      int v = (int) strtol(&(((char*)payload)[1]), NULL, 16);
      Serial.println(v);
      uint8_t r = colorMap((v >> 16) & 0xFF);
      uint8_t g = colorMap((v >> 8) & 0xFF);
      uint8_t b = colorMap(v & 0xFF);
      Serial.printf("%d %d %d\n", r,g,b);
      for(uint16_t i = 0; i < nLEDs; ++i) {
        strip.setPixelColor(i, r, g, b);
      }
    } else {
      //Parse Color instruction struct
      uint8_t start_p = payload[0];
      uint8_t len = payload[1];
      for(uint8_t i = start_p; i < start_p + len; ++i) {
        strip.setPixelColor(i, colorMap(payload[2+i]), colorMap(payload[3+i]), colorMap(payload[4+i]));
      }
    }
    strip.show();
  }
}

void loop() {
  client.loop();
  if ((OTA_TIMEOUT > 0) && (millis() < OTA_TIMEOUT)) {
    ArduinoOTA.handle();
  }
}


