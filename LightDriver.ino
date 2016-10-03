#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <LPD8806.h>
#include <SPI.h>
#include <Thread.h>
#include <ThreadController.h>
#include <WebSocketClient.h>

const char* ssid = "";//SET THIS
const char* password = "";//SET THIS
const char* uuid = "8e973cac-d3af-4dd4-8754-4e98e399549d";
char* websocket_host = "millibyte.io";
char* websocket_path = "/bastet/";
const unsigned short websocket_port = 8001;

int nLEDs = 160*3;
int sCL = 14;
int mOSI = 13;
LPD8806 strip = LPD8806(nLEDs, mOSI, sCL);
Thread lightControl = Thread();
Thread moodControl = Thread();
unsigned long updateRate = 10;
unsigned long pollRate = 2000;
unsigned long pattern_timer = 0;
unsigned char pattern_state = 0;

WiFiClient web;
WebSocketClient webSocket;
bool handshake_complete = false;
typedef void (*PatternFunction)(unsigned long *pattern_timer, unsigned char *pattern_state);
PatternFunction currentMood;
const int n_moods = 28;
char* mood_map[n_moods] = {
  "sex",
  "light",
  "rain",
  "random",
  "happy",
  "sad",
  "angry",
  "win",
  "pumped",
  "bored",
  "pretty",
  "cold",
  "hot",
  "wet",
  "dry",
  "windy",
  "calm",
  "crazy",
  "dark",
  "devious",
  "musical",
  "lazy",
  "sleepy",
  "frustrated",
  "excited",
  "groovy",
  "love",
  "fun",
};
PatternFunction mood_table[n_moods];

void updateLights() {
  currentMood(&pattern_timer, &pattern_state);
}

void pollMood() {
  String data;
  int index;
  if (setupConnection()) {
    webSocket.getData(data);
    index = 0;
    if (data.length() > 0) {
      while(index < n_moods) {
        if (strcmp(mood_map[index], data.c_str()) == 0) {
          currentMood = mood_table[index];
        }
      }
    }
  }
}

bool setupConnection() {
  if (!web.connected()) {
    unsigned char tries = 5;
    while ((!web.connect(websocket_host, 80)) && (tries > 0)) {
      --tries;
      delay(5000);
    }
  }
  if (web.connected()) {
    webSocket.path = websocket_path;
    webSocket.host = websocket_host;
    if (webSocket.handshake(web)) {
      handshake_complete = true;
    }
  }
}

void setup() {
  mood_table[0] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    unsigned char color = 0;
    unsigned char min_color = 10;
    unsigned char max_color = 127;
    unsigned char addend = max_color - min_color;
    float cycle_percent = ((float)(millis() - *pattern_timer))/3000.0f;
    if (cycle_percent > 1.0f) {
      cycle_percent = 1.0f;
    }
    switch(*pattern_state) {
      case 0:
        color = addend * cycle_percent;
        if (cycle_percent >= 1.0f) {
          *pattern_state = 1;
          *pattern_timer = millis();
        }
        break;
      case 1:
        color = addend;
        if (cycle_percent >= 0.3f) {
          *pattern_state = 2;
          *pattern_timer = millis();
        }
        break;
      case 2:
        color = addend * (1.0f - cycle_percent);
        if (cycle_percent >= 1.0f) {
          *pattern_state = 3;
          *pattern_timer = millis();
        }
        break;
      case 3:
        if (cycle_percent >= 0.1f) {
          *pattern_state = 0;
          *pattern_timer = millis();
        }
        break;
      default:
        break;
    }
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, strip.Color(min_color + color,  0,  0));
    }
    strip.show();
  };
  
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
  moodControl.enabled = true;
  moodControl.setInterval(pollRate);
  moodControl.onRun(pollMood);
  currentMood = mood_table[0];
  setupConnection();
}

void loop() {
  ArduinoOTA.handle();
  if (lightControl.shouldRun()){
    lightControl.run();
  }
  if (moodControl.shouldRun()){
    moodControl.run();
  }
}

