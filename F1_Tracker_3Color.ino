// Mac 64:E8:33:B8:F6:28

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

// Scheduler Variables - NOW USING WALL CLOCK ALIGNMENT
unsigned long lastCheckMinute = 0;  // Track which minute we last checked

// ------------------- TIME CONSTANTS ----------------------
// Race timing windows (in seconds)
const long RACE_IN_PROGRESS_WINDOW_SEC = 14400;      // 4 hours - race considered "in progress"
const long RACE_WINDOW_BEFORE_SEC = 21600;           // 6 hours - race window before start
const long RACE_WINDOW_AFTER_SEC = 21600;            // 6 hours - race window after start
const long NEXT_RACE_APPROACHING_SEC = 64800;        // 18 hours - show grid when next race approaching
const long SECONDS_PER_DAY = 86400;                 // 1 day in seconds
const long SECONDS_PER_HOUR = 3600;                 // 1 hour in seconds

// Update frequency constants (in minutes)
const int UPDATE_INTERVAL_FAR_MIN = 360;             // >48h before race: every 6 hours
const int UPDATE_INTERVAL_NORMAL_MIN = 120;          // Normal operation: every 2 hours
const int UPDATE_INTERVAL_RACE_WINDOW_MIN = 30;      // Race window: every 30 minutes
const int UPDATE_INTERVAL_GRID_SEARCH_MIN = 30;      // 24h before race: every 30 min until grid found

// Timing thresholds (in seconds)
const long HOURS_48_BEFORE_RACE_SEC = 172800;       // 48 hours = 2 days
const long HOURS_24_BEFORE_RACE_SEC = 86400;         // 24 hours = 1 day

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
// Optimized: Reduced from 16KB to 12KB (adequate for F1 API responses)
// Saves 4KB RAM with no functionality loss
#define API_SCRATCH_DOC_SIZE 12288
StaticJsonDocument<API_SCRATCH_DOC_SIZE> doc;

// ------------------- API CACHE ----------------------
struct CacheEntry {
  String data;
  unsigned long timestamp;
  bool valid;
};

struct {
  CacheEntry calendar;
  CacheEntry driverStandings;
  CacheEntry constructorStandings;
  CacheEntry qualifying;
  CacheEntry results;  // Race results cache
} apiCache;

// Quick availability cache (per round, lightweight)
struct AvailabilityCache {
  unsigned int round;
  bool qualifyingAvailable;
  bool resultsAvailable;
  unsigned long qualifyingTimestamp;  // Separate timestamp for qualifying
  unsigned long resultsTimestamp;     // Separate timestamp for results
  bool qualifyingValid;
  bool resultsValid;
};
AvailabilityCache availabilityCache = {0, false, false, 0, 0, false, false};
const unsigned long QUALIFYING_AVAILABILITY_TTL = 43200000UL;  // 12 hours - qualifying never changes once posted
const unsigned long RESULTS_AVAILABILITY_TTL = 300000UL;        // 5 minutes - results can appear after race

// Cache TTL (Time To Live) in milliseconds
const unsigned long CACHE_TTL_CALENDAR = 3600000UL;      // 1 hour - calendar rarely changes
const unsigned long CACHE_TTL_STANDINGS = 3600000UL;     // 1 hour - standings only change after races
const unsigned long CACHE_TTL_QUALIFYING = 86400000UL;   // 24 hours - qualifying never changes once posted

void clearCache() {
  apiCache.calendar.valid = false;
  apiCache.driverStandings.valid = false;
  apiCache.constructorStandings.valid = false;
  apiCache.qualifying.valid = false;
  apiCache.results.valid = false;
  availabilityCache.qualifyingValid = false;
  availabilityCache.resultsValid = false;
  Serial.println(F("[Cache] Cleared all cache"));
}

void invalidateStandingsCache() {
  // Call this when race finishes to refresh standings
  apiCache.driverStandings.valid = false;
  apiCache.constructorStandings.valid = false;
  Serial.println(F("[Cache] Invalidated standings cache"));
}

bool getCachedData(CacheEntry* entry, unsigned long ttl) {
  if (!entry->valid) return false;
  if (millis() - entry->timestamp > ttl) {
    entry->valid = false;
    return false;
  }
  Serial.println(F("[Cache] HIT"));
  return true;
}

void cacheData(CacheEntry* entry, const String& data) {
  entry->data = data;
  entry->timestamp = millis();
  entry->valid = true;
  Serial.println(F("[Cache] Stored"));
}

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

// ------------------- TIME ----------------------
#define MY_TZ "EST5EDT,M3.2.0/2,M11.1.0/2"
#define NTP1 "pool.ntp.org"
#define NTP2 "time.nist.gov"

struct tm timeinfo;

// ------------------- PERSISTED STATE ----------------------
Preferences prefs;
unsigned processedRound = 0;
time_t   nextRaceEpoch   = 0;
String   nextRaceDateStr;


String buildApiUrl(const char* suffix) {
  struct tm currentTime;
  if (!getLocalTime(&currentTime)) {
    return ""; // fallback if time not available
  }
  int currentYear = 1900 + currentTime.tm_year;
  String seasonStr = String(currentYear);
  
  char baseUrl[80];
  snprintf_P(baseUrl, sizeof(baseUrl), API_BASE_TEMPLATE, seasonStr.c_str());
  
  return String(baseUrl) + String(FPSTR(suffix));
}

