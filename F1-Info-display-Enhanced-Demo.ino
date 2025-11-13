// ============================================================
// F1 TRACKER DEMO MODE - Cycles through all display modes
// ============================================================
// This is a TEMPORARY file for taking photos of all modes
// DO NOT use this in production - use F1_Tracker_3Color.ino instead
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> // v7
#include <time.h>
#include <SPI.h>
#define ENABLE_GxEPD2_display 0
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ctype.h>

// ------------------- WIFI/NETWORK ----------------------
enum screenAlignment { LEFT, RIGHT, CENTER };
bool wifiConnected = false;

// ------------------- DEMO MODE VARIABLES ----------------------
int demoMode = 0;  // 0=Normal, 1=Race Approaching, 2=Race In Progress, 3=Post-Race
const int DEMO_DELAY_MS = 7000;  // 7 seconds between mode changes
unsigned long lastModeChange = 0;

// ------------------- TIME CONSTANTS ----------------------
const long NEXT_RACE_APPROACHING_SEC = 64800;  // 18 hours
const long RACE_IN_PROGRESS_WINDOW_SEC = 14400;  // 4 hours

// ------------------- API ENDPOINTS (PROGMEM) ----------------------
const char API_BASE_TEMPLATE[] PROGMEM = "https://api.jolpi.ca/ergast/f1/%s";
const char API_RACES_SUFFIX[] PROGMEM = "/races.json";
const char API_RESULTS_SUFFIX[] PROGMEM = "/results.json";  
const char API_DRIVER_STAND_SUFFIX[] PROGMEM = "/driverstandings.json";
const char API_CONSTR_STAND_SUFFIX[] PROGMEM = "/constructorstandings.json";

// ------------------- SCREEN / PINS ----------------------
#define SCREEN_WIDTH 296
#define SCREEN_HEIGHT 128
#define listx 225
#define logoWidth 80
#define logoHeight 20

// ------------------- CACHED DRIVER LINES ----------------------
String DRV_LINE[10];
String PTS_LINE[10];
int    DRV_COUNT = 0;

// ---- Layout constants ----
static const int TOP_BAND_H      = 30;
static const int DRIVERS_TITLE_Y = 16;
static const int NEXT_LINE1_Y    = 16;
static const int NEXT_LINE2_Y    = 28;
static const int DRIVERS_ROW_Y0  = 28;
static const int DRIVERS_Y_NUDGE = -4;

// Podium layout constants
static const int PODIUM_LOGO_Y = 50;
static const int PODIUM_1ST_X = 133;
static const int PODIUM_1ST_Y = 86;
static const int PODIUM_1ST_W = 30;
static const int PODIUM_1ST_H = 30;
static const int PODIUM_2ND_X = 104;
static const int PODIUM_2ND_Y = 95;
static const int PODIUM_2ND_W = 30;
static const int PODIUM_2ND_H = 21;
static const int PODIUM_3RD_X = 162;
static const int PODIUM_3RD_Y = 104;
static const int PODIUM_3RD_W = 30;
static const int PODIUM_3RD_H = 12;
static const int PODIUM_TEXT_Y = 118;
static const int PODIUM_TEXT_CENTER_X = 155;
static const int PODIUM_NAME_1ST_Y = 77;
static const int PODIUM_NAME_2ND_X_OFFSET = -38;
static const int PODIUM_NAME_2ND_Y = 86;
static const int PODIUM_NAME_3RD_X_OFFSET = 20;
static const int PODIUM_NAME_3RD_Y = 95;

// --- ESP32 Pin Map ---
static const uint8_t EPDBUSY = 4;
static const uint8_t EPDRST  = 5;
static const uint8_t EPDDC   = 7;
static const uint8_t EPDCS   = 3;
static const uint8_t EPDSCK  = 6;
static const uint8_t EPDMOSI = 10;

GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(
  GxEPD2_290_C90c(EPDCS, EPDDC, EPDRST, EPDBUSY)
);

U8G2_FOR_ADAFRUIT_GFX gfx;

// ------------------- JSON / MEMORY ----------------------
#define API_SCRATCH_DOC_SIZE 12288
StaticJsonDocument<API_SCRATCH_DOC_SIZE> doc;

