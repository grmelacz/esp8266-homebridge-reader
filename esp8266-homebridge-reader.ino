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

const String hb_host = "http://homebridge-address:8581";
const String hb_username = "hb-user";
const String hb_password = "hb-pass";

const String id_outdoor = "thermometer-unique-id";

// --- Timing ---
const unsigned long apiInterval = 15000UL;                  // 15 s
const unsigned long displayInterval = 5000UL;               // 5 s
const unsigned long bucketInterval = 12UL * 60UL * 1000UL;  // 12 min
const unsigned long blinkInterval = 500UL;                  // 0.5 s on / 0.5 s off

// --- Chart layout ---
const int chartTop = 0;
const int chartBottom = 53;
const int chartHeight = chartBottom - chartTop + 1;

const int lineRightX = SCREEN_WIDTH - 4;
const int liveMarkerX = SCREEN_WIDTH - 2;

// --- History ---
const int MAX_HISTORY_POINTS = 120;
const float MIN_VISIBLE_RANGE = 0.2f;

// --- Global state ---
String authToken = "";

unsigned long lastApiUpdate = 0;
unsigned long lastDisplayToggle = 0;
unsigned long bucketStartMillis = 0;
unsigned long lastStatusLog = 0;

float currentOutdoorTemp = NAN;

// 0 = current temp, 1 = chart
int displayState = 0;

// Finished 12-minute averaged samples ring buffer
float history[MAX_HISTORY_POINTS];
int historyHead = 0;
int historyCount = 0;

// Current unfinished averaging bucket
float bucketSum = 0.0f;
int bucketSamples = 0;

// Render buffer for finished historical samples only
float renderValues[MAX_HISTORY_POINTS];
int renderCount = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

void showMessage(const String& line1, const String& line2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 18);
  display.println(line1);
  if (line2.length() > 0) {
    display.setCursor(0, 32);
    display.println(line2);
  }
  display.display();
}

void logWiFiStatus() {
  Serial.printf("[WiFi] status=%d, connected=%s, localIP=%s, RSSI=%d\n",
                WiFi.status(),
                WiFi.status() == WL_CONNECTED ? "yes" : "no",
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

String getAuthToken() {
  WiFiClient client;
  HTTPClient http;
  String url = hb_host + "/api/auth/login";

  Serial.println();
  Serial.println("[AUTH] Starting login...");
  Serial.printf("[AUTH] URL: %s\n", url.c_str());

  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    Serial.println("[AUTH] http.begin() failed");
    return "";
  }

  http.addHeader("Content-Type", "application/json");
  String payload = "{\"username\":\"" + hb_username + "\",\"password\":\"" + hb_password + "\"}";
  Serial.printf("[AUTH] Payload: %s\n", payload.c_str());

  int httpCode = http.POST(payload);
  Serial.printf("[AUTH] HTTP code: %d\n", httpCode);

  if (httpCode <= 0) {
    Serial.printf("[AUTH] Transport error: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return "";
  }

  String response = http.getString();
  Serial.printf("[AUTH] Response body: %s\n", response.c_str());

  String token = "";
  if (httpCode == 200 || httpCode == 201) {
    StaticJsonDocument<768> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.printf("[AUTH] JSON parse failed: %s\n", error.c_str());
    } else if (doc.containsKey("access_token")) {
      token = doc["access_token"].as<String>();
      Serial.printf("[AUTH] Token received, length=%u\n", token.length());
    } else {
      Serial.println("[AUTH] access_token not found in response");
    }
  } else {
    Serial.printf("[AUTH] Unexpected HTTP status: %d\n", httpCode);
  }

  http.end();
  return token;
}

float getTemperature(const String& uniqueId) {
  float temp = NAN;

  if (authToken == "") {
    Serial.println("[TEMP] No auth token, skipping temperature fetch");
    return temp;
  }

  WiFiClient client;
  HTTPClient http;
  String url = hb_host + "/api/accessories/" + uniqueId;

  Serial.println();
  Serial.println("[TEMP] Fetching temperature...");
  Serial.printf("[TEMP] URL: %s\n", url.c_str());

  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    Serial.println("[TEMP] http.begin() failed");
    return temp;
  }

  http.addHeader("Authorization", "Bearer " + authToken);

  int httpCode = http.GET();
  Serial.printf("[TEMP] HTTP code: %d\n", httpCode);

  if (httpCode <= 0) {
    Serial.printf("[TEMP] Transport error: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return temp;
  }

  String response = http.getString();

  if (httpCode == 200) {
    StaticJsonDocument<1536> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.printf("[TEMP] JSON parse failed: %s\n", error.c_str());
      Serial.printf("[TEMP] Response body: %s\n", response.c_str());
    } else if (doc["values"].containsKey("CurrentTemperature")) {
      temp = doc["values"]["CurrentTemperature"].as<float>();
      Serial.printf("[TEMP] CurrentTemperature = %.2f\n", temp);
    } else {
      Serial.println("[TEMP] CurrentTemperature not found in response");
      Serial.printf("[TEMP] Response body: %s\n", response.c_str());
    }
  } else if (httpCode == 401) {
    Serial.println("[TEMP] Unauthorized, clearing token");
    authToken = "";
    Serial.printf("[TEMP] Response body: %s\n", response.c_str());
  } else {
    Serial.printf("[TEMP] Unexpected HTTP status: %d\n", httpCode);
    Serial.printf("[TEMP] Response body: %s\n", response.c_str());
  }

  http.end();
  return temp;
}