// Build the season base URL, independent of global timeinfo
String seasonBaseUrl() {
  time_t now = time(nullptr);
  struct tm ti = {};
  localtime_r(&now, &ti);
  int currentYear = ti.tm_year + 1900;

  char baseUrl[96];
  snprintf_P(baseUrl, sizeof(baseUrl), API_BASE_TEMPLATE, String(currentYear).c_str());
  return String(baseUrl);
}

// ------------------- WIFI CONNECTION -------------------
bool setup_wifi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(300); // 5 minute timeout
  
  Serial.println(F("Attempting to connect to WiFi..."));
  
  if (wm.autoConnect("F1Tracker-Setup", "formula1")) { 
    Serial.println(F("WiFi connected successfully!"));
    Serial.println("IP: " + WiFi.localIP().toString());
    wifiConnected = true;
    configTime(0, 0, NTP1, NTP2);
    setenv("TZ", MY_TZ, 1);
    tzset();
    return true;
  }
  
  Serial.println(F("Failed to connect to WiFi."));
  wifiConnected = false;
  return false;
}

// ------------------- HELPERS -------------------

// Safer UTF-8: skip stray continuation bytes & cut by codepoints
String utf8_substr(const String& s, int codepoints) {
  if (s.length() == 0 || codepoints <= 0) return "";
  
  const char* p = s.c_str();
  if (!p) return "";
  
  String out;
  out.reserve(codepoints * 4);  // Reserve space for worst-case UTF-8 (4 bytes per char)
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

void clearLineArea(int x, int y, int h = LINE_H) {
  display.fillRect(x, y, SCREEN_WIDTH - x, h, GxEPD_WHITE);
}

int textWidth(const char* s) {
  return gfx.getUTF8Width(s);
}

// Forward declarations
bool qualifyingAvailableForRound(unsigned round);
bool resultsAvailableForLastRound();
void DrawStartingGrid(unsigned round);
void DrawDriversStandings();
void ensureWiFiConnected();
void disconnectWiFiIfIdle();

// Local date as MM/DD
String nextRaceLocalDateMMDD() {
  time_t epoch = nextRaceEpoch ? nextRaceEpoch : isoUtcToEpoch(nextDate, nextTime);
  if (!epoch) return "";
  struct tm loc = {};
  if (!localtime_r(&epoch, &loc)) return "";
  char buf[16];
  if (strftime(buf, sizeof(buf), "%m/%d", &loc) == 0) return "";
  return String(buf);
}

String nextRaceLocalTimeLower() {
    if (nextRound == 0 || nextRaceEpoch == 0) return "";

    struct tm loc = {};
    if (!localtime_r(&nextRaceEpoch, &loc)) {
        return "";
    }

    char temp_buf[32];
    if (strftime(temp_buf, sizeof(temp_buf), "%I:%M %p", &loc) == 0) {
        return "";
    }

    char* src = temp_buf;
    if (temp_buf[0] == '0') {
        src++; 
    }

    String result;
    result.reserve(16);  // Pre-allocate to reduce fragmentation
    
    while (*src != '\0' && result.length() < 15) {
        char c = tolower(*src);
        
        if (isdigit(c) || c == ':' || c == 'a' || c == 'p' || c == 'm' || c == ' ') {
            result += c;
        }
        src++;
    }
    
    result.trim();  // Remove trailing spaces
    
    return result;
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
  
  // Validate date/time ranges
  if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return 0;
  
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
  t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss; t.tm_isdst = 0;
  return timegm_utc(&t);
}

// ------------------- RACE STATE DETECTION -------------------
bool isRaceInProgress() {
  if (nextRaceEpoch == 0) return false;
  
  time_t now = time(nullptr);
  long diffToRaceStart = nextRaceEpoch - now;
  
  // Race is "in progress" from start time until +4 hours
  // OR if we're past race start but API hasn't updated yet (lastRound hasn't incremented)
  if (diffToRaceStart <= 0 && diffToRaceStart >= -RACE_IN_PROGRESS_WINDOW_SEC) {
    return true;
  }
  
  return false;
}

bool isInRaceWindow() {
  // Race window = 6 hours before race start to 6 hours after
  if (nextRaceEpoch == 0) return false;
  
  time_t now = time(nullptr);
  long diff = nextRaceEpoch - now;
  
  return (diff <= RACE_WINDOW_BEFORE_SEC && diff >= -RACE_WINDOW_AFTER_SEC);
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
  // Ensure WiFi is connected before making API call
  ensureWiFiConnected();
  
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

// ------------------- CACHED API FUNCTIONS ----------------------
template <typename TFilterDoc>
bool fetchCalendarWithCache(TFilterDoc* filter = nullptr) {
  String url = buildApiUrl(API_RACES_SUFFIX);
  
  // Check cache first
  if (getCachedData(&apiCache.calendar, CACHE_TTL_CALENDAR)) {
    DeserializationError err;
    if (filter) {
      err = deserializeJson(doc, apiCache.calendar.data, DeserializationOption::Filter(*filter));
    } else {
      err = deserializeJson(doc, apiCache.calendar.data);
    }
    if (!err) return true;
    // If deserialization failed, fall through to fetch fresh
  }
  
  // Fetch fresh data
  Serial.print(F("[Cache] MISS - Fetching: "));
  if (!httpGetJsonWithRetry(url.c_str(), 3, 500, filter)) return false;
  
  // Cache the raw JSON for next time
  String jsonStr;
  serializeJson(doc, jsonStr);
  cacheData(&apiCache.calendar, jsonStr);
  
  return true;
}

template <typename TFilterDoc>
bool fetchDriverStandingsWithCache(TFilterDoc* filter = nullptr) {
  String url = buildApiUrl(API_DRIVER_STAND_SUFFIX);
  
  if (getCachedData(&apiCache.driverStandings, CACHE_TTL_STANDINGS)) {
    DeserializationError err;
    if (filter) {
      err = deserializeJson(doc, apiCache.driverStandings.data, DeserializationOption::Filter(*filter));
    } else {
      err = deserializeJson(doc, apiCache.driverStandings.data);
    }
    if (!err) return true;
  }
  
  Serial.print(F("[Cache] MISS - Fetching: "));
  if (!httpGetJsonWithRetry(url.c_str(), 3, 500, filter)) return false;
  
  String jsonStr;
  serializeJson(doc, jsonStr);
  cacheData(&apiCache.driverStandings, jsonStr);
  
  return true;
}

template <typename TFilterDoc>
bool fetchConstructorStandingsWithCache(TFilterDoc* filter = nullptr) {
  String url = buildApiUrl(API_CONSTR_STAND_SUFFIX);
  
  if (getCachedData(&apiCache.constructorStandings, CACHE_TTL_STANDINGS)) {
    DeserializationError err;
    if (filter) {
      err = deserializeJson(doc, apiCache.constructorStandings.data, DeserializationOption::Filter(*filter));
    } else {
      err = deserializeJson(doc, apiCache.constructorStandings.data);
    }
    if (!err) return true;
  }
  
  Serial.print(F("[Cache] MISS - Fetching: "));
  if (!httpGetJsonWithRetry(url.c_str(), 3, 500, filter)) return false;
  
  String jsonStr;
  serializeJson(doc, jsonStr);
  cacheData(&apiCache.constructorStandings, jsonStr);
  
  return true;
}

template <typename TFilterDoc>
bool fetchQualifyingWithCache(unsigned int round, TFilterDoc* filter = nullptr) {
  String url = seasonBaseUrl() + "/" + String(round) + "/qualifying.json";
  
  if (getCachedData(&apiCache.qualifying, CACHE_TTL_QUALIFYING)) {
    DeserializationError err;
    if (filter) {
      err = deserializeJson(doc, apiCache.qualifying.data, DeserializationOption::Filter(*filter));
    } else {
      err = deserializeJson(doc, apiCache.qualifying.data);
    }
    if (!err) return true;
  }
  
  Serial.print(F("[Cache] MISS - Fetching: "));
  if (!httpGetJsonWithRetry(url.c_str(), 2, 400, filter)) return false;
  
  String jsonStr;
  serializeJson(doc, jsonStr);
  cacheData(&apiCache.qualifying, jsonStr);
  
  return true;
}

// ------------------- DRAWING HELPERS -------------------
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

// ------------------- WIFI MANAGER CALLBACK -------------------
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

// ------------------- HEADER -------------------
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

  if (!fetchCalendarWithCache(&filter)) {
    Serial.println(F("[ERR] races fetch failed."));
    return;
  }

  if (!doc["MRData"]["RaceTable"]["Races"].is<JsonArray>()) {
    Serial.println(F("[ERR] races JSON shape unexpected"));
    return;
  }

  JsonArray races = doc["MRData"]["RaceTable"]["Races"].as<JsonArray>();
  time_t nowEpoch = time(nullptr);
  int race_count = 0;

  for (JsonObject race : races) {
    const char* dateStr = race["date"] | "";
    String t = cleanTime(String(race["time"] | "00:00:00"));
    const char* rndStr  = race["round"] | "";
    unsigned rnd = (rndStr[0] == '\0') ? 0 : atoi(rndStr);

    const char* circuit = race["Circuit"]["circuitName"] | "";
    const char* GPname  = race["raceName"] | "";
    String loc;
    loc.reserve(64);  // Pre-allocate to reduce fragmentation
    loc = String(race["Circuit"]["Location"]["locality"].as<const char*>());
    loc += ", ";
    loc += race["Circuit"]["Location"]["country"].as<const char*>();

    if (race_count < 3) {
      Serial.printf("[DEBUG] R%u: Date=%s, Time=%s\n", rnd, dateStr, t.c_str());
      race_count++;
    }
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
      Serial.println(F("[STEP] FetchCalendar: Breaking loop early (Next race found)"));
      break;
    }
  }

  if (lastRound) Serial.printf("[DATA] Last: R%u %s (%s %s)\n", lastRound, lastGP.c_str(), lastDate.c_str(), lastTime.c_str());
  if (nextRound) Serial.printf("[DATA] Next: R%u %s (%s %s)\n", nextRound, nextGP.c_str(), nextDate.c_str(), nextTime.c_str());
  Serial.println(F("[STEP] FetchCalendar: end"));

  if (nextRound) {
    nextRaceEpoch   = isoUtcToEpoch(nextDate, nextTime);
    nextRaceDateStr = nextDate;
    prefs.putUInt("nextRound", nextRound);
    prefs.putULong64("nextEpoch", (uint64_t)nextRaceEpoch);
    prefs.putString("nextDate", nextRaceDateStr);
  }
  if (lastRound) {
    prefs.putUInt("lastRound", lastRound);
  }
}