// ------------------- F1 LOGO ----------------------
const uint8_t F1_Logo[] PROGMEM = {
  0x00,0x00,0x00,0x7f,0xff,0xff,0xff,0xff,0x8f,0xfe,0x00,0x00,0x07,0xff,0xff,0xff,
  0xff,0xff,0x1f,0xfc,0x00,0x00,0x1f,0xff,0xff,0xff,0xff,0xfe,0x3f,0xf8,0x00,0x00,
  0x3f,0xff,0xff,0xff,0xff,0xfc,0x7f,0xf0,0x00,0x00,0x7f,0xff,0xff,0xff,0xff,0xf8,
  0xff,0xe0,0x00,0x01,0xff,0xff,0xff,0xff,0xff,0xf1,0xff,0xc0,0x00,0x03,0xff,0xff,
  0xff,0xff,0xff,0xe3,0xff,0x80,0x00,0x07,0xff,0xe0,0x00,0x00,0x00,0x07,0xff,0x00,
  0x00,0x0f,0xff,0x00,0x00,0x00,0x00,0x0f,0xfe,0x00,0x00,0x1f,0xfc,0x3f,0xff,0xff,
  0xff,0x1f,0xfc,0x00,0x00,0x3f,0xf8,0xff,0xff,0xff,0xfe,0x3f,0xf8,0x00,0x00,0x7f,
  0xf1,0xff,0xff,0xff,0xfc,0x7f,0xf0,0x00,0x00,0xff,0xe3,0xff,0xff,0xff,0xf8,0xff,
  0xe0,0x00,0x01,0xff,0xc7,0xff,0xff,0xff,0xf1,0xff,0xc0,0x00,0x03,0xff,0x8f,0xff,
  0xff,0xff,0xe3,0xff,0x80,0x00,0x07,0xff,0x3f,0xf8,0x00,0x00,0x07,0xff,0x00,0x00,
  0x0f,0xfe,0x7f,0xe0,0x00,0x00,0x0f,0xfe,0x00,0x00,0x1f,0xfc,0xff,0xc0,0x00,0x00,
  0x1f,0xfc,0x00,0x00,0x3f,0xf9,0xff,0x80,0x00,0x00,0x3f,0xf8,0x00,0x00,0x7f,0xf1,
  0xff,0x00,0x00,0x00,0x7f,0xf0,0x00,0x00
};

// ------------------- CALENDAR DATA ----------------------
unsigned lastRound = 0, nextRound = 0;
String lastDate, lastCircuit, lastLoc;
String nextDate, nextCircuit, nextLoc;
String lastGP, nextGP;
String nextTime, lastTime;
time_t nextRaceEpoch = 0;

// ------------------- TIME ----------------------
#define MY_TZ "EST5EDT,M3.2.0/2,M11.1.0/2"
#define NTP1 "pool.ntp.org"
#define NTP2 "time.nist.gov"
struct tm timeinfo;

// ------------------- HELPER FUNCTIONS ----------------------
String buildApiUrl(const char* suffix) {
  struct tm currentTime;
  if (!getLocalTime(&currentTime)) {
    return "";
  }
  int currentYear = 1900 + currentTime.tm_year;
  String seasonStr = String(currentYear);
  
  char baseUrl[80];
  snprintf_P(baseUrl, sizeof(baseUrl), API_BASE_TEMPLATE, seasonStr.c_str());
  
  return String(baseUrl) + String(FPSTR(suffix));
}

String seasonBaseUrl() {
  time_t now = time(nullptr);
  struct tm ti = {};
  localtime_r(&now, &ti);
  int currentYear = ti.tm_year + 1900;

  char baseUrl[96];
  snprintf_P(baseUrl, sizeof(baseUrl), API_BASE_TEMPLATE, String(currentYear).c_str());
  return String(baseUrl);
}

String utf8_substr(const String& s, int codepoints) {
  if (s.length() == 0 || codepoints <= 0) return "";
  
  const char* p = s.c_str();
  if (!p) return "";
  
  String out;
  out.reserve(codepoints * 4);
  int seen = 0;
  
  while (*p && seen < codepoints) {
    uint8_t c = (uint8_t)*p;
    if ((c & 0xC0) == 0x80) { p++; continue; }
    int bytes = 1;
    if ((c & 0xF8) == 0xF0) bytes = 4;
    else if ((c & 0xF0) == 0xE0) bytes = 3;
    else if ((c & 0xE0) == 0xC0) bytes = 2;
    for (int i = 0; i < bytes && *p; i++) out += *p++;
    seen++;
  }
  return out;
}

static const int LINE_H = 12;

int textWidth(const char* s) {
  return gfx.getUTF8Width(s);
}

