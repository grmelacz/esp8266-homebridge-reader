#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- Configuration ---
const char* ssid = "ssid";
const char* password = "password";

// Use your actual Homebridge IP address here
const String hb_host = "http://homebridge-address:8581"; 
const String hb_username = "hb-user";
const String hb_password = "hb-pass";

const String id_outdoor = "outdoor-unique-id";
const String id_indoor = "indoor-unique-id";

// --- Global Variables ---
String authToken = "";

// Separate timers for API requests and Display swapping
unsigned long lastApiUpdate = 0;
unsigned long lastDisplayToggle = 0;
const long apiInterval = 15000;      // Fetch data every 15 seconds
const long displayInterval = 5000;   // Swap the bottom line every 5 seconds

// Variables to store the latest fetched data
float currentOutdoorTemp = NAN;
float currentIndoorTemp = NAN;
float currentIndoorHum = NAN;
bool showHumidity = false;

// Struct to easily return multiple values from our API function
struct SensorData {
  float temperature;
  float humidity;
};

void setup() {
  Serial.begin(115200);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,20);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
}

String getAuthToken() {
  WiFiClient client;
  HTTPClient http;
  
  String url = hb_host + "/api/auth/login";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{\"username\":\"" + hb_username + "\",\"password\":\"" + hb_password + "\"}";
  int httpCode = http.POST(payload);
  
  String token = "";
  if (httpCode == 201 || httpCode == 200) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, http.getString());
    token = doc["access_token"].as<String>();
  }
  
  http.end();
  return token;
}

// Updated function to extract both temperature and humidity
SensorData getSensorData(String uniqueId) {
  SensorData data = {NAN, NAN}; // Default to NAN if not found
  if (authToken == "") return data;
  
  WiFiClient client;
  HTTPClient http;
  
  String url = hb_host + "/api/accessories/" + uniqueId;
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + authToken);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, http.getString());
    if (!error) {
      // Check if the JSON contains these keys before extracting
      if (doc["values"].containsKey("CurrentTemperature")) {
        data.temperature = doc["values"]["CurrentTemperature"].as<float>();
      }
      if (doc["values"].containsKey("CurrentRelativeHumidity")) {
        data.humidity = doc["values"]["CurrentRelativeHumidity"].as<float>();
      }
    }
  } else if (httpCode == 401) {
    authToken = ""; // Clear token so it re-authenticates
  }
  
  http.end();
  return data;
}

// Updated display function with a toggle flag for the bottom line
void updateDisplay(float tOutdoor, float tIndoor, float hIndoor, bool showHum) {
  display.clearDisplay();
  
  // --- TOP HALF: Outdoor Temperature (Always visible) ---
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("Outdoor Temperature:");
  
  display.setTextSize(2);
  display.setCursor(0,10);
  if (isnan(tOutdoor)) display.print("--.-");
  else display.print(tOutdoor, 1);
  
  display.print(" ");
  display.setTextSize(1);
  display.cp437(true);
  display.write(167); // Degree symbol
  display.setTextSize(2);
  display.print("C");
  
  // --- BOTTOM HALF: Indoor Temperature/Humidity (alternating) ---
  display.setTextSize(1);
  display.setCursor(0, 35);
  
  if (!showHum) {
    // Show Temperature
    display.print("Indoor Temperature:");
    display.setTextSize(2);
    display.setCursor(0, 45);
    if (isnan(tIndoor)) display.print("--.-");
    else display.print(tIndoor, 1);
    
    display.print(" ");
    display.setTextSize(1);
    display.write(167);
    display.setTextSize(2);
    display.print("C");
  } else {
    // Show Humidity
    display.print("Indoor Humidity:");
    display.setTextSize(2);
    display.setCursor(0, 45);
    if (isnan(hIndoor)) display.print("--.-");
    else display.print(hIndoor, 1);
    
    display.print(" %");
  }
  
  display.display(); 
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long currentMillis = millis();

    // 1. Fetch data from API every 15 seconds
    if (currentMillis - lastApiUpdate >= apiInterval || lastApiUpdate == 0) {
      lastApiUpdate = currentMillis;

      if (authToken == "") {
        display.clearDisplay();
        display.setCursor(0,20);
        display.setTextSize(1);
        display.print("Logging in...");
        display.display();
        
        authToken = getAuthToken();
      }
      
      if (authToken != "") {
        SensorData outdoorData = getSensorData(id_outdoor);
        SensorData indoorData = getSensorData(id_indoor);
        
        currentOutdoorTemp = outdoorData.temperature;
        currentIndoorTemp = indoorData.temperature;
        currentIndoorHum = indoorData.humidity;
        
        // Force an immediate display update after fetching new data
        updateDisplay(currentOutdoorTemp, currentIndoorTemp, currentIndoorHum, showHumidity);
      }
    }

    // 2. Toggle the bottom display line every 5 seconds
    if (currentMillis - lastDisplayToggle >= displayInterval && authToken != "") {
      lastDisplayToggle = currentMillis;
      showHumidity = !showHumidity; // Flip the flag
      
      // Update the screen with the new layout
      updateDisplay(currentOutdoorTemp, currentIndoorTemp, currentIndoorHum, showHumidity);
    }
  }
}