// -----------------------------------------------------------------------------
// History helpers
// -----------------------------------------------------------------------------

void addHistoryPoint(float value) {
  history[historyHead] = value;
  historyHead = (historyHead + 1) % MAX_HISTORY_POINTS;

  if (historyCount < MAX_HISTORY_POINTS) {
    historyCount++;
  }

  Serial.printf("[HIST] Stored average %.2f at slot=%d, historyCount=%d\n",
                value, historyHead == 0 ? MAX_HISTORY_POINTS - 1 : historyHead - 1, historyCount);
}

float getHistoryValue(int logicalIndex) {
  int start = (historyHead - historyCount + MAX_HISTORY_POINTS) % MAX_HISTORY_POINTS;
  int physicalIndex = (start + logicalIndex) % MAX_HISTORY_POINTS;
  return history[physicalIndex];
}

// -----------------------------------------------------------------------------
// Text helpers
// -----------------------------------------------------------------------------

void drawCenteredText(const String& text, int y, int size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

int getTextWidth(const String& text, int size = 1) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return (int)w;
}

// -----------------------------------------------------------------------------
// Render values
// -----------------------------------------------------------------------------

void buildRenderValues() {
  renderCount = 0;
  for (int i = 0; i < historyCount; i++) {
    renderValues[renderCount++] = getHistoryValue(i);
  }
}

bool getRealMinMax(float &minTemp, float &maxTemp) {
  bool hasAny = false;

  if (renderCount > 0) {
    minTemp = renderValues[0];
    maxTemp = renderValues[0];
    hasAny = true;

    for (int i = 1; i < renderCount; i++) {
      if (renderValues[i] < minTemp) minTemp = renderValues[i];
      if (renderValues[i] > maxTemp) maxTemp = renderValues[i];
    }
  }

  if (!isnan(currentOutdoorTemp)) {
    if (!hasAny) {
      minTemp = currentOutdoorTemp;
      maxTemp = currentOutdoorTemp;
      hasAny = true;
    } else {
      if (currentOutdoorTemp < minTemp) minTemp = currentOutdoorTemp;
      if (currentOutdoorTemp > maxTemp) maxTemp = currentOutdoorTemp;
    }
  }

  return hasAny;
}

void getDisplayRange(float realMin, float realMax, float &displayMin, float &displayMax) {
  float realRange = realMax - realMin;

  if (realRange < MIN_VISIBLE_RANGE) {
    float center = (realMin + realMax) * 0.5f;
    displayMin = center - 0.1f;
    displayMax = center + 0.1f;
  } else {
    displayMin = realMin;
    displayMax = realMax;
  }
}