static time_t timegm_utc(struct tm* tm) {
  int year  = tm->tm_year + 1900;
  int month = tm->tm_mon + 1;
  if (month <= 2) { year -= 1; month += 12; }
  int64_t days = 365LL * year + year/4 - year/100 + year/400
               + (153*(month-3) + 2)/5 + tm->tm_mday - 719469;
  return days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

String cleanTime(String t) {
  int z = t.indexOf('Z'); if (z >= 0) t.remove(z);
  if (t.length() == 0) t = F("00:00:00");
  return t;
}

time_t isoUtcToEpoch(const String& ymd, const String& hms) {
  if (ymd.length() == 0 || hms.length() == 0) return 0;
  
  int y, m, d, hh = 0, mm = 0, ss = 0;
  if (sscanf(ymd.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return 0;
  sscanf(hms.c_str(), "%2d:%2d:%2d", &hh, &mm, &ss);
  
  if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return 0;
  
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
  t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss; t.tm_isdst = 0;
  return timegm_utc(&t);
}

// ------------------- HTTP ----------------------
template <typename TFilterDoc>
bool httpDeserializeWithFilter(Stream& s,
                               StaticJsonDocument<API_SCRATCH_DOC_SIZE>& target,
                               TFilterDoc* filter) {
  if (filter) {
    DeserializationError err =
        deserializeJson(target, s, DeserializationOption::Filter(*filter));
    if (err) {
      Serial.printf("[ERR] JSON parse (filtered) failed: %s\n", err.c_str());
      return false;
    }
  } else {
    DeserializationError err = deserializeJson(target, s);
    if (err) {
      Serial.printf("[ERR] JSON parse failed: %s\n", err.c_str());
      return false;
    }
  }
  return true;
}

template <typename TFilterDoc>
bool httpGetJsonRaw(const char* url, TFilterDoc* filter = nullptr) {
  doc.clear();
  
  Serial.printf("\n--- HTTP Request ---\nAPI: %s\n", url);

  HTTPClient http;
  http.useHTTP10(true);
  http.begin(url);
  http.setConnectTimeout(20000);
  http.setTimeout(25000);
  http.addHeader("User-Agent", "F1Tracker/NanoESP32");

  int httpCode = http.GET();
  Serial.printf("HTTP Status Code: %d\n", httpCode);
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[ERR] HTTP GET failed. Code %d\n", httpCode);
    http.end();
    return false;
  }

  Serial.println(F("[STEP] net: Deserializing"));
  
  bool ok = httpDeserializeWithFilter(http.getStream(), doc, filter);
  http.end();
  if (!ok) return false;

  Serial.println(F("[INFO] JSON success."));
  return true;
}

template <typename TFilterDoc>
bool httpGetJsonWithRetry(const char* url,
                          int tries = 3,
                          int backoff_ms = 500,
                          TFilterDoc* filter = nullptr) {
  for (int i = 0; i < tries; i++) {
    if (httpGetJsonRaw(url, filter)) return true;
    delay(backoff_ms);
    backoff_ms *= 2;
  }
  return false;
}

// ------------------- DRAWING HELPERS ----------------------
void drawStringRED(int x, int y, String str, screenAlignment alignment) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextWrap(false);
  display.getTextBounds(str, x, y, &x1, &y1, &w, &h);
  if (alignment == RIGHT) x -= w;
  if (alignment == CENTER) x -= w / 2;
  gfx.setForegroundColor(GxEPD_RED);
  gfx.setBackgroundColor(GxEPD_WHITE);
  display.setTextColor(GxEPD_RED, GxEPD_WHITE);
  gfx.setCursor(x, y + h);
  gfx.print(str);
}

void drawStringBLACK(int x, int y, String str, screenAlignment alignment) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextWrap(false);
  display.getTextBounds(str, x, y, &x1, &y1, &w, &h);
  if (alignment == RIGHT) x -= w;
  if (alignment == CENTER) x -= w / 2;
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);
  gfx.setCursor(x, y + h);
  display.setTextColor(GxEPD_BLACK, GxEPD_WHITE);
  gfx.print(str);
}

