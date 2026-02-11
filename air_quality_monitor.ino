// MQ-6 + MQ-135 + Flame + DHT11 + I2C LCD + Buzzer -> Traffic Light (ESP32)
// MQ-6   AO  -> GPIO 34 (ADC1)
// MQ-135 AO  -> GPIO 35 (ADC1)
// Flame  D0  -> GPIO 14 (digital)
// DHT11  DATA-> GPIO 4  (digital)
// LCD I2C SDA-> GPIO 21
// LCD I2C SCL-> GPIO 22
// Green LED  -> GPIO 25
// Yellow LED -> GPIO 26
// Red LED    -> GPIO 27
// Buzzer     -> GPIO 32

#include <DHT.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// -------- WiFi / ThingSpeak --------
//please enter your own wifi and thingspeak credentials 
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";

const char* THINGSPEAK_WRITE_KEY = "";
const char* THINGSPEAK_URL = "";

const unsigned long TS_PERIOD_MS = 15000;
unsigned long lastTsMs = 0;

// ---------------- Pins ----------------
const int MQ6_PIN   = 34;
const int MQ135_PIN = 35;

const int FLAME_DO_PIN = 14;

const int DHT_PIN  = 4;
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

const int GREEN_PIN  = 25;
const int YELLOW_PIN = 26;
const int RED_PIN    = 27;

const int BUZZER_PIN = 32;

// -------------- LCD ----------------
hd44780_I2Cexp lcd; 

// -------------- ADC settings --------------
const int ADC_MAX = 4095;
const float VREF = 3.3;

// ---------- Thresholds (RAW ADC) ----------
const int MQ6_GREEN_MAX   = 700;
const int MQ6_YELLOW_MAX  = 1200;

const int MQ135_GREEN_MAX  = 650;
const int MQ135_YELLOW_MAX = 900;

// Flame sensor logic 
const bool FLAME_ACTIVE_LOW = false;

// Temperature threshold to force RED 
const float TEMP_RED_C = 50.0f;

// Smoothing settings
const int N = 20;

// Separate smoothing buffers for each sensor
int  buf6[N], buf135[N];
int  idx6 = 0, idx135 = 0;
long sum6 = 0, sum135 = 0;
bool filled6 = false, filled135 = false;

// ---------------- Timers ----------------
unsigned long lastDhtMs  = 0;
unsigned long lastLcdMs  = 0;   
unsigned long lastBeepMs = 0;

// Last sensor values
float lastTempC = NAN;
float lastHum   = NAN;
bool  buzzerState = false;

// ---------------- Helpers ----------------
int readSmoothed(int pin, int *buf, int &idx, long &sum, bool &filled) {
  int v = analogRead(pin);

  sum -= buf[idx];
  buf[idx] = v;
  sum += buf[idx];

  idx = (idx + 1) % N;
  if (idx == 0) filled = true;

  int denom = filled ? N : idx;
  if (denom <= 0) denom = 1;
  return (int)(sum / denom);
}

void setLight(bool r, bool y, bool g) {
  digitalWrite(RED_PIN,    r ? HIGH : LOW);
  digitalWrite(YELLOW_PIN, y ? HIGH : LOW);
  digitalWrite(GREEN_PIN,  g ? HIGH : LOW);
}

// 0=GREEN, 1=YELLOW, 2=RED
int levelFromThresholds(int smooth, int greenMax, int yellowMax) {
  if (smooth <= greenMax) return 0;
  if (smooth <= yellowMax) return 1;
  return 2;
}

const char* levelName(int lvl) {
  if (lvl == 0) return "GREEN";
  if (lvl == 1) return "YELLOW";
  return "RED";
}

char levelChar(int lvl) {
  if (lvl == 0) return 'G';
  if (lvl == 1) return 'Y';
  return 'R';
}

bool flameDetected() {
  int d = digitalRead(FLAME_DO_PIN);
  // optional debug:
  Serial.print("  FlameD0=");
  Serial.print(d);

  if (FLAME_ACTIVE_LOW) return (d == LOW);
  return (d == HIGH);
}

