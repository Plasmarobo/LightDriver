#include "FastLED.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <SimpleTimer.h>

//WATCHDOG SETUP
SimpleTimer watchdog;
#define MAX_ERRORS 3
#define DELAY 3000

void check_watchdog(uint8_t &errors) {
  if (errors > MAX_ERRORS) {
    Serial.println("!!");
    Serial.println("Watchdog: Too many Errors, rebooting...");
    ESP.restart();
  }
}

void pet_watchdog() {
  uint8_t error_count = 0;
  while (!wifi_connect()) {
    ++error_count;
    check_watchdog(error_count);
    Serial.println("WiFi failure");
    delay(DELAY);
  }
  while (!mqtt_connect()){
    ++error_count;
    check_watchdog(error_count);
    Serial.println("MQTT failure");
    delay(DELAY);
  }
}

//WIFI SETUP
const String ssid = "";//SET THIS
const String password = "";//SET THIS
WiFiClient wifi;

bool wifi_connect() {
  if (wifi.connected()) {
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  if (WiFi.waitForConnectResult() == WL_CONNECTED) {
    return true;
  }
  return false;
}

//LED STRIP SETUP
#define NLEDS 480
#define SCL 14
#define MOSI 13

CRGB leds[NLEDS];

bool led_enable;

void update_leds() {
 FastLED.show();
}

//MQTT SETUP
#define MQTT_SERVER      "192.168.2.1"
#define MQTT_SERVERPORT  1883                   // use 8883 for SSL
const char* MQTT_POWER = "lights/power";
const char* MQTT_COLOR = "lights/color";
const char* MQTT_EFFECT = "lights/effect";
const char* MQTT_DATA = "lights/raw_leds";

PubSubClient client(MQTT_SERVER, MQTT_SERVERPORT, mqtt_callback, wifi);

bool mqtt_connect() {
  if (client.connected())
  {
    return true;
  }
  if (client.connect("BedroomLights")){
    client.subscribe(MQTT_POWER);
    client.subscribe(MQTT_COLOR);
    client.subscribe(MQTT_EFFECT);
    client.subscribe(MQTT_DATA);
    return true;
  }
  return false;
}

bool is_topic(const char* t, const char *tt){
  return strcmp(t, tt) == 0; 
}

void mqtt_callback(char *topic, uint8_t* payload, uint32_t len) {
  Serial.print("MQTT: ");
  Serial.println(topic);
  uint8_t *safe_buffer = new uint8_t[len+1];
  memcpy(safe_buffer, payload, len);
  safe_buffer[len] = '\0';
  if (is_topic(topic, MQTT_POWER)) {
    LEDS.setBrightness(atoi((char*)safe_buffer));
    Serial.println(atoi((char*)safe_buffer));
  } else if (is_topic(topic, MQTT_COLOR)) {
    Serial.println((char*)safe_buffer);
    fill_solid(leds, NLEDS, CRGB(strtol((char*)&safe_buffer[1], NULL, 16)));
  } else if (is_topic(topic, MQTT_EFFECT)) {
    Serial.println((char*)payload);
    Serial.println("Effects not implemented");
  } else if (is_topic(topic, MQTT_DATA)) {
    if (((len - 4) % 3) != 0) {
      Serial.println("Data stream not aligned!");
      delete [] safe_buffer;
      return;
    }
    //Parse Color instruction struct
    uint16_t start_p = payload[0] + (payload[1] << 8);
    uint16_t end_p = payload[2] + (payload[3] << 8);
    if (len < end_p + 4) {
      Serial.println("Data stream too short!");
    }
    for(uint16_t i = start_p; i < end_p; ++i) {
      leds[i] = CRGB(payload[4+i], payload[5+i], payload[6+i]);
    }
  }
  delete [] safe_buffer;
}

//OTA setup
void setup_ota() {
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
}
  
//==ENTRY==
void connect_all() {
  uint8_t error_count = 0;
  Serial.print("Connecting Wifi...");
  while (!wifi_connect()) {
    ++error_count;
    check_watchdog(error_count);
    Serial.print("X");
    delay(DELAY);
  }
  Serial.println("Ok");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Connecting MQTT...");
  while (!mqtt_connect()){
    ++error_count;
    check_watchdog(error_count);
    Serial.print("X");
    delay(DELAY);
  }
  Serial.println("Ok");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Boot");
  
  LEDS.addLeds<LPD8806, MOSI, SCL, GRB>(leds, NLEDS);
  connect_all();
  
  setup_ota();
  Serial.println("System Up");
}

void loop() {
  ArduinoOTA.handle();
  pet_watchdog();
  client.loop();
  update_leds();
}