// ------------------- WIFI MANAGER CALLBACK ----------------------
void configModeCallback(WiFiManager* myWiFiManager) {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  
  gfx.setFont(u8g2_font_helvB10_tf);
  drawStringRED(SCREEN_WIDTH/2, 30, F("WiFi Setup Required"), CENTER);
  
  gfx.setFont(u8g2_font_helvB08_tf);
  drawStringBLACK(SCREEN_WIDTH/2, 55, F("1. Connect to this WiFi:"), CENTER);
  
  gfx.setFont(u8g2_font_helvB10_tf);
  drawStringRED(SCREEN_WIDTH/2, 72, F("F1Tracker-Setup"), CENTER);
  
  gfx.setFont(u8g2_font_helvB08_tf);
  drawStringBLACK(SCREEN_WIDTH/2, 87, F("Password: formula1"), CENTER);
  
  drawStringBLACK(SCREEN_WIDTH/2, 105, F("2. Open browser (auto-popup)"), CENTER);
  drawStringBLACK(SCREEN_WIDTH/2, 117, F("3. Select your home WiFi"), CENTER);
  
  display.display(false);
  display.hibernate();
  
  Serial.println(F("[WiFi] Config portal started"));
  Serial.println("SSID: F1Tracker-Setup");
  Serial.println("Password: formula1");
}

// ------------------- HEADER ----------------------
void DrawTime() {
  char datebuf[16], timebuf[16];
  strftime(datebuf, sizeof(datebuf), "%m/%d/%Y", &timeinfo);
  strftime(timebuf, sizeof(timebuf), "%I:%M %p", &timeinfo);
  if (timebuf[0] == '0') memmove(timebuf, timebuf + 1, strlen(timebuf));
  for (char* p = timebuf; *p; ++p) *p = tolower(*p);

  String leftStr  = String("Today: ")   + datebuf;
  String rightStr = String("Updated: ") + timebuf;

  gfx.setFont(u8g2_font_helvB08_tf);

  int wLeft  = gfx.getUTF8Width(leftStr.c_str());
  int wRight = gfx.getUTF8Width(rightStr.c_str());
  const int midX   = SCREEN_WIDTH / 2;
  const int cellW  = midX;
  const int baseY  = 8;

  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);
  gfx.setCursor((cellW - wLeft)/2, baseY);
  gfx.print(leftStr);
  gfx.setCursor(midX + (cellW - wRight)/2, baseY);
  gfx.print(rightStr);

  display.drawLine(midX, 0, midX, 11, GxEPD_RED);
  display.drawLine(midX, -1, midX, 0, GxEPD_RED);
  display.drawLine(0, 11, SCREEN_WIDTH, 11, GxEPD_RED);
}

// ------------------- FETCH CALENDAR ----------------------
void FetchCalendar() {
  lastRound = nextRound = 0;

  StaticJsonDocument<512> filter;
  filter["MRData"]["RaceTable"]["Races"][0]["date"] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["time"] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["round"] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["raceName"] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["Circuit"]["circuitName"] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["Circuit"]["Location"]["locality"] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["Circuit"]["Location"]["country"] = true;

  String url = buildApiUrl(API_RACES_SUFFIX);
  if (!httpGetJsonWithRetry(url.c_str(), 3, 500, &filter)) {
    Serial.println(F("[ERR] races fetch failed."));
    return;
  }

  if (!doc["MRData"]["RaceTable"]["Races"].is<JsonArray>()) {
    Serial.println(F("[ERR] races JSON shape unexpected"));
    return;
  }

  JsonArray races = doc["MRData"]["RaceTable"]["Races"].as<JsonArray>();
  time_t nowEpoch = time(nullptr);

  for (JsonObject race : races) {
    const char* dateStr = race["date"] | "";
    String t = cleanTime(String(race["time"] | "00:00:00"));
    const char* rndStr  = race["round"] | "";
    unsigned rnd = (rndStr[0] == '\0') ? 0 : atoi(rndStr);

    const char* circuit = race["Circuit"]["circuitName"] | "";
    const char* GPname  = race["raceName"] | "";
    String loc;
    loc.reserve(64);
    loc = String(race["Circuit"]["Location"]["locality"].as<const char*>());
    loc += ", ";
    loc += race["Circuit"]["Location"]["country"].as<const char*>();

    if (!dateStr || rnd == 0 || dateStr[0] == '\0') continue;

    struct tm tmRace = {};
    if (sscanf(dateStr, "%4d-%2d-%2d", &tmRace.tm_year, &tmRace.tm_mon, &tmRace.tm_mday) != 3) continue;
    tmRace.tm_year -= 1900; tmRace.tm_mon -= 1;
    int h, m, s; if (sscanf(t.c_str(), "%2d:%2d:%2d", &h, &m, &s) != 3) continue;
    tmRace.tm_hour = h; tmRace.tm_min = m; tmRace.tm_sec = s; tmRace.tm_isdst = 0;

    time_t raceEpoch = timegm_utc(&tmRace);

    if (raceEpoch < nowEpoch) {
      if (rnd > lastRound) {
        lastRound   = rnd;
        lastDate    = String(dateStr);
        lastTime    = t;
        lastGP      = String(GPname); lastGP.replace("Grand Prix","GP");
        lastCircuit = String(circuit);
        lastLoc     = loc;
      }
    } else if (nextRound == 0) {
      nextRound   = rnd;
      nextDate    = String(dateStr);
      nextTime    = t;
      nextGP      = String(GPname); nextGP.replace("Grand Prix","GP");
      nextCircuit = String(circuit);
      nextLoc     = loc;
      nextRaceEpoch = raceEpoch;
      break;
    }
  }

  if (lastRound) Serial.printf("[DATA] Last: R%u %s\n", lastRound, lastGP.c_str());
  if (nextRound) Serial.printf("[DATA] Next: R%u %s\n", nextRound, nextGP.c_str());
}