// ------------------- DRAW LAST RACE -------------------
void DrawLastRace(bool currentRaceInProgress) {
  unsigned int targetRound = lastRound;
  time_t nowEpoch = time(nullptr);
  
  // Calculate timing values
  long diffToNext = nextRaceEpoch ? (long)(nextRaceEpoch - nowEpoch) : 999999;
  time_t lastRaceStartEpoch = lastDate.length() > 0 ? isoUtcToEpoch(lastDate, lastTime) : 0;
  long diffSinceLastRace = lastRaceStartEpoch ? (nowEpoch - lastRaceStartEpoch) : 999999;
  
  // Check if we're within 18 hours of the NEXT race starting
  bool nextRaceApproaching = false;
  if (nextRaceEpoch > 0 && diffToNext > 0 && diffToNext < NEXT_RACE_APPROACHING_SEC) {
    nextRaceApproaching = true;
  }
  
  // Check if the last race is still in progress (within 4 hours of start)
  bool isStillRaceEvent = (nowEpoch >= lastRaceStartEpoch) && 
                          (nowEpoch <= lastRaceStartEpoch + RACE_IN_PROGRESS_WINDOW_SEC);
  
  // Check if results are available
  bool haveResults = resultsAvailableForLastRound();

  // If next race is within 18h, show blank podium instead of old results
  if (nextRaceApproaching && !currentRaceInProgress) {
    display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
    display.drawRect(PODIUM_1ST_X, PODIUM_1ST_Y, PODIUM_1ST_W, PODIUM_1ST_H, GxEPD_BLACK);
    display.drawRect(PODIUM_2ND_X, PODIUM_2ND_Y, PODIUM_2ND_W, PODIUM_2ND_H, GxEPD_BLACK);
    display.drawRect(PODIUM_3RD_X, PODIUM_3RD_Y, PODIUM_3RD_W, PODIUM_3RD_H, GxEPD_BLACK);
    // Position text centered under podium boxes
    drawStringBLACK(PODIUM_TEXT_CENTER_X, PODIUM_TEXT_Y, "Awaiting Results", CENTER);
    return;
  }

  // Determine which GP name to show
  // If race is in progress but API has already updated (moved to "last"), use lastGP
  // If race is in progress and still "next" in API, use nextGP
  String displayGP = lastGP;
  if (currentRaceInProgress) {
    // Check if the current race is still "next" (at T=0) or has moved to "last" (T+2h)
    // If diffSinceLastRace is small (< 1 day), the "last" race IS the current race
    if (diffSinceLastRace < SECONDS_PER_DAY) {
      displayGP = lastGP;  // API updated, current race is now "last"
    } else {
      displayGP = nextGP;  // API hasn't updated yet, current race is still "next"
    }
  }

  if (isStillRaceEvent) {
    if (lastRound > 1) { 
      targetRound = lastRound - 1;
    }
  }

  if (targetRound == 0) {
    display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
    if (currentRaceInProgress) {
      drawStringBLACK(PODIUM_TEXT_CENTER_X, PODIUM_TEXT_Y, displayGP.c_str(), CENTER);
    }
    return;
  }
  
  String resultsUrl;
  resultsUrl.reserve(128);  // Pre-allocate to reduce fragmentation
  resultsUrl = seasonBaseUrl() + "/" + String(targetRound) + "/results/";
  drawStringBLACK(PODIUM_TEXT_CENTER_X, PODIUM_TEXT_Y, displayGP.c_str(), CENTER);
  
  StaticJsonDocument<512> filter;
  filter["MRData"]["RaceTable"]["Races"][0]["Results"][0]["Driver"]["familyName"] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["Results"][1]["Driver"]["familyName"] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["Results"][2]["Driver"]["familyName"] = true;
  
  // If current race in progress, show empty podium regardless of previous results
  if (currentRaceInProgress) {
    display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
    display.drawRect(PODIUM_1ST_X, PODIUM_1ST_Y, PODIUM_1ST_W, PODIUM_1ST_H, GxEPD_BLACK);
    display.drawRect(PODIUM_2ND_X, PODIUM_2ND_Y, PODIUM_2ND_W, PODIUM_2ND_H, GxEPD_BLACK);
    display.drawRect(PODIUM_3RD_X, PODIUM_3RD_Y, PODIUM_3RD_W, PODIUM_3RD_H, GxEPD_BLACK);
    return;
  }
  
  // Check cache first for results
  bool useCache = false;
  if (getCachedData(&apiCache.results, CACHE_TTL_STANDINGS)) {
    DeserializationError err = deserializeJson(doc, apiCache.results.data, DeserializationOption::Filter(filter));
    if (!err) useCache = true;
  }
  
  if (!useCache && httpGetJsonWithRetry(resultsUrl.c_str(), 3, 500, &filter)) {
    // Cache the results
    String jsonStr;
    serializeJson(doc, jsonStr);
    cacheData(&apiCache.results, jsonStr);
  }
  
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
      
      // Always draw the podium structure
      display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
      display.drawRect(PODIUM_1ST_X, PODIUM_1ST_Y, PODIUM_1ST_W, PODIUM_1ST_H, GxEPD_BLACK);
      display.drawRect(PODIUM_2ND_X, PODIUM_2ND_Y, PODIUM_2ND_W, PODIUM_2ND_H, GxEPD_BLACK);
      display.drawRect(PODIUM_3RD_X, PODIUM_3RD_Y, PODIUM_3RD_W, PODIUM_3RD_H, GxEPD_BLACK);

      // Show names if results are available
      if (haveResults) {
        drawStringRED(SCREEN_WIDTH / 2 - 1, PODIUM_NAME_1ST_Y, abbrev0.c_str(), CENTER);
        drawStringRED(SCREEN_WIDTH / 2 + PODIUM_NAME_2ND_X_OFFSET, PODIUM_NAME_2ND_Y, abbrev1.c_str(), LEFT);
        drawStringRED(SCREEN_WIDTH / 2 + PODIUM_NAME_3RD_X_OFFSET, PODIUM_NAME_3RD_Y, abbrev2.c_str(), LEFT);
      }
    } else {
      display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
    }
  } else {
    display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
  }
}