void readDHTIfDue() {
  unsigned long now = millis();
  if (now - lastDhtMs < 2000) return; // DHT11 min ~1-2s
  lastDhtMs = now;

  float h = dht.readHumidity();
  float t = dht.readTemperature(); // Celsius

  if (!isnan(t)) lastTempC = t;
  if (!isnan(h)) lastHum = h;
}

void updateBuzzer(int overall, bool flame, bool highTemp) {
  unsigned long now = millis();

  // Any RED reason -> fast beep
  if (overall == 2 || flame || highTemp) {
    if (now - lastBeepMs >= 200) {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      lastBeepMs = now;
    }
    return;
  }

  // Yellow -> slow beep
  if (overall == 1) {
    if (now - lastBeepMs >= 500) {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      lastBeepMs = now;
    }
    return;
  }

  // Green -> off
  digitalWrite(BUZZER_PIN, LOW);
  buzzerState = false;
}

void formatUptimeMMSS(char* out, size_t n) {
  unsigned long s = millis() / 1000;
  unsigned long mm = (s % 3600) / 60;
  unsigned long ss = s % 60;
  snprintf(out, n, "%02lu:%02lu", mm, ss);
}

const char* causeName(int lvl6, int lvl135, bool flame, bool highTemp, int overall) {
  if (flame) return "FLAME";
  if (highTemp) return "TEMP";

  // otherwise, blame the worse gas sensor
  if (lvl6 > lvl135) return "MQ6";
  if (lvl135 > lvl6) return "MQ135";

  if (overall == 1 || overall == 2) return "GAS";
  return "OK";
}

// LCD behavior:
// - GREEN: show gas + temp/hum + uptime
// - YELLOW/RED: show WARNING + CAUSE
void updateLCDIfDue(
  int s6, int s135,
  int lvl6, int lvl135,
  bool flame,
  bool highTemp,
  int overall
) {
  unsigned long now = millis();
  if (now - lastLcdMs < 500) return;
  lastLcdMs = now;

  // Clear each update
  lcd.clear();

  if (overall == 0) {
    // ----- NORMAL (GREEN) -----
    char line1[17];
    snprintf(line1, sizeof(line1), "MQ6%4d 135%4d", s6, s135);
    lcd.setCursor(0, 0);
    lcd.print(line1);

    int t_int = isnan(lastTempC) ? -99 : (int)(lastTempC + 0.5f);
    int h_int = isnan(lastHum)   ? -1  : (int)(lastHum + 0.5f);

    char mmss[6];
    formatUptimeMMSS(mmss, sizeof(mmss));

    char line2[17];
    snprintf(line2, sizeof(line2), "T%2dC H%2d%% %5s", t_int, h_int, mmss);
    lcd.setCursor(0, 1);
    lcd.print(line2);
    return;
  }

  // ----- WARNING (YELLOW/RED) -----
  lcd.setCursor(0, 0);
  if (overall == 1) lcd.print("!! WARNING YEL !!");
  else             lcd.print("!! WARNING RED !!");

  const char* cause = causeName(lvl6, lvl135, flame, highTemp, overall);
  char line2[17];
  snprintf(line2, sizeof(line2), "CAUSE:%-10s", cause);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed (timeout).");
  }
}
void sendToThingSpeak(
  int raw6, int raw135,
  float tempC, float hum,
  bool flame, int overall
) {
  unsigned long now = millis();
  if (now - lastTsMs < TS_PERIOD_MS) return;
  lastTsMs = now;

  // keep wifi alive
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  // Handle NaNs for ThingSpeak (send empty)
  String tStr = isnan(tempC) ? "" : String(tempC, 1);
  String hStr = isnan(hum)   ? "" : String(hum, 0);

  String url = String(THINGSPEAK_URL) +
    "?api_key=" + THINGSPEAK_WRITE_KEY +
    "&field1=" + String(raw6) +
    "&field2=" + String(raw135) +
    "&field3=" + tStr +
    "&field4=" + hStr +
    "&field5=" + String(flame ? 1 : 0) +
    "&field6=" + String(overall);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();     
  String body = http.getString();
  http.end();

  Serial.print("ThingSpeak HTTP ");
  Serial.print(code);
  Serial.print(" | response: ");
  Serial.println(body);
}