// ------------------- DRAW NEXT RACE ----------------------
void DrawNextRace(bool forceRaceInProgress = false) {
  if (nextRound == 0) return;

  gfx.setFont(u8g2_font_helvB08_tf);
  const int NEXT_RACE_MAX_WIDTH = 200;

  if (forceRaceInProgress) {
    // Show "Current Race:" instead of "Next:"
    const char* currentLabel = "Current Race:";
    int currentLabelW = textWidth(currentLabel);
    drawStringRED(0, NEXT_LINE1_Y, currentLabel, LEFT);
    display.fillRect(currentLabelW + 4, NEXT_LINE1_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - currentLabelW - 4, LINE_H, GxEPD_WHITE);
    drawStringBLACK(currentLabelW + 4, NEXT_LINE1_Y, nextGP, LEFT);
    
    // Line 2: "Race in Progress!"
    drawStringRED(0, NEXT_LINE2_Y, "Race in Progress!", LEFT);
    return;
  }

  // Line 1: "Next:" + race name
  const char* nextLabel = "Next:";
  int nextLabelW = textWidth(nextLabel);
  drawStringRED(0, NEXT_LINE1_Y, nextLabel, LEFT);
  display.fillRect(nextLabelW + 4, NEXT_LINE1_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - nextLabelW - 4, LINE_H, GxEPD_WHITE);
  drawStringBLACK(nextLabelW + 4, NEXT_LINE1_Y, nextGP, LEFT);

  // Line 2: "Lights Out:" + date/time
  const char* loLabel = "Lights Out:";
  int loW = textWidth(loLabel);
  drawStringRED(0, NEXT_LINE2_Y, loLabel, LEFT);
  
  time_t epoch = nextRaceEpoch;
  if (epoch) {
    struct tm loc = {};
    if (localtime_r(&epoch, &loc)) {
      char buf[32];
      if (strftime(buf, sizeof(buf), "%m/%d %I:%M %p", &loc) > 0) {
        String timeStr = String(buf);
        for (char* p = (char*)timeStr.c_str(); *p; ++p) *p = tolower(*p);
        if (timeStr[0] == '0') timeStr.remove(0, 1);
        display.fillRect(loW + 4, NEXT_LINE2_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - loW - 4, LINE_H, GxEPD_WHITE);
        drawStringBLACK(loW + 4, NEXT_LINE2_Y, timeStr, LEFT);
      }
    }
  }
}

// ------------------- DRAW DRIVERS STANDINGS ----------------------
void DrawDriversStandings() {
  gfx.setFont(u8g2_font_helvB08_tf);

  StaticJsonDocument<512> filter;
  for (int i = 0; i < 10; i++) {
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["position"] = true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["Driver"]["familyName"] = true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["points"] = true;
  }

  String url = buildApiUrl(API_DRIVER_STAND_SUFFIX);
  if (!httpGetJsonWithRetry(url.c_str(), 3, 500, &filter)) {
    Serial.println(F("Failed to fetch driver standings."));
    return;
  }

  if (doc["MRData"]["StandingsTable"]["StandingsLists"].size() == 0 ||
      !doc["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"].is<JsonArray>()) {
    Serial.println(F("Driver standings JSON shape unexpected."));
    return;
  }

  JsonArray ds = doc["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"].as<JsonArray>();

  DRV_COUNT = min(10, (int)ds.size());
  for (int i = 0; i < DRV_COUNT; i++) {
    JsonObject d = ds[i];
    String pos  = d["position"] | "";
    String full = d["Driver"]["familyName"] | "";
    String pts  = d["points"] | "";
    String abbr = utf8_substr(full, 3);

    DRV_LINE[i] = pos + " " + abbr;
    PTS_LINE[i] = " " + pts;
  }

  int baseY = max(DRIVERS_ROW_Y0, TOP_BAND_H + 2) + DRIVERS_Y_NUDGE;
  if (baseY < DRIVERS_ROW_Y0) baseY = DRIVERS_ROW_Y0;

  for (int i = 0; i < DRV_COUNT; i++) {
    int y = baseY + i * 10;
    drawStringBLACK(listx, y, DRV_LINE[i], LEFT);
    drawStringBLACK(296,  y, PTS_LINE[i], RIGHT);
  }
}