// ------------------- COUNTDOWN HELPERS -------------------
String nextRaceCountdownDH() {
  time_t epoch = nextRaceEpoch ? nextRaceEpoch : isoUtcToEpoch(nextDate, nextTime);
  if (!epoch) return "";

  long diff = (long)(epoch - time(nullptr));
  if (diff <= 0) {
    return "";
  }

  long days  = diff / 86400L;
  long hours = (diff % 86400L) / 3600L;

  if (days == 0 && hours == 0) {
    return "<1 hour";
  }

  String out;
  out.reserve(32);  // Pre-allocate to reduce fragmentation
  if (days > 0) {
    out += String(days) + " day";
    if (days != 1) out += "s";
    if (hours > 0) out += " ";
  }
  if (hours > 0) {
    out += String(hours) + " hr";
    if (hours != 1) out += "s";
  }

  return out;
}

// ------------------- DRAW NEXT RACE -------------------
void DrawNextRace() {
  if (nextRound == 0) return;

  gfx.setFont(u8g2_font_helvB08_tf);
  
  const int NEXT_RACE_MAX_WIDTH = 200;  // Only use left 200px, leave right side for drivers

  // Check if race is currently in progress
  if (isRaceInProgress()) {
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
  // Clear only the area we're writing to (left side only)
  display.fillRect(nextLabelW + 4, NEXT_LINE1_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - nextLabelW - 4, LINE_H, GxEPD_WHITE);
  drawStringBLACK(nextLabelW + 4, NEXT_LINE1_Y, nextGP, LEFT);

  // Line 2: "Lights Out:" + dynamic tail
  const char* loLabel = "Lights Out:";
  int loW = textWidth(loLabel);
  drawStringRED(0, NEXT_LINE2_Y, loLabel, LEFT);

  time_t epoch = nextRaceEpoch ? nextRaceEpoch : isoUtcToEpoch(nextDate, nextTime);
  long diff = epoch ? (long)(epoch - time(nullptr)) : 0;

  if (diff <= 0) {
    // This shouldn't happen now that we have isRaceInProgress() check above
    // But keep as fallback
    display.fillRect(0, NEXT_LINE2_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH, LINE_H, GxEPD_WHITE);
    drawStringRED(0, NEXT_LINE2_Y, "Lights Out!", LEFT);
    return;
  }
  
  String dLocal = nextRaceLocalDateMMDD();
  String tLocal = nextRaceLocalTimeLower();
  String tMinus = nextRaceCountdownDH();

  String tail;
  tail.reserve(64);  // Pre-allocate to reduce fragmentation
  if (dLocal.length()) tail += dLocal;
  if (tLocal.length()) { 
    tLocal.trim();
    if (tail.length()) tail += " "; 
    tail += tLocal; 
  }
  if (tMinus.length()) { if (tail.length()) tail += " - "; tail += tMinus; }
  tail.trim();

  // Clear only the area where countdown will be written (left side)
  display.fillRect(loW + 4, NEXT_LINE2_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - loW - 4, LINE_H, GxEPD_WHITE);
  drawStringBLACK(loW + 4, NEXT_LINE2_Y, tail, LEFT);
}

void DrawDrivers() {
  bool showGrid = false;
  
  // Check if we should show Starting Grid for the NEXT race (within 18h)
  if (nextRaceEpoch > 0 && nextRound > 0) {
    long diff = (long)(nextRaceEpoch - time(nullptr));
   if (diff > 0 && diff <= NEXT_RACE_APPROACHING_SEC && qualifyingAvailableForRound(nextRound)) {
      showGrid = true;
    }
  }
  
  // OR check if race is currently in progress for LAST race (within 4 hours of start)
  if (!showGrid && lastRound > 0) {
    time_t nowEpoch = time(nullptr);
    time_t lastRaceStartEpoch = isoUtcToEpoch(lastDate, lastTime);
    bool isStillRaceEvent = (nowEpoch >= lastRaceStartEpoch) && 
                            (nowEpoch <= lastRaceStartEpoch + RACE_IN_PROGRESS_WINDOW_SEC);
    
    if (isStillRaceEvent && qualifyingAvailableForRound(lastRound)) {
      showGrid = true;
      // Draw the grid for the race that's happening NOW (lastRound)
      gfx.setFont(u8g2_font_helvB08_tf);
      drawStringRED(SCREEN_WIDTH - 5, DRIVERS_TITLE_Y, F("Starting Grid"), RIGHT);
      DrawStartingGrid(lastRound);
      return;
    }
  }

  gfx.setFont(u8g2_font_helvB08_tf);
  if (showGrid) {
    drawStringRED(SCREEN_WIDTH - 5, DRIVERS_TITLE_Y, F("Starting Grid"), RIGHT);
    DrawStartingGrid(nextRound);
  } else {
    drawStringRED(308, DRIVERS_TITLE_Y, F("Top 10 Drivers"), RIGHT);
    DrawDriversStandings();
  }
}

// ------------------- DRAW DRIVERS STANDINGS -------------------
void DrawDriversStandings() {
  gfx.setFont(u8g2_font_helvB08_tf);

  StaticJsonDocument<512> filter;
  for (int i = 0; i < 10; i++) {
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["position"] = true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["Driver"]["familyName"] = true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"][i]["points"] = true;
  }

  if (!fetchDriverStandingsWithCache(&filter)) {
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

bool qualifyingAvailableForRound(unsigned round) {
  if (round == 0) return false;

  // Check availability cache first (qualifying never changes once posted)
  if (availabilityCache.qualifyingValid && 
      availabilityCache.round == round && 
      (millis() - availabilityCache.qualifyingTimestamp) < QUALIFYING_AVAILABILITY_TTL) {
    return availabilityCache.qualifyingAvailable;
  }

  // Use cached qualifying data if available
  if (getCachedData(&apiCache.qualifying, CACHE_TTL_QUALIFYING)) {
    StaticJsonDocument<256> filter;
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][0]["Driver"]["familyName"] = true;
    DeserializationError err = deserializeJson(doc, apiCache.qualifying.data, DeserializationOption::Filter(filter));
    if (!err && doc["MRData"]["RaceTable"]["Races"].is<JsonArray>() &&
        doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].is<JsonArray>()) {
      JsonArray q = doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].as<JsonArray>();
      bool available = q.size() > 0;
      // Update availability cache (qualifying never changes once posted)
      availabilityCache.round = round;
      availabilityCache.qualifyingAvailable = available;
      availabilityCache.qualifyingTimestamp = millis();
      availabilityCache.qualifyingValid = true;
      return available;
    }
  }

  // Cache miss - fetch with minimal filter
  StaticJsonDocument<256> filter;
  filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][0]["Driver"]["familyName"] = true;

  String url;
  url.reserve(128);
  url = seasonBaseUrl() + "/" + String(round) + "/qualifying/";
  if (!httpGetJsonWithRetry(url.c_str(), 2, 400, &filter)) {
    // Update cache with false result (will check again later)
    availabilityCache.round = round;
    availabilityCache.qualifyingAvailable = false;
    availabilityCache.qualifyingTimestamp = millis();
    availabilityCache.qualifyingValid = true;
    return false;
  }

  if (!doc["MRData"]["RaceTable"]["Races"].is<JsonArray>()) return false;
  if (!doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].is<JsonArray>()) return false;
  JsonArray q = doc["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"].as<JsonArray>();
  bool available = q.size() > 0;
  
  // Update availability cache (qualifying never changes once posted)
  availabilityCache.round = round;
  availabilityCache.qualifyingAvailable = available;
  availabilityCache.qualifyingTimestamp = millis();
  availabilityCache.qualifyingValid = true;
  
  return available;
}