int mapTempToY(float temp, float displayMin, float displayMax) {
  float range = displayMax - displayMin;

  float paddedMin;
  float paddedMax;

  if (range < MIN_VISIBLE_RANGE) {
    float mid = (displayMin + displayMax) * 0.5f;
    paddedMin = mid - 0.5f;
    paddedMax = mid + 0.5f;
  } else {
    float pad = range * 0.10f;
    paddedMin = displayMin - pad;
    paddedMax = displayMax + pad;
  }

  float paddedRange = paddedMax - paddedMin;
  if (paddedRange <= 0.0f) paddedRange = 1.0f;

  float norm = (temp - paddedMin) / paddedRange;
  if (norm < 0.0f) norm = 0.0f;
  if (norm > 1.0f) norm = 1.0f;

  return chartBottom - (int)(norm * (chartHeight - 1));
}

// -----------------------------------------------------------------------------
// Clipped drawing
// -----------------------------------------------------------------------------

void drawPixelClipped(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH) return;
  if (y < chartTop || y > chartBottom) return;
  display.drawPixel(x, y, WHITE);
}

void drawLineClippedVertical(int x0, int y0, int x1, int y1, int yOffset) {
  display.drawLine(x0, y0 + yOffset, x1, y1 + yOffset, WHITE);
}

void drawThickPixel(int x, int y) {
  drawPixelClipped(x, y);
  drawPixelClipped(x, y - 1);
  drawPixelClipped(x, y + 1);
}

void drawBlinkMarker(int x, int y) {
  drawThickPixel(x, y);
  drawThickPixel(x + 1, y);
}

void drawThickLine(int x0, int y0, int x1, int y1) {
  // Because mapped y values are already constrained to chart region,
  // these 3 lines stay visually in-bounds for this layout.
  drawLineClippedVertical(x0, y0, x1, y1, 0);
  drawLineClippedVertical(x0, y0, x1, y1, -1);
  drawLineClippedVertical(x0, y0, x1, y1, 1);

  drawThickPixel(x0, y0);
  drawThickPixel(x1, y1);
}

// -----------------------------------------------------------------------------
// Display
// -----------------------------------------------------------------------------

void updateTempScreen(float value) {
  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("Balkon ");
  display.cp437(true);
  display.write(167);
  display.print("C");

  if (isnan(value)) {
    drawCenteredText("--.-", 25, 4);
    display.display();
    return;
  }

  drawCenteredText(String(value, 1), 25, 4);
  display.display();
}

void updateChartScreen(unsigned long nowMillis) {
  display.clearDisplay();

  buildRenderValues();

  float realMin, realMax;
  if (!getRealMinMax(realMin, realMax)) {
    drawCenteredText("No data", 24, 2);
    display.display();
    return;
  }

  float displayMin, displayMax;
  getDisplayRange(realMin, realMax, displayMin, displayMax);

  // Historical line chart from finished samples only
  if (renderCount == 1) {
    int y = mapTempToY(renderValues[0], displayMin, displayMax);
    drawThickLine(0, y, lineRightX, y);
  } else if (renderCount >= 2) {
    for (int i = 0; i < renderCount - 1; i++) {
      int x0;
      int x1;

      if (historyCount < MAX_HISTORY_POINTS) {
        x0 = ((long)i * lineRightX) / (renderCount - 1);
        x1 = ((long)(i + 1) * lineRightX) / (renderCount - 1);
      } else {
        x0 = ((long)i * lineRightX) / (MAX_HISTORY_POINTS - 1);
        x1 = ((long)(i + 1) * lineRightX) / (MAX_HISTORY_POINTS - 1);
      }

      int y0 = mapTempToY(renderValues[i], displayMin, displayMax);
      int y1 = mapTempToY(renderValues[i + 1], displayMin, displayMax);

      drawThickLine(x0, y0, x1, y1);
    }
  } else if (!isnan(currentOutdoorTemp)) {
    // No finished history yet: baseline at current temperature
    int y = mapTempToY(currentOutdoorTemp, displayMin, displayMax);
    drawThickLine(0, y, lineRightX, y);
  }

  // Current live reading only as blinking marker on far right
  if (!isnan(currentOutdoorTemp)) {
    bool blinkOn = ((nowMillis / blinkInterval) % 2) == 0;
    if (blinkOn) {
      int y = mapTempToY(currentOutdoorTemp, displayMin, displayMax);
      drawBlinkMarker(liveMarkerX, y);
    }
  }

  // Bottom line: truthful min/max
  display.setTextSize(1);

  String minStr = "Min:" + String(realMin, 1);
  String maxStr = "Max:" + String(realMax, 1);

  display.setCursor(0, 56);
  display.print(minStr);

  int maxX = SCREEN_WIDTH - getTextWidth(maxStr, 1);
  if (maxX < 0) maxX = 0;
  display.setCursor(maxX, 56);
  display.print(maxStr);

  display.display();
}