// ------------------- DRAW STARTING GRID ----------------------
void DrawStartingGrid(unsigned round) {
  if (round == 0) return;
  gfx.setFont(u8g2_font_helvB08_tf);

  StaticJsonDocument<768> filter;
  for (int i = 0; i < 10; i++) {
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["position"] = true;
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["Driver"]["familyName"] = true;
  }

  String url = seasonBaseUrl() + "/" + String(round) + "/qualifying/";
  if (!httpGetJsonWithRetry(url.c_str(), 2, 400, &filter)) {
    drawStringBLACK(listx, DRIVERS_ROW_Y0, F("No grid data"), LEFT);
    return;
  }

  if (!(doc["MRData"]["RaceTable"]["Races"].size() > 0 &&
        doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].is<JsonArray>())) {
    drawStringBLACK(listx, DRIVERS_ROW_Y0, F("No grid data"), LEFT);
    return;
  }

  JsonArray q = doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].as<JsonArray>();
  int rows = min(10, (int)q.size());

  for (int i = 0; i < rows; i++) {
    JsonObject row = q[i];
    String pos  = row["position"] | "";
    String fam  = row["Driver"]["familyName"] | "";

    String lineLeft;
    lineLeft.reserve(32);
    lineLeft = pos + " " + fam;
    int y = DRIVERS_ROW_Y0 + i * 10;
    drawStringBLACK(listx, y, lineLeft, LEFT);
  }
}

// ------------------- DRAW CONSTRUCTORS ----------------------
void DrawConstructors() {
  StaticJsonDocument<512> filter;
  for (int i = 0; i < 5; i++) {
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["position"] = true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["Constructor"]["name"] = true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["points"] = true;
  }

  String url = buildApiUrl(API_CONSTR_STAND_SUFFIX);
  if (!httpGetJsonWithRetry(url.c_str(), 3, 500, &filter)) {
    Serial.println(F("Failed to fetch constructor standings."));
    return;
  }

  if (doc["MRData"]["StandingsTable"]["StandingsLists"].size() > 0 &&
      doc["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"].is<JsonArray>()) {

    JsonArray cs = doc["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"].as<JsonArray>();

    gfx.setFont(u8g2_font_helvB08_tf);
    drawStringRED(0, 67, F("Top 5 Constructors"), LEFT);

    for (int i = 0; i < 5 && i < cs.size(); i++) {
      JsonObject c = cs[i];
      String pos    = c["position"] | "";
      String constr = c["Constructor"]["name"] | "";
      String pts    = c["points"] | "";

      String nameLine;
      nameLine.reserve(48);
      nameLine = pos + " " + constr;
      String pointsLine;
      pointsLine.reserve(16);
      pointsLine = " " + pts;

      int y = 78 + i * 10;
      drawStringBLACK(0, y, nameLine, LEFT);
      drawStringBLACK(96, y, pointsLine, RIGHT);
    }
  }
}

