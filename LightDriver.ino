
#include "FastLED.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <SimpleTimer.h>
#include <list>
#include <string>

class MQTTHandler;
class PowerHandler;
class ColorHandler;
class RawHandler;

#define NLEDS 34
//DEVICE API
// lights/<NAME>/power
// lights/<NAME>/color
// lights/<NAME>/raw
// lights/<NAME>/config
/*
 * Implements
 * POWER 0-255 byte
 * COLOR HEX-ENCODED RGB string
 * POWER_RATE ms integer
 * COLOR_RATE ms integer
 */

const char* MQTT_NAME_PREFIX = "PlantLight"; //LIGHTSTRIP XXXXXX
const char* MQTT_POWER = "lights/power";
const char* MQTT_COLOR = "lights/color";
const char* MQTT_DATA = "lights/raw_leds";

char mqtt_name[256]; //Name is 16 chars + null terminator

class MQTTHandler {
  public:
    MQTTHandler(std::string topic) {
      _topic = topic;
      _ms = 0;
    }
    void Parse(std::string topic, std::string payload) {
      if (topic.compare(_topic) == 0)
        this->Handle(payload);
        _ms = millis();
    }
    
    virtual void Handle(std::string payload) {
      Serial.println(payload.c_str());
    }
    
    virtual void Commit() = 0;

    uint32_t UpdatedAt() { return _ms; }
  protected:
    std::string _topic;
    uint32_t _ms;
};

//WATCHDOG SETUP
#define MAX_ERRORS 3
#define DELAY 3000

//WIFI SETUP
const String ssid = "FBI-Fi";//SET THIS
const String password = "B1ytches";//SET THIS
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
#define SCL 14
#define MOSI 13

CRGB leds[NLEDS];

bool led_enable;

class PowerHandler : public MQTTHandler {
  public:
    PowerHandler(std::string topic) : MQTTHandler(topic) {
      _power = 10;
    }
    void Handle(std::string payload) override {
      _power = atoi(payload.c_str()) & 0xFF;
      _ms = millis();
    }
    void Commit() {
      FastLED.setBrightness(_power);
    }
    uint8_t GetPower() {
      return _power;
    }
  protected:
    uint8_t _power;
};

class ColorHandler : public MQTTHandler {
  public:
    ColorHandler(std::string topic) : MQTTHandler(topic) {
      _color = CRGB(255, 0, 0);
    }
    void Handle(std::string payload) override {
      _color = CRGB(strtol(payload.c_str(), NULL, 16));
      _ms = millis();
    }
    void Commit() {
      uint32_t start = millis();
      CRGB start_color = leds[0];
      while((millis() - start) > _ms) {
        uint16_t blend_amount = ((millis() - _ms) << 8) / GetRate();
        if (blend_amount < 256) {
          fill_solid(leds, NLEDS, blend(start_color, GetColor(), blend_amount));
        } else {
          fill_solid(leds, NLEDS, GetColor());
        }
        FastLED.show();
      }
    }
    CRGB GetColor() {return _color;}
  protected:
    CRGB _color;
};

class RawHandler : public MQTTHandler {
  // Handles a stream of color values
  protected:
    unsigned short _ptr;
  public:
    RawHandler(std::string topic) : MQTTHandler(topic) {
      _ptr = 0;
    }
    void Handle(std::string payload) override {
      if (payload == "END") {
        update_leds();
      } else {
        for(unsigned int i = 0; i < payload.size(); i += 6) {
          if (i > NLEDS)
            break;
          leds[_ptr] = CRGB(strtol(payload.substr(i, 6).c_str(), NULL, 16));
        }
      }
    }
    void Commit() {
      FastLED.show();
    }
};

std::list<MQTTHandler*> handlers;
ColorHandler colorState(MQTT_COLOR);
PowerHandler powerState(MQTT_POWER);
RawHandler rawState(MQTT_DATA);

void setup_api() {
  handlers.push(&colorState);
  handlers.push(&powerState);
  handlers.push(&rawState);
}

//MQTT SETUP
#define MQTT_SERVER       "192.168.2.1"
#define MQTT_SERVERPORT   1883                   // use 8883 for SSL
const char MQTT_HOSTNAME[] = "MQTT.lan";

PubSubClient client(MQTT_SERVER, MQTT_SERVERPORT, mqtt_callback, wifi);

bool mqtt_connect() {
  if (client.connected())
  {
    return true;
  }
  if (client.connect("BedroomLights")){
    client.subscribe(MQTT_POWER);
    client.subscribe(MQTT_COLOR);
    client.subscribe(MQTT_POWER_RATE);
    client.subscribe(MQTT_COLOR_RATE);
    return true;
  }
  return false;
}

bool is_topic(const char* t, const char *tt){
  return strcmp(t, tt) == 0; 
}

#define MAX_BUFFER_LEN 256
uint8_t safe_buffer[MAX_BUFFER_LEN+1];

void mqtt_callback(char *topic, char* payload, uint32_t len) {
  Serial.print("MQTT: ");
  Serial.println(topic);
  len = MAX_BUFFER_LEN < len ? MAX_BUFFER_LEN : len;
  std::string payload_buffer(payload, len);
  
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
  if (!wifi_connect()) {
    delay(5000);
    ESP.reset();
  }
  Serial.println("Ok");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Connecting MQTT...");
  while (udp.parsePacket() > 0);
  int res = WiFi.hostByName(MQTT_HOSTNAME, mqtt_ip);
  if (res != 1) {
    mDNSResolver::Resolver resolver(udp);
    resolver.setLocalIP(WiFi.localIP());
    mqtt_ip = resolver.search(MQTT_HOSTNAME);
    if (mqtt_ip == INADDR_NONE) {
      //HARDCODED IP
      oled.println("MQTT:Hardcoded");
      oled.display();
      SERIAL(println("Using hardcoded IP"));
      mqtt_ip.fromString(MQTT_IP);
    }
  }
  while (!mqtt_connect()){
    ++error_count;
    Serial.print("X");
    delay(DELAY);
    if (error_count >= MAX_ERRORS)
      ESP.reset();
  }
  Serial.println("Ok");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Boot");
  EEPROM.begin(512);
  unsigned int id = 0;
  EEPROM.get(0, id);
  if (id == 0) {
    randomSeed(analogRead(0));
    EEPROM.put(random(2147483647));
  }
  sprintf(&(mqtt_name[0]), "%s%04x", MQTT_NAME_PREFIX, id);
  
  LEDS.addLeds<LPD8806, MOSI, SCL, BRG>(leds, NLEDS).setCorrection(TypicalSMD5050);
  connect_all();
  
  setup_ota();
  Serial.println("System Up");
}

void loop() {
  ArduinoOTA.handle();
  client.loop();
  update_leds();
}

