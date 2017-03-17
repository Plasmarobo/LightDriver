#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <LPD8806.h>
#include <SPI.h>
#include <Thread.h>
#include <ThreadController.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>

#define OTA_TIMEOUT 30000

const String ssid = "";//SET THIS
const String password = "";//SET THIS
const String uuid = "8e973cac-d3af-4dd4-8754-4e98e399549d";
const String bastet_host = "millibyte.io";
const String bastet_path = "/";
const unsigned short bastet_port = 80;

int nLEDs = 32*15;
int sCL = 14;
int mOSI = 13;
LPD8806 strip = LPD8806(nLEDs, mOSI, sCL);
Thread lightControl = Thread();
Thread moodControl = Thread();
unsigned long updateRate = 10;
unsigned long pollRate = 3000;
unsigned long pattern_timer = 0;
unsigned char pattern_state = 0;

WiFiClient wifi;
HttpClient http = HttpClient(wifi, bastet_host, bastet_port);

bool handshake_complete = false;
typedef bool (*PatternFunction)(unsigned long *pattern_timer, unsigned char *pattern_state);

const uint8_t PROGMEM gamma_factors[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,
    3,  3,  3,  3,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  7,  8,
    8,  9,  9, 10, 10, 11, 12, 12, 13, 13, 14, 15, 16, 16, 17, 18,
   19, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 34,
   35, 36, 37, 39, 40, 41, 43, 44, 45, 47, 48, 50, 51, 53, 55, 56,
   58, 60, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 87,
   89, 92, 94, 96, 99,101,103,106,108,111,113,116,119,121,124,127
};


const int n_moods = 28;
const int n_brightness = 8;
int current_mood;
int current_brightness;
unsigned long updated_at = 0;
/* MOODS:
  "sexy",
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
// MOODS */

/* BRIGHTNESSES
  "off",
  "dark",
  "dim",
  "normal",
  "on",
  "bright",
  "high",
  "maximum",
//BRIGHTNESS */

unsigned char brightness_map[n_brightness] = {
  0,
  2,
  10,
  50,
  50,
  90,
  110,
  127
};

PatternFunction mood_table[n_moods];

uint8_t apply_gamma(uint8_t v) {
  return pgm_read_byte(&gamma_factors[v]);
};

void setColor(int pixelNumber, unsigned char r, unsigned char g, unsigned char b) {
  strip.setPixelColor(pixelNumber,
    apply_gamma(r),
    apply_gamma(g),
    apply_gamma(b));
};

void setColor(int pixelNumber, float r, float g, float b) {
  setColor(pixelNumber, 
          (unsigned char)(r * 255), 
          (unsigned char)(g * 255),
          (unsigned char)(b * 255));
};


bool setupConnection() {
  if (!wifi.connected()) {
    handshake_complete = false;
    wifi.connect(bastet_host.c_str(), bastet_port);
  }
  if (wifi.connected()) {
      return true;
  }
  return false;
};

void pollMood() {
  String data;
  if (setupConnection()) {
    StaticJsonBuffer<512> json_buffer;
    JsonObject& root = json_buffer.createObject();
    root["request"] = json_buffer.createObject();
    root["request"]["type"] = "StateSync";
    root["mood"] = current_mood;
    root["brightness"] = current_brightness;
    root["updated_at"] = 0;
    char str[512];
    root.printTo(str, sizeof(str));
    http.post("/", "application/json", str);
    if (http.responseStatusCode() != 200) {
      Serial.println(http.responseBody());
    } else {
      http.readHeader();
      data = http.responseBody();
      data = data.substring(data.indexOf('{'), data.lastIndexOf('}')+1);
      if (data.length() > 0){
        StaticJsonBuffer<512> info_buffer;
        JsonObject& info = info_buffer.parseObject(data);
        current_mood = info["mood"];
        current_brightness = info["brightness"];
        updated_at = info["updated_at"];
        Serial.print("Mood ");
        Serial.print(current_mood);
        Serial.print(" Brightness ");
        Serial.print(current_brightness);
        Serial.print(" TimeStamp ");
        Serial.println(updated_at);
      }
    }
  }
};

void updateLights() {
  if (current_brightness != 0) {
    if(mood_table[current_mood](&pattern_timer, &pattern_state) == true) {
      pollMood();
    }
  } else {
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, strip.Color(0, 0, 0));
    }
    strip.show();
    pollMood();
  }
};