// ------------------- DRAW LAST RACE (PODIUM) ----------------------
void DrawLastRace(bool showResults, bool showAwaiting = false, bool showEmpty = false) {
  unsigned int targetRound = lastRound;
  
  if (targetRound == 0) {
    display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
    return;
  }
  
  String displayGP = lastGP;
  drawStringBLACK(PODIUM_TEXT_CENTER_X, PODIUM_TEXT_Y, displayGP.c_str(), CENTER);
  
  if (showEmpty) {
    // Empty podium (race in progress)
    display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
    display.drawRect(PODIUM_1ST_X, PODIUM_1ST_Y, PODIUM_1ST_W, PODIUM_1ST_H, GxEPD_BLACK);
    display.drawRect(PODIUM_2ND_X, PODIUM_2ND_Y, PODIUM_2ND_W, PODIUM_2ND_H, GxEPD_BLACK);
    display.drawRect(PODIUM_3RD_X, PODIUM_3RD_Y, PODIUM_3RD_W, PODIUM_3RD_H, GxEPD_BLACK);
    return;
  }
  
  if (showAwaiting) {
    // "Awaiting Results" mode
    display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
    display.drawRect(PODIUM_1ST_X, PODIUM_1ST_Y, PODIUM_1ST_W, PODIUM_1ST_H, GxEPD_BLACK);
    display.drawRect(PODIUM_2ND_X, PODIUM_2ND_Y, PODIUM_2ND_W, PODIUM_2ND_H, GxEPD_BLACK);
    display.drawRect(PODIUM_3RD_X, PODIUM_3RD_Y, PODIUM_3RD_W, PODIUM_3RD_H, GxEPD_BLACK);
    drawStringBLACK(PODIUM_TEXT_CENTER_X, PODIUM_TEXT_Y, "Awaiting Results", CENTER);
    return;
  }
  
  if (showResults) {
    // Fetch and show podium results
    StaticJsonDocument<512> filter;
    filter["MRData"]["RaceTable"]["Races"][0]["Results"][0]["Driver"]["familyName"] = true;
    filter["MRData"]["RaceTable"]["Races"][0]["Results"][1]["Driver"]["familyName"] = true;
    filter["MRData"]["RaceTable"]["Races"][0]["Results"][2]["Driver"]["familyName"] = true;

    String url = seasonBaseUrl() + "/" + String(targetRound) + "/results/";
    if (httpGetJsonWithRetry(url.c_str(), 3, 500, &filter)) {
      if (doc["MRData"]["RaceTable"]["Races"].size() > 0 &&
          doc["MRData"]["RaceTable"]["Races"][0]["Results"].is<JsonArray>()) {

        JsonArray podium = doc["MRData"]["RaceTable"]["Races"][0]["Results"].as<JsonArray>();
        if (podium.size() >= 3) {
          String fam0 = String(podium[0]["Driver"]["familyName"] | "");
          String fam1 = String(podium[1]["Driver"]["familyName"] | "");
          String fam2 = String(podium[2]["Driver"]["familyName"] | "");
          String abbrev0 = utf8_substr(fam0, 3);
          String abbrev1 = utf8_substr(fam1, 3);
          String abbrev2 = utf8_substr(fam2, 3);
          
          display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
          display.drawRect(PODIUM_1ST_X, PODIUM_1ST_Y, PODIUM_1ST_W, PODIUM_1ST_H, GxEPD_BLACK);
          display.drawRect(PODIUM_2ND_X, PODIUM_2ND_Y, PODIUM_2ND_W, PODIUM_2ND_H, GxEPD_BLACK);
          display.drawRect(PODIUM_3RD_X, PODIUM_3RD_Y, PODIUM_3RD_W, PODIUM_3RD_H, GxEPD_BLACK);

          drawStringRED(SCREEN_WIDTH / 2 - 1, PODIUM_NAME_1ST_Y, abbrev0.c_str(), CENTER);
          drawStringRED(SCREEN_WIDTH / 2 + PODIUM_NAME_2ND_X_OFFSET, PODIUM_NAME_2ND_Y, abbrev1.c_str(), LEFT);
          drawStringRED(SCREEN_WIDTH / 2 + PODIUM_NAME_3RD_X_OFFSET, PODIUM_NAME_3RD_Y, abbrev2.c_str(), LEFT);
          return;
        }
      }
    }
  }
  
  // Fallback: just logo
  display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
}