// ---------------- Setup / Loop ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // I2C init for LCD
  Wire.begin(21, 22);
  Wire.setClock(100000);

  int lcdStatus = lcd.begin(16, 2);
  if (lcdStatus) {
    Serial.print("LCD begin failed, status=");
    Serial.println(lcdStatus);
  } else {
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Environmental");
    lcd.setCursor(0, 1);
    lcd.print("Monitor v2.0");
    delay(1200);
    lcd.clear();
  }

  // ADC config
  analogReadResolution(12);
  analogSetPinAttenuation(MQ6_PIN,   ADC_11db);
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);

  // Flame input:
  pinMode(FLAME_DO_PIN, INPUT);

  // LEDs
  pinMode(RED_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // init smoothing buffers
  for (int i = 0; i < N; i++) {
    buf6[i] = 0;
    buf135[i] = 0;
  }

  // DHT11 init
  dht.begin();

  Serial.println("MQ-6 + MQ-135 + Flame + DHT11 + LCD + Buzzer started...");
  connectWiFi();

}

void loop() {
  // Read gas sensors
  int raw6   = analogRead(MQ6_PIN);
  int raw135 = analogRead(MQ135_PIN);

  int smooth6   = readSmoothed(MQ6_PIN,   buf6,   idx6,   sum6,   filled6);
  int smooth135 = readSmoothed(MQ135_PIN, buf135, idx135, sum135, filled135);

  // Flame detection
  bool flame = flameDetected();

  // Read DHT if due
  readDHTIfDue();
  bool highTemp = (!isnan(lastTempC) && lastTempC >= TEMP_RED_C);

  // Gas levels
  int lvl6   = levelFromThresholds(smooth6,   MQ6_GREEN_MAX,   MQ6_YELLOW_MAX);
  int lvl135 = levelFromThresholds(smooth135, MQ135_GREEN_MAX, MQ135_YELLOW_MAX);

  int overall = max(lvl6, lvl135);

  // Overrides
  if (flame)   overall = 2;
  if (highTemp) overall = 2;

  // Set traffic light
  if (overall == 0) setLight(false, false, true);
  else if (overall == 1) setLight(false, true, false);
  else setLight(true, false, false);

  // Buzzer
  updateBuzzer(overall, flame, highTemp);

  // Serial prints
  float v6   = (smooth6   / (float)ADC_MAX) * VREF;
  float v135 = (smooth135 / (float)ADC_MAX) * VREF;

  sendToThingSpeak(raw6, raw135, lastTempC, lastHum, flame, overall);

  Serial.print("MQ6 Raw:");
  Serial.print(raw6);
  Serial.print(" Smooth:");
  Serial.print(smooth6);
  Serial.print(" V:");
  Serial.print(v6, 3);
  Serial.print(" Level:");
  Serial.print(levelName(lvl6));

  Serial.print(" | MQ135 Raw:");
  Serial.print(raw135);
  Serial.print(" Smooth:");
  Serial.print(smooth135);
  Serial.print(" V:");
  Serial.print(v135, 3);
  Serial.print(" Level:");
  Serial.print(levelName(lvl135));

  Serial.print(" | Flame:");
  Serial.print(flame ? "DETECTED" : "none");

  Serial.print(" | Temp:");
  if (isnan(lastTempC)) Serial.print("N/A");
  else { Serial.print(lastTempC, 1); Serial.print("C"); }

  Serial.print(" Hum:");
  if (isnan(lastHum)) Serial.print("N/A");
  else { Serial.print(lastHum, 0); Serial.print("%"); }

  Serial.print(" || OVERALL: ");
  Serial.println(levelName(overall));

  // LCD update
  updateLCDIfDue(smooth6, smooth135, lvl6, lvl135, flame, highTemp, overall);

  delay(300);
}