void DrawStartingGrid(unsigned round) {
  if (round == 0) return;
  gfx.setFont(u8g2_font_helvB08_tf);

  StaticJsonDocument<768> filter;
  for (int i = 0; i < 10; i++) {
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["position"] = true;
    filter["MRData"]["RaceTable"]["Races"][0]["QualifyingResults"][i]["Driver"]["familyName"] = true;
  }

  String url;
  url.reserve(128);  // Pre-allocate to reduce fragmentation
  url = seasonBaseUrl() + "/" + String(round) + "/qualifying/";
  if (!fetchQualifyingWithCache(round, &filter)) {
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
    lineLeft.reserve(32);  // Pre-allocate to reduce fragmentation
    lineLeft = pos + " " + fam;
    int y = DRIVERS_ROW_Y0 + i * 10;
    drawStringBLACK(listx, y, lineLeft, LEFT);
  }
}

// ------------------- DRAW CONSTRUCTORS -------------------
void DrawConstructors() {
  StaticJsonDocument<512> filter;
  for (int i = 0; i < 5; i++) {
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["position"] = true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["Constructor"]["name"] = true;
    filter["MRData"]["StandingsTable"]["StandingsLists"][0]["ConstructorStandings"][i]["points"] = true;
  }

  if (!fetchConstructorStandingsWithCache(&filter)) {
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
    nameLine.reserve(48);  // Pre-allocate to reduce fragmentation
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

bool resultsAvailableForLastRound() {
  if (lastRound == 0) return false;

  // Check availability cache first (results can appear after race finishes)
  if (availabilityCache.resultsValid && 
      availabilityCache.round == lastRound && 
      (millis() - availabilityCache.resultsTimestamp) < RESULTS_AVAILABILITY_TTL) {
    return availabilityCache.resultsAvailable;
  }

  // Check if we have cached results
  if (getCachedData(&apiCache.results, CACHE_TTL_STANDINGS)) {
    StaticJsonDocument<512> filter;
    filter["MRData"]["RaceTable"]["Races"][0]["Results"][0] = true;
    filter["MRData"]["RaceTable"]["Races"][0]["Results"][1] = true;
    filter["MRData"]["RaceTable"]["Races"][0]["Results"][2] = true;
    DeserializationError err = deserializeJson(doc, apiCache.results.data, DeserializationOption::Filter(filter));
    if (!err && doc["MRData"]["RaceTable"]["Races"].size() > 0 &&
        doc["MRData"]["RaceTable"]["Races"][0]["Results"].is<JsonArray>()) {
      JsonArray podium = doc["MRData"]["RaceTable"]["Races"][0]["Results"].as<JsonArray>();
      bool available = podium.size() >= 3;
      // Update availability cache (results can appear after race)
      availabilityCache.round = lastRound;
      availabilityCache.resultsAvailable = available;
      availabilityCache.resultsTimestamp = millis();
      availabilityCache.resultsValid = true;
      return available;
    }
  }

  // Cache miss - fetch
  StaticJsonDocument<512> filter;
  filter["MRData"]["RaceTable"]["Races"][0]["Results"][0] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["Results"][1] = true;
  filter["MRData"]["RaceTable"]["Races"][0]["Results"][2] = true;

  String url;
  url.reserve(128);
  url = seasonBaseUrl() + "/" + String(lastRound) + "/results/";
  if (!httpGetJsonWithRetry(url.c_str(), 3, 500, &filter)) {
    // Update cache with false result (will check again later)
    availabilityCache.round = lastRound;
    availabilityCache.resultsAvailable = false;
    availabilityCache.resultsTimestamp = millis();
    availabilityCache.resultsValid = true;
    return false;
  }

  // Cache the results
  String jsonStr;
  serializeJson(doc, jsonStr);
  cacheData(&apiCache.results, jsonStr);

  if (doc["MRData"]["RaceTable"]["Races"].size() == 0) return false;
  if (!doc["MRData"]["RaceTable"]["Races"][0]["Results"].is<JsonArray>()) return false;
  JsonArray podium = doc["MRData"]["RaceTable"]["Races"][0]["Results"].as<JsonArray>();
  bool available = podium.size() >= 3;
  
  // Update availability cache (results can appear after race)
  availabilityCache.round = lastRound;
  availabilityCache.resultsAvailable = available;
  availabilityCache.resultsTimestamp = millis();
  availabilityCache.resultsValid = true;
  
  return available;
}

// ------------------- SETUP -------------------
void setup() {
  Serial.begin(115200);
  delay(2000);

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
  wifiManager.setConfigPortalTimeout(300); // 5 min timeout

  Serial.println(F("[WiFi] Starting WiFi setup..."));
  
  if (!wifiManager.autoConnect("F1Tracker-Setup", "formula1")) {
    Serial.println(F("[WiFi] Failed to connect - restarting..."));
    
    // Show failure message
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

  // Show success message
  display.fillScreen(GxEPD_WHITE);
  display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, 50, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
  drawStringBLACK(40, 84, F("It's lights out and away we go!!!"), LEFT);
  display.display(false);
  display.hibernate();

  // Initialize preferences
  prefs.begin("f1tracker", false);
  processedRound   = prefs.getUInt("processedRound", 0);
  nextRound        = prefs.getUInt("nextRound", 0);
  nextRaceEpoch    = (time_t)prefs.getULong64("nextEpoch", 0);
  nextRaceDateStr  = prefs.getString("nextDate", "");

  // Initial data fetch and display
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    DrawTime();
    FetchCalendar();
    
    bool raceIsInProgress = isRaceInProgress();

    DrawNextRace();  // This will now properly show "Race in Progress!" if needed
    DrawDrivers();
    DrawConstructors();
    DrawLastRace(raceIsInProgress);
    
  } while (display.nextPage());
  display.hibernate();

  if (lastRound) {
    processedRound = lastRound;
    prefs.putUInt("processedRound", processedRound);
  }
  
  // Initialize wall-clock tracking
  lastCheckMinute = timeinfo.tm_min;
  
  Serial.println(F("[Setup] Complete!"));
}

// ------------------- FULL REFRESH -------------------
void fullRefreshAndReschedule() {
  // Update current time before drawing
  getLocalTime(&timeinfo);
  
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    DrawTime();
    FetchCalendar();
    DrawDrivers();
    DrawConstructors();
    DrawLastRace(isRaceInProgress());
    DrawNextRace();  // This will now properly show "Race in Progress!" if needed
  } while (display.nextPage());
  display.hibernate();

  processedRound = lastRound;
  prefs.putUInt("processedRound", processedRound);
}

// ------------------- WALL CLOCK ALIGNED SCHEDULER -------------------
bool shouldUpdateNow(int currentMinute) {
  // Prevent duplicate updates in same minute
  if (currentMinute == lastCheckMinute) {
    return false;
  }
  
  if (nextRaceEpoch == 0) {
    // No race scheduled, use normal 2-hour updates
    if (currentMinute == 0 && (timeinfo.tm_hour % 2 == 0)) {
      return true;
    }
    return false;
  }
  
  time_t now = time(nullptr);
  long diffToRace = (long)(nextRaceEpoch - now);
  
  // Determine if we're in race window (6h before to 6h after race)
  bool inRaceWindow = isInRaceWindow();
  
  if (inRaceWindow) {
    // During race window: Update every 30 minutes (at :00 and :30)
    if (currentMinute % UPDATE_INTERVAL_RACE_WINDOW_MIN == 0) {
      Serial.printf("[Scheduler] Race window - update triggered at minute %d\n", currentMinute);
      return true;
    }
  } else if (diffToRace > 0 && diffToRace <= HOURS_24_BEFORE_RACE_SEC) {
    // Within 24 hours before race: Update every 30 minutes to catch starting grid
    if (currentMinute % UPDATE_INTERVAL_GRID_SEARCH_MIN == 0) {
      // Check cached availability (don't make API call here - check during actual update)
      // If availability cache is valid and says grid is available, switch to 2-hour updates
      // Qualifying never changes once posted, so we can trust the cache for 12 hours
      if (availabilityCache.qualifyingValid && 
          availabilityCache.round == nextRound && 
          availabilityCache.qualifyingAvailable &&
          (millis() - availabilityCache.qualifyingTimestamp) < QUALIFYING_AVAILABILITY_TTL) {
        // Grid found! Switch to 2-hour updates until race window
        if (currentMinute == 0 && (timeinfo.tm_hour % 2 == 0)) {
          Serial.printf("[Scheduler] Grid found (cached) - 2h updates until race window at minute %d\n", currentMinute);
          return true;
        }
      } else {
        // Still searching for grid: Update every 30 minutes
        Serial.printf("[Scheduler] Searching for grid - update at minute %d\n", currentMinute);
        return true;
      }
    }
  } else if (diffToRace > HOURS_24_BEFORE_RACE_SEC && diffToRace <= HOURS_48_BEFORE_RACE_SEC) {
    // Between 24-48 hours: Update every 2 hours (grid might be posted)
    if (currentMinute == 0 && (timeinfo.tm_hour % 2 == 0)) {
      Serial.printf("[Scheduler] 24-48h before race - 2h updates at hour %d\n", timeinfo.tm_hour);
      return true;
    }
  } else if (diffToRace > HOURS_48_BEFORE_RACE_SEC) {
    // More than 48 hours before: Update every 6 hours (at :00 of 0, 6, 12, 18)
    if (currentMinute == 0 && (timeinfo.tm_hour % 6 == 0)) {
      Serial.printf("[Scheduler] >48h before race - 6h updates at hour %d\n", timeinfo.tm_hour);
      return true;
    }
  } else {
    // Fallback: Normal 2-hour updates
    if (currentMinute == 0 && (timeinfo.tm_hour % 2 == 0)) {
      Serial.printf("[Scheduler] Normal schedule - update at hour %d\n", timeinfo.tm_hour);
      return true;
    }
  }
  
  return false;
}

// ------------------- WIFI POWER MANAGEMENT -------------------
void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;  // Already connected
  }
  
  Serial.println(F("[WiFi] Reconnecting..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Uses saved credentials
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WiFi] Reconnected!"));
    Serial.println("IP: " + WiFi.localIP().toString());
    wifiConnected = true;
  } else {
    Serial.println(F("\n[WiFi] Reconnection failed - restarting..."));
    delay(1000);
    ESP.restart();
  }
}