// ------------------- DEMO MODE DRAWING ----------------------
void DrawDemoMode(int mode) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    DrawTime();
    
    switch(mode) {
      case 0: // Normal mode: Driver standings + Constructors + Podium with results
        Serial.println(F("[DEMO] Mode 0: Normal"));
        DrawNextRace(false);
        gfx.setFont(u8g2_font_helvB08_tf);
        drawStringRED(308, DRIVERS_TITLE_Y, F("Top 10 Drivers"), RIGHT);
        DrawDriversStandings();
        DrawConstructors();
        DrawLastRace(true, false, false);
        break;
        
      case 1: // Race approaching: Starting grid + "Awaiting Results"
        Serial.println(F("[DEMO] Mode 1: Race Approaching"));
        DrawNextRace(false);
        gfx.setFont(u8g2_font_helvB08_tf);
        drawStringRED(SCREEN_WIDTH - 5, DRIVERS_TITLE_Y, F("Starting Grid"), RIGHT);
        if (nextRound > 0) {
          DrawStartingGrid(nextRound);
        } else if (lastRound > 0) {
          DrawStartingGrid(lastRound);
        }
        DrawConstructors();
        DrawLastRace(false, true, false);
        break;
        
      case 2: // Race in progress: "Race in Progress!" + Starting grid + Empty podium
        Serial.println(F("[DEMO] Mode 2: Race In Progress"));
        DrawNextRace(true);  // Force "Race in Progress!" mode
        gfx.setFont(u8g2_font_helvB08_tf);
        drawStringRED(SCREEN_WIDTH - 5, DRIVERS_TITLE_Y, F("Starting Grid"), RIGHT);
        if (nextRound > 0) {
          DrawStartingGrid(nextRound);
        } else if (lastRound > 0) {
          DrawStartingGrid(lastRound);
        }
        DrawConstructors();
        DrawLastRace(false, false, true);  // Empty podium
        break;
        
      case 3: // Post-race: Driver standings + Constructors + Podium with results
        Serial.println(F("[DEMO] Mode 3: Post-Race"));
        DrawNextRace(false);
        gfx.setFont(u8g2_font_helvB08_tf);
        drawStringRED(308, DRIVERS_TITLE_Y, F("Top 10 Drivers"), RIGHT);
        DrawDriversStandings();
        DrawConstructors();
        DrawLastRace(true, false, false);  // Show results
        break;
    }
  } while (display.nextPage());
  display.hibernate();
}

// ------------------- SETUP ----------------------
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("\n=== F1 TRACKER DEMO MODE ==="));
  Serial.println(F("Cycles through all display modes every 7 seconds"));

  // Initialize SPI and display
  SPI.begin(EPDSCK, -1, EPDMOSI, EPDCS);
  SPI.setFrequency(4000000);
  pinMode(EPDBUSY, INPUT);
  pinMode(EPDRST, OUTPUT);
  pinMode(EPDDC,  OUTPUT);
  pinMode(EPDCS,  OUTPUT);

  display.init();
  display.setRotation(3);
  gfx.begin(display);
  gfx.setFontMode(0);
  gfx.setFontDirection(0);
  gfx.setBackgroundColor(GxEPD_WHITE);
  gfx.setFont(u8g2_font_helvB10_tf);
  display.fillScreen(GxEPD_WHITE);
  display.setFullWindow();

  // Setup WiFi using WiFiManager
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(300);

  Serial.println(F("[WiFi] Starting WiFi setup..."));
  
  if (!wifiManager.autoConnect("F1Tracker-Setup", "formula1")) {
    Serial.println(F("[WiFi] Failed to connect - restarting..."));
    display.fillScreen(GxEPD_WHITE);
    gfx.setFont(u8g2_font_helvB10_tf);
    drawStringRED(SCREEN_WIDTH/2, 50, F("WiFi Setup Failed"), CENTER);
    drawStringBLACK(SCREEN_WIDTH/2, 75, F("Restarting..."), CENTER);
    display.display(false);
    delay(3000);
    ESP.restart();
  }

  wifiConnected = true;
  Serial.println(F("\n[WiFi] Connected!"));
  Serial.println("IP: " + WiFi.localIP().toString());

  // Configure time
  configTzTime(MY_TZ, NTP1, NTP2);
  while (!getLocalTime(&timeinfo)) { 
    delay(1000); 
    Serial.print("."); 
  }
  Serial.println(F("\n[Time] Synced!"));

  // Show startup message
  display.fillScreen(GxEPD_WHITE);
  display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, 50, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
  drawStringBLACK(40, 84, F("DEMO MODE - Cycling"), LEFT);
  drawStringBLACK(40, 100, F("through all modes..."), LEFT);
  display.display(false);
  display.hibernate();
  delay(2000);

  // Fetch initial data
  Serial.println(F("\n[DEMO] Fetching calendar data..."));
  FetchCalendar();
  
  Serial.println(F("\n[DEMO] Data loaded. Starting mode cycle..."));
  lastModeChange = millis();
}

// ------------------- LOOP ----------------------
void loop() {
  // Update time
  getLocalTime(&timeinfo);
  
  // Check if it's time to change modes
  if (millis() - lastModeChange >= DEMO_DELAY_MS) {
    Serial.printf("\n[DEMO] Switching to mode %d\n", demoMode);
    
    // Draw the current mode
    DrawDemoMode(demoMode);
    
    // Move to next mode
    demoMode = (demoMode + 1) % 4;
    lastModeChange = millis();
  }
  
  delay(100);  // Small delay to prevent tight loop
}