void refreshDisplay(unsigned long nowMillis) {
  if (displayState == 0) {
    updateTempScreen(currentOutdoorTemp);
  } else {
    updateChartScreen(nowMillis);
  }
}

// -----------------------------------------------------------------------------
// Bucket management
// -----------------------------------------------------------------------------

void resetBucket(unsigned long nowMillis) {
  bucketStartMillis = nowMillis;
  bucketSum = 0.0f;
  bucketSamples = 0;
  Serial.printf("[BUCKET] Reset at millis=%lu\n", nowMillis);
}

void addToBucket(float temp, unsigned long nowMillis) {
  if (isnan(temp)) return;

  if (bucketStartMillis == 0) {
    resetBucket(nowMillis);
  }

  bucketSum += temp;
  bucketSamples++;
  Serial.printf("[BUCKET] Added sample %.2f, count=%d, sum=%.2f\n", temp, bucketSamples, bucketSum);
}

void finalizeBucketIfNeeded(unsigned long nowMillis) {
  if (bucketStartMillis == 0) return;
  if (nowMillis - bucketStartMillis < bucketInterval) return;

  if (bucketSamples > 0) {
    float avg = bucketSum / (float)bucketSamples;
    Serial.printf("[BUCKET] Finalizing bucket: avg=%.2f from %d samples\n", avg, bucketSamples);
    addHistoryPoint(avg);
  } else {
    Serial.println("[BUCKET] Finalize called with 0 samples");
  }

  resetBucket(nowMillis);
}

// -----------------------------------------------------------------------------
// Setup / Loop
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("=== Boot ===");

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[OLED] SSD1306 allocation failed");
    for (;;) {}
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.printf("[WiFi] Connecting to SSID: %s\n", ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("[WiFi] Connected.");
  logWiFiStatus();

  refreshDisplay(millis());
}

void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (currentMillis - lastStatusLog >= 5000UL) {
      lastStatusLog = currentMillis;
      Serial.println("[WiFi] Not connected, retrying...");
      logWiFiStatus();
      showMessage("WiFi disconnected");
    }
    return;
  }

  if (currentMillis - lastApiUpdate >= apiInterval || lastApiUpdate == 0) {
    lastApiUpdate = currentMillis;

    if (authToken == "") {
      showMessage("Logging in...");
      authToken = getAuthToken();

      if (authToken == "") {
        Serial.println("[AUTH] Login failed, no token");
        showMessage("Login failed", "See serial log");
      } else {
        Serial.println("[AUTH] Login successful");
      }
    }

    if (authToken != "") {
      float temp = getTemperature(id_outdoor);
      if (!isnan(temp)) {
        currentOutdoorTemp = temp;
        addToBucket(currentOutdoorTemp, currentMillis);
        refreshDisplay(currentMillis);
      } else {
        Serial.println("[TEMP] Temperature fetch failed");
        showMessage("Fetch failed", "See serial log");
      }
    }
  }

  finalizeBucketIfNeeded(currentMillis);

  if (currentMillis - lastDisplayToggle >= displayInterval && authToken != "") {
    lastDisplayToggle = currentMillis;
    displayState = (displayState + 1) % 2;
    refreshDisplay(currentMillis);
  }

  if (displayState == 1 && authToken != "") {
    static unsigned long lastBlinkRefresh = 0;
    if (currentMillis - lastBlinkRefresh >= 150) {
      lastBlinkRefresh = currentMillis;
      refreshDisplay(currentMillis);
    }
  }
}