void disconnectWiFiIfIdle() {
  // Disconnect WiFi after updates to save power
  // WiFi will be reconnected automatically when needed
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);  // Disconnect and turn off radio
    WiFi.mode(WIFI_OFF);
    wifiConnected = false;
    Serial.println(F("[WiFi] Disconnected to save power"));
  }
}

// ------------------- LOOP -------------------
void loop() {
  // Get current time (doesn't require WiFi)
  if (!getLocalTime(&timeinfo)) {
    // Time sync failed - reconnect WiFi and retry
    ensureWiFiConnected();
    if (!getLocalTime(&timeinfo)) {
      delay(1000);
      return;
    }
  }
  
  int currentMinute = timeinfo.tm_min;
  
  // Check if it's time to update
  if (shouldUpdateNow(currentMinute)) {
    lastCheckMinute = currentMinute;
    
    Serial.printf("\n[Scheduler] Wall-clock aligned update at %02d:%02d\n", 
                  timeinfo.tm_hour, timeinfo.tm_min);

    // Ensure WiFi is connected for API calls
    ensureWiFiConnected();

    // Re-read calendar to detect round changes / next race timing shifts
    FetchCalendar();

    // Decide if we need to invalidate standings cache
    bool newRoundFinished = (processedRound != lastRound && lastRound != 0);
    bool haveResults      = resultsAvailableForLastRound();
    
    // Invalidate standings cache when race finishes
    if (newRoundFinished) {
      invalidateStandingsCache();
    }

    // Always do full refresh (simpler for e-paper)
    fullRefreshAndReschedule();
    
    // Disconnect WiFi after update to save power
    disconnectWiFiIfIdle();
  }
  
  // Check every 60 seconds (updates only happen at minute boundaries)
  delay(60000);
}