uint32_t WeightColor(float r, float g, float b) {
    return strip.Color(apply_gamma((unsigned char)(r * (float)(brightness_map[current_brightness]))),
                       apply_gamma((unsigned char)(g * (float)(brightness_map[current_brightness]))),
                       apply_gamma((unsigned char)(b * (float)(brightness_map[current_brightness]))));
};

void setup() {
  mood_table[0] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    unsigned char color = 0;
    unsigned char max_color = brightness_map[current_brightness];
    unsigned char min_color = max_color/2;
    unsigned char addend = max_color - min_color;
    float cycle_percent = ((float)(millis() - *pattern_timer))/6000.0f;
    if (cycle_percent > 1.0f) {
      cycle_percent = 1.0f;
    }
    switch(*pattern_state) {
      case 0:
        color = 0;
        if (cycle_percent >= 0.3f) {
          *pattern_state = 1;
          *pattern_timer = millis();
        }
        break;
      case 1:
        color = addend * cycle_percent;
        if (cycle_percent >= 1.0f) {
          *pattern_state = 2;
          *pattern_timer = millis();
        }
        break;
      case 2:
        color = addend;
        if (cycle_percent >= 0.3f) {
          *pattern_state = 3;
          *pattern_timer = millis();
        }
        break;
      case 3:
        color = addend * (1.0f - cycle_percent);
        if (cycle_percent >= 1.0f) {
          *pattern_state = 4;
          *pattern_timer = millis();
        }
        break;
      case 4:
        if (cycle_percent >= 0.1f) {
          *pattern_state = 5;
          *pattern_timer = millis();
          return true;
        }
        break;
      case 5:
        *pattern_state = 0;
        *pattern_timer = millis();
        break;
      default:
        break;
    }
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, strip.Color(min_color + color,  0,  0));
    }
    strip.show();
    return false;
  };
  mood_table[1] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, WeightColor(1.0f, 0.9f, 0.9f));
    }
    strip.show();
    if ((millis() - (*pattern_timer)) > 6000) {
      *pattern_state = 0;
      *pattern_timer = millis();
      return true;
    }
    return false;
  };
  mood_table[2] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    unsigned char r = random(5);  
    if (*pattern_timer % 500 == 0) {
      for(int i = 0; i < nLEDs; ++i) {
        if (r <= 2) {
          strip.setPixelColor(i, WeightColor(0.788f, 0.886f, 1.0f));    
        } else if (r == 3) {
          strip.setPixelColor(i, WeightColor(0.847f, 0.969f, 1.0f));
        } else if (r == 4) {
          strip.setPixelColor(i, WeightColor(0.251f, 0.612f, 1.0f));
        }
      }
      strip.show();
    }
    if(*pattern_timer > 5000) {
      *pattern_timer = 0;
      return true;
    } else {
      return false;
    }
  };
  mood_table[3] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, random(0, brightness_map[current_brightness]), random(0, brightness_map[current_brightness]), random(0, brightness_map[current_brightness]));
    }
    strip.show();
    return true;
  };
  mood_table[4] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, 122, 127, 124);
    }
    strip.show();
    return true;
  };
  mood_table[5] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, WeightColor(0.788f, 0.886f, 1.0f));
    }
    strip.show();
    return true;
  };
  mood_table[6] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, 127, 91, 38);
    }
    strip.show();
    return true;
  };
  mood_table[7] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {
    for(int i = 0; i < nLEDs; ++i) { 
      strip.setPixelColor(i, 121, 127, 124);
    }
    strip.show();
    return true;
  };
  mood_table[8] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[9] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[10] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[11] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[12] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[13] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[14] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[15] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[16] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[17] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[18] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[19] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[20] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[21] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[22] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[23] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[24] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[25] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[26] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  mood_table[27] = [](unsigned long *pattern_timer, unsigned char *pattern_state) {return true;};
  
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
  pattern_timer = millis();
  strip.begin();
  strip.show();

  lightControl.enabled = true;
  lightControl.setInterval(updateRate);
  lightControl.onRun(updateLights);
  current_mood = 1;
  current_brightness = 0;
  setupConnection();
}

void loop() {
  if ((OTA_TIMEOUT > 0) && (millis() < OTA_TIMEOUT)) {
    ArduinoOTA.handle();
  }
  if (lightControl.shouldRun()){
    lightControl.run();
  }
}

