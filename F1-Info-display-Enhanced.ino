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

// ------------------- DEBUG MODE ----------------------
// Set to 1 to enable debug mode with time offset simulation
// In debug mode, all time calculations use simulated time, but API calls use real time
// This allows testing race scenarios using past races that have complete data
//
// INTERACTIVE COMMANDS (when DEBUG_MODE = 1):
// Open Serial Monitor (115200 baud) and send commands:
//   +1h, +2h, +6h    - Add hours to offset
//   -1h, -2h, -6h    - Subtract hours from offset
//   +1d, +2d, +7d    - Add days to offset
//   -1d, -2d, -7d    - Subtract days from offset
//   set <seconds>    - Set exact offset (e.g., "set -2592000")
//   reset            - Reset to default offset
//   status           - Show current offset and simulated time
//   refresh          - Force immediate display update
//   before           - Jump to 2 hours before race (if race detected)
//   during           - Jump to race start time
//   after            - Jump to 1 hour after race start
//
// IMPORTANT: Set to 1 to enable debug mode, 0 to disable
#define DEBUG_MODE 0

// Time offset in seconds (negative = simulate being in the past relative to real time)
// This is now a variable (not const) so it can be changed interactively in debug mode
// Default: -30 days
long debugTimeOffsetSeconds = -2592000;

// Auto-refresh flag for debug mode (set to false to disable automatic scheduler updates)
// Default to false in debug mode - only refresh when menu option is selected
bool debugAutoRefresh = false;

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

// ------------------- DEBUG TIME HELPERS ----------------------
// Get simulated time for logic/scheduling (uses offset in debug mode)
time_t getSimulatedTime() {
  #if DEBUG_MODE
    return time(nullptr) + debugTimeOffsetSeconds;
  #else
    return time(nullptr);
  #endif
}

// Get simulated local time and populate timeinfo struct
// In debug mode, this uses simulated time; otherwise uses real time
bool getSimulatedLocalTime(struct tm* timeinfo) {
  #if DEBUG_MODE
    time_t simTime = getSimulatedTime();
    return localtime_r(&simTime, timeinfo) != nullptr;
  #else
    return getLocalTime(timeinfo);
  #endif
}

// ------------------- PERSISTED STATE ----------------------
Preferences prefs;
unsigned processedRound = 0;
time_t   nextRaceEpoch   = 0;
String   nextRaceDateStr;

// ------------------- CACHED VALUES (optimization) ----------------------
time_t   cachedLastRaceEpoch = 0;  // Cached lastRaceEpoch calculation
time_t   cachedNowEpoch = 0;        // Cached current time (updated per cycle)
unsigned cachedLastRaceRound = 0;  // Track when lastRaceEpoch was calculated
unsigned cachedQualifyingRound = 0; // Track which round we checked qualifying for
bool     cachedQualifyingAvailable = false; // Cached qualifying availability


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
// NOTE: Always uses REAL time (not simulated) so API calls fetch correct year's data
String seasonBaseUrl() {
  time_t now = time(nullptr);  // Real time for API calls
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
  
  time_t now = getSimulatedTime();  // Use simulated time for logic
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
  
  time_t now = getSimulatedTime();  // Use simulated time for logic
  long diff = nextRaceEpoch - now;
  
  return (diff <= RACE_WINDOW_BEFORE_SEC && diff >= -RACE_WINDOW_AFTER_SEC);
}

// ------------------- HTTP ----------------------
template <typename TFilterDoc>
bool httpDeserializeFromBuffer(const String& payload,
                               StaticJsonDocument<API_SCRATCH_DOC_SIZE>& target,
                               TFilterDoc* filter) {
  if (payload.length() == 0) {
    Serial.println(F("[ERR] HTTP body empty"));
    return false;
  }

  DeserializationError err;
  if (filter) {
    err = deserializeJson(target, payload, DeserializationOption::Filter(*filter));
  } else {
    err = deserializeJson(target, payload);
  }

  if (err) {
    Serial.printf("[ERR] JSON parse failed: %s (body %u bytes, heap %u)\n",
                  err.c_str(), payload.length(), ESP.getFreeHeap());
    return false;
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
  http.begin(url);
  http.setConnectTimeout(20000);
  http.setTimeout(45000);
  http.addHeader("User-Agent", "F1Tracker/NanoESP32");
  http.addHeader("Accept-Encoding", "identity");

  int httpCode = http.GET();
  Serial.printf("HTTP Status Code: %d\n", httpCode);
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[ERR] HTTP GET failed. Code %d\n", httpCode);
    http.end();
    return false;
  }

  int expectedSize = http.getSize();
  Serial.println(F("[STEP] net: Reading body"));
  String payload;
  if (expectedSize > 0) payload.reserve((size_t)expectedSize + 1);
  payload = http.getString();
  http.end();

  Serial.printf("[INFO] Received %u bytes", payload.length());
  if (expectedSize > 0) Serial.printf(" (expected %d)", expectedSize);
  Serial.println();

  if (expectedSize > 0 && (int)payload.length() < expectedSize) {
    Serial.printf("[ERR] Incomplete HTTP body (%u/%d bytes, heap %u)\n",
                  payload.length(), expectedSize, ESP.getFreeHeap());
    return false;
  }

  Serial.println(F("[STEP] net: Deserializing"));
  bool ok = httpDeserializeFromBuffer(payload, doc, filter);
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

// ------------------- MODE DETECTION -------------------
#if DEBUG_MODE
int getCurrentDisplayMode() {
  // Use cached time if available (optimization)
  time_t nowEpoch = (cachedNowEpoch > 0) ? cachedNowEpoch : getSimulatedTime();
  
  // First, check if we have a next race scheduled
  if (nextRaceEpoch > 0 && nextRound > 0) {
    long diffToRace = (long)(nextRaceEpoch - nowEpoch);
    
    // Mode 3: Race In Progress (race started, results not available)
    // Race is in progress if we're past start time but within 4 hours
    if (diffToRace <= 0 && diffToRace >= -RACE_IN_PROGRESS_WINDOW_SEC) {
      // Check if results are available for THIS race (nextRound)
      bool haveResultsForCurrentRace = false;
      
      // Check cache for nextRound results
      if (getCachedData(&apiCache.results, CACHE_TTL_STANDINGS)) {
        StaticJsonDocument<512> filter;
        filter["MRData"]["RaceTable"]["Races"][0]["round"] = true;
        filter["MRData"]["RaceTable"]["Races"][0]["Results"][0] = true;
        DeserializationError err = deserializeJson(doc, apiCache.results.data, DeserializationOption::Filter(filter));
        if (!err && doc["MRData"]["RaceTable"]["Races"].size() > 0) {
          // Check if this is the current race
          unsigned int cachedRound = doc["MRData"]["RaceTable"]["Races"][0]["round"] | 0;
          if (cachedRound == nextRound) {
            if (doc["MRData"]["RaceTable"]["Races"][0]["Results"].is<JsonArray>()) {
              JsonArray results = doc["MRData"]["RaceTable"]["Races"][0]["Results"].as<JsonArray>();
              haveResultsForCurrentRace = (results.size() >= 3);
            }
          }
        }
      }
      
      // If no results for current race, it's Mode 3
      if (!haveResultsForCurrentRace) {
        return 3;  // Race In Progress
      }
      // If results are available, fall through to Mode 4 check
    }
    
    // Mode 2: Race Approaching (within 18h before race, grid available)
    // Check this BEFORE Mode 4, since Mode 2 is more specific
    if (diffToRace > 0 && diffToRace <= NEXT_RACE_APPROACHING_SEC) {
      if (qualifyingAvailableForRound(nextRound)) {
        return 2;  // Race Approaching
      }
    }
  }
  
  // Mode 4: Post-Race (results available for last completed race)
  // Only check if we're NOT in race window for next race AND far enough past last race
  // This prevents Mode 4 from showing when we're just in normal operation with old results
  if (lastRound > 0 && !isRaceInProgress() && !isInRaceWindow()) {
    // Check if we're more than 4 hours past the last race start
    // Use cached lastRaceEpoch if available (optimization)
    if (cachedLastRaceEpoch == 0 && lastDate.length() > 0) {
      cachedLastRaceEpoch = isoUtcToEpoch(lastDate, lastTime);
    }
    time_t lastRaceStartEpoch = cachedLastRaceEpoch;
    if (lastRaceStartEpoch > 0) {
      long diffSinceLastRace = nowEpoch - lastRaceStartEpoch;
      // Only show Mode 4 if we're well past the last race (more than 4 hours)
      if (diffSinceLastRace > RACE_IN_PROGRESS_WINDOW_SEC) {
        if (resultsAvailableForLastRound()) {
          return 4;  // Post-Race
        }
      }
    }
  }
  
  // Mode 1: Normal Operation (default)
  return 1;  // Normal Operation
}
#endif

// ------------------- HEADER -------------------
void DrawTime() {
  char datebuf[16], timebuf[16];
  strftime(datebuf, sizeof(datebuf), "%m/%d/%Y", &timeinfo);
  strftime(timebuf, sizeof(timebuf), "%I:%M %p", &timeinfo);
  if (timebuf[0] == '0') memmove(timebuf, timebuf + 1, strlen(timebuf));
  for (char* p = timebuf; *p; ++p) *p = tolower(*p);

  String leftStr  = String("Today: ")   + datebuf;
  #if DEBUG_MODE
    int currentMode = getCurrentDisplayMode();
    leftStr = String("[DEBUG] Mode ") + String(currentMode) + " " + datebuf;  // Show debug indicator with mode
  #endif
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
  time_t nowEpoch = getSimulatedTime();  // Use simulated time for logic
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
  // Note: We don't clear nextEpoch when nextRound is 0, as the scheduler
  // uses it as a fallback to detect if a race just finished
  if (lastRound) {
    prefs.putUInt("lastRound", lastRound);
    prefs.putString("lastDate", lastDate);
    prefs.putString("lastTime", lastTime);
    prefs.putString("lastGP", lastGP);
    // Invalidate cached lastRaceEpoch when lastRound changes
    if (cachedLastRaceRound != lastRound) {
      cachedLastRaceEpoch = 0;
      cachedLastRaceRound = lastRound;
    }
    // Invalidate cached lastRaceEpoch when lastRound changes
    if (cachedLastRaceRound != lastRound) {
      cachedLastRaceEpoch = 0;
      cachedLastRaceRound = lastRound;
    }
  }
}

// ------------------- DRAW LAST RACE -------------------
void DrawLastRace(bool currentRaceInProgress) {
  unsigned int targetRound = lastRound;
  // Use cached time (optimization)
  time_t nowEpoch = cachedNowEpoch;
  
  // Calculate timing values
  long diffToNext = nextRaceEpoch ? (long)(nextRaceEpoch - nowEpoch) : 999999;
  // Use cached lastRaceEpoch if available (optimization)
  if (cachedLastRaceEpoch == 0 && lastDate.length() > 0) {
    cachedLastRaceEpoch = isoUtcToEpoch(lastDate, lastTime);
  }
  time_t lastRaceStartEpoch = cachedLastRaceEpoch;
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
  
  #if DEBUG_MODE
    // In debug mode, use mode detection to determine behavior
    int currentMode = getCurrentDisplayMode();
    Serial.printf("[DEBUG] DrawLastRace: currentMode = %d, nextRaceApproaching = %d, currentRaceInProgress = %d\n", 
                  currentMode, nextRaceApproaching, currentRaceInProgress);
    // Mode 2 (Race Approaching): Always show blank podium, hide old results
    if (currentMode == 2) {
      Serial.println(F("[DEBUG] DrawLastRace: Mode 2 detected, showing blank podium"));
      display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
      display.drawRect(PODIUM_1ST_X, PODIUM_1ST_Y, PODIUM_1ST_W, PODIUM_1ST_H, GxEPD_BLACK);
      display.drawRect(PODIUM_2ND_X, PODIUM_2ND_Y, PODIUM_2ND_W, PODIUM_2ND_H, GxEPD_BLACK);
      display.drawRect(PODIUM_3RD_X, PODIUM_3RD_Y, PODIUM_3RD_W, PODIUM_3RD_H, GxEPD_BLACK);
      drawStringBLACK(PODIUM_TEXT_CENTER_X, PODIUM_TEXT_Y, "Awaiting Results", CENTER);
      return;
    }
  #else
    int currentMode = 0;  // Not used in production, but needed for variable scope
  #endif

  // If next race is within 18h (Mode 2: Race Approaching), show blank podium
  // This hides old results and shows "Awaiting Results" for the upcoming race
  // Also check if qualifying is available (confirms we're in Mode 2, not just approaching)
  if (nextRaceApproaching && !currentRaceInProgress) {
    // Double-check: if qualifying is available, we're definitely in Mode 2
    if (nextRound > 0 && qualifyingAvailableForRound(nextRound)) {
      display.drawBitmap(SCREEN_WIDTH / 2 - logoWidth / 2, PODIUM_LOGO_Y, F1_Logo, logoWidth, logoHeight, GxEPD_RED);
      display.drawRect(PODIUM_1ST_X, PODIUM_1ST_Y, PODIUM_1ST_W, PODIUM_1ST_H, GxEPD_BLACK);
      display.drawRect(PODIUM_2ND_X, PODIUM_2ND_Y, PODIUM_2ND_W, PODIUM_2ND_H, GxEPD_BLACK);
      display.drawRect(PODIUM_3RD_X, PODIUM_3RD_Y, PODIUM_3RD_W, PODIUM_3RD_H, GxEPD_BLACK);
      drawStringBLACK(PODIUM_TEXT_CENTER_X, PODIUM_TEXT_Y, "Awaiting Results", CENTER);
      return;
    }
  }

  // If we're in Mode 2 (next race approaching with grid), don't show old race name
  // Skip drawing old podium entirely (already handled above, but double-check)
  #if DEBUG_MODE
    if (currentMode == 2) {
      // Already handled above, but double-check we don't continue
      return;
    }
  #endif
  
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

  long diff = (long)(epoch - getSimulatedTime());  // Use simulated time for logic
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
  
  // Cache text widths for static strings (optimization)
  static int cachedCurrentLabelW = -1;
  static int cachedNextLabelW = -1;
  static int cachedLoLabelW = -1;
  static int cachedAwaitLabelW = -1;
  
  if (cachedCurrentLabelW < 0) {
    cachedCurrentLabelW = textWidth("Current Race:");
    cachedNextLabelW = textWidth("Next:");
    cachedLoLabelW = textWidth("Lights Out:");
    cachedAwaitLabelW = textWidth("Awaiting Results");
  }

  // Check if race is currently in progress
  if (isRaceInProgress()) {
    // Show "Current Race:" instead of "Next:"
    drawStringRED(0, NEXT_LINE1_Y, "Current Race:", LEFT);
    display.fillRect(cachedCurrentLabelW + 4, NEXT_LINE1_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - cachedCurrentLabelW - 4, LINE_H, GxEPD_WHITE);
    drawStringBLACK(cachedCurrentLabelW + 4, NEXT_LINE1_Y, nextGP, LEFT);
    
    // Line 2: "Race in Progress!"
    drawStringRED(0, NEXT_LINE2_Y, "Race in Progress!", LEFT);
    return;
  }

  // Check if we're showing the starting grid (within 18h of race) - show "Awaiting Results" instead of countdown
  bool showingGrid = false;
  if (nextRaceEpoch > 0 && nextRound > 0) {
    // Use cached time (optimization)
    long diff = (long)(nextRaceEpoch - cachedNowEpoch);
    if (diff > 0 && diff <= NEXT_RACE_APPROACHING_SEC && qualifyingAvailableForRound(nextRound)) {
      showingGrid = true;
    }
  }

  // Line 1: "Next:" + race name
  drawStringRED(0, NEXT_LINE1_Y, "Next:", LEFT);
  // Clear only the area we're writing to (left side only)
  display.fillRect(cachedNextLabelW + 4, NEXT_LINE1_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - cachedNextLabelW - 4, LINE_H, GxEPD_WHITE);
  drawStringBLACK(cachedNextLabelW + 4, NEXT_LINE1_Y, nextGP, LEFT);

  // Line 2: Show "Awaiting Results" if grid is showing, otherwise show countdown
  if (showingGrid) {
    // Grid is showing, so race is approaching - show "Awaiting Results"
    drawStringRED(0, NEXT_LINE2_Y, "Awaiting Results", LEFT);
    display.fillRect(cachedAwaitLabelW + 4, NEXT_LINE2_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - cachedAwaitLabelW - 4, LINE_H, GxEPD_WHITE);
  } else {
    // Normal countdown display
    drawStringRED(0, NEXT_LINE2_Y, "Lights Out:", LEFT);

    time_t epoch = nextRaceEpoch ? nextRaceEpoch : isoUtcToEpoch(nextDate, nextTime);
    // Use cached time (optimization)
    long diff = epoch ? (long)(epoch - cachedNowEpoch) : 0;

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

    // Optimize string concatenation (reduce temporary objects)
    String tail;
    tail.reserve(80);  // Pre-allocate to reduce fragmentation (slightly larger for safety)
    
    // Build string more efficiently
    bool needsSpace = false;
    if (dLocal.length()) {
      tail = dLocal;
      needsSpace = true;
    }
    if (tLocal.length()) {
      tLocal.trim();
      if (needsSpace) tail += " ";
      tail += tLocal;
      needsSpace = true;
    }
    if (tMinus.length()) {
      if (needsSpace) tail += " - ";
      tail += tMinus;
    }
    tail.trim();

    // Clear only the area where countdown will be written (left side)
    display.fillRect(cachedLoLabelW + 4, NEXT_LINE2_Y - (LINE_H - 10), NEXT_RACE_MAX_WIDTH - cachedLoLabelW - 4, LINE_H, GxEPD_WHITE);
    drawStringBLACK(cachedLoLabelW + 4, NEXT_LINE2_Y, tail, LEFT);
  }
}

void DrawDrivers() {
  bool showGrid = false;
  unsigned int gridRound = 0;
  
  #if DEBUG_MODE
    // Use mode detection to determine what to show
    int currentMode = getCurrentDisplayMode();
    Serial.printf("[DEBUG] DrawDrivers: Current mode = %d\n", currentMode);
    
    if (currentMode == 2 || currentMode == 3) {
      // Mode 2: Race Approaching - show grid for nextRound
      // Mode 3: Race In Progress - show grid until results available
      showGrid = true;
      // For Mode 3, try nextRound first (race just started, still in nextRound)
      if (currentMode == 3) {
        if (nextRound > 0 && qualifyingAvailableForRound(nextRound)) {
          gridRound = nextRound;
          Serial.printf("[DEBUG] DrawDrivers: Mode 3 - using nextRound %u for grid\n", nextRound);
        } else if (lastRound > 0 && qualifyingAvailableForRound(lastRound)) {
          gridRound = lastRound;
          Serial.printf("[DEBUG] DrawDrivers: Mode 3 - using lastRound %u for grid\n", lastRound);
        } else {
          Serial.println(F("[DEBUG] DrawDrivers: Mode 3 - qualifying not available, trying to find correct round"));
          // Even if qualifying not available, try to show grid for the race in progress
          // At race start, the race might still be in nextRound, or might have moved to lastRound
          // Check which one is the current race based on timing
          // Use cached time (optimization)
          time_t nowEpoch = cachedNowEpoch;
          if (nextRaceEpoch > 0) {
            long diffToNext = (long)(nextRaceEpoch - nowEpoch);
            // If we're at or past race start, the race might be in lastRound now
            if (diffToNext <= 0 && lastRound > 0) {
              gridRound = lastRound;
              Serial.printf("[DEBUG] DrawDrivers: Mode 3 - using lastRound %u (race has started)\n", lastRound);
            } else if (nextRound > 0) {
              gridRound = nextRound;
              Serial.printf("[DEBUG] DrawDrivers: Mode 3 - using nextRound %u\n", nextRound);
            }
          } else if (lastRound > 0) {
            gridRound = lastRound;
            Serial.printf("[DEBUG] DrawDrivers: Mode 3 - using lastRound %u (no next race)\n", lastRound);
          }
        }
      } else {
        // Mode 2: Race Approaching
        if (nextRound > 0 && qualifyingAvailableForRound(nextRound)) {
          gridRound = nextRound;
        } else if (lastRound > 0 && qualifyingAvailableForRound(lastRound)) {
          gridRound = lastRound;
        }
      }
    } else {
      // Mode 1 or Mode 4: Show driver standings
      showGrid = false;
      Serial.printf("[DEBUG] DrawDrivers: Mode %d - showing driver standings\n", currentMode);
    }
  #else
    // Production mode logic
    // Check if we should show Starting Grid for the NEXT race (within 18h BEFORE race start)
    if (nextRaceEpoch > 0 && nextRound > 0) {
      // Use cached time (optimization)
      long diff = (long)(nextRaceEpoch - cachedNowEpoch);
      if (diff > 0 && diff <= NEXT_RACE_APPROACHING_SEC && qualifyingAvailableForRound(nextRound)) {
        showGrid = true;
        gridRound = nextRound;
      }
    }
    
    // Check if race is currently in progress (race has started but results not available yet)
    if (!showGrid && isRaceInProgress()) {
      // Check if results are available for CURRENT race (nextRound)
      bool haveResultsForCurrentRace = false;
      if (nextRound > 0) {
        // Check cache for nextRound results specifically
        if (getCachedData(&apiCache.results, CACHE_TTL_STANDINGS)) {
          StaticJsonDocument<512> filter;
          filter["MRData"]["RaceTable"]["Races"][0]["round"] = true;
          filter["MRData"]["RaceTable"]["Races"][0]["Results"][0] = true;
          DeserializationError err = deserializeJson(doc, apiCache.results.data, DeserializationOption::Filter(filter));
          if (!err && doc["MRData"]["RaceTable"]["Races"].size() > 0) {
            unsigned int cachedRound = doc["MRData"]["RaceTable"]["Races"][0]["round"] | 0;
            if (cachedRound == nextRound && doc["MRData"]["RaceTable"]["Races"][0]["Results"].is<JsonArray>()) {
              JsonArray results = doc["MRData"]["RaceTable"]["Races"][0]["Results"].as<JsonArray>();
              haveResultsForCurrentRace = (results.size() >= 3);
            }
          }
        }
      }
      
      // If no results for current race, show grid
      if (!haveResultsForCurrentRace) {
        if (nextRound > 0 && qualifyingAvailableForRound(nextRound)) {
          showGrid = true;
          gridRound = nextRound;
        } else if (lastRound > 0 && qualifyingAvailableForRound(lastRound)) {
          showGrid = true;
          gridRound = lastRound;
        }
      }
    }
  #endif

  gfx.setFont(u8g2_font_helvB08_tf);
  #if DEBUG_MODE
    Serial.printf("[DEBUG] DrawDrivers: Final decision - showGrid=%d, gridRound=%u\n", showGrid, gridRound);
  #endif
  if (showGrid && gridRound > 0) {
    drawStringRED(SCREEN_WIDTH - 5, DRIVERS_TITLE_Y, F("Starting Grid"), RIGHT);
    DrawStartingGrid(gridRound);
    #if DEBUG_MODE
      Serial.printf("[DEBUG] DrawDrivers: Showing grid for round %u\n", gridRound);
    #endif
  } else {
    drawStringRED(308, DRIVERS_TITLE_Y, F("Top 10 Drivers"), RIGHT);
    DrawDriversStandings();
    #if DEBUG_MODE
      Serial.printf("[DEBUG] DrawDrivers: Showing driver standings (showGrid=%d, gridRound=%u)\n", showGrid, gridRound);
    #endif
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
  // Start Serial immediately
  Serial.begin(115200);
  
  // ESP32-C3 USB Serial needs time to initialize
  // Give it time to establish connection
  delay(2000);
  
  // Force output - don't wait for Serial.available() check
  // ESP32-C3 Serial is always "available" even if nothing is connected
  Serial.println();
  Serial.println();
  Serial.println(F("=== F1 TRACKER STARTING ==="));
  Serial.flush();
  delay(100);
  
  Serial.print(F("Free heap: "));
  Serial.println(ESP.getFreeHeap());
  Serial.flush();
  
  Serial.print(F("Chip model: "));
  Serial.println(ESP.getChipModel());
  Serial.flush();
  
  Serial.print(F("Chip revision: "));
  Serial.println(ESP.getChipRevision());
  Serial.flush();
  
  #if DEBUG_MODE
    Serial.println(F("*** DEBUG MODE IS ENABLED ***"));
    Serial.println();
    printDebugMenu();
  #else
    Serial.println(F("Normal mode (DEBUG_MODE = 0)"));
    Serial.println(F("To enable debug mode, set DEBUG_MODE to 1 in code"));
  #endif
  Serial.flush();

  // Initialize SPI and display
  Serial.println(F("[INIT] Starting SPI..."));
  Serial.flush();
  
  SPI.begin(EPDSCK, -1, EPDMOSI, EPDCS);
  SPI.setFrequency(4000000);
  pinMode(EPDBUSY, INPUT);
  pinMode(EPDRST, OUTPUT);
  pinMode(EPDDC,  OUTPUT);
  pinMode(EPDCS,  OUTPUT);

  Serial.println(F("[INIT] Initializing display..."));
  Serial.flush();
  
  display.init();
  Serial.println(F("[INIT] Display initialized"));
  Serial.flush();
  
  display.setRotation(3);
  gfx.begin(display);
  gfx.setFontMode(0);
  gfx.setFontDirection(0);
  gfx.setBackgroundColor(GxEPD_WHITE);
  gfx.setFont(u8g2_font_helvB10_tf);
  display.fillScreen(GxEPD_WHITE);
  display.setFullWindow();
  
  Serial.println(F("[INIT] Display setup complete"));
  Serial.flush();

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

  // Configure time (always sync real time first for NTP)
  configTzTime(MY_TZ, NTP1, NTP2);
  struct tm realTime;
  while (!getLocalTime(&realTime)) { 
    delay(1000); 
    Serial.print("."); 
  }
  Serial.println(F("\n[Time] Synced!"));
  
  // Now get simulated time for logic (in debug mode, this applies offset)
  getSimulatedLocalTime(&timeinfo);
  #if DEBUG_MODE
    Serial.println(F("\n=== DEBUG MODE ENABLED ==="));
    Serial.println(F("Interactive commands available via Serial Monitor:"));
    Serial.println(F("  +1h, +2h, +6h  - Add hours"));
    Serial.println(F("  -1h, -2h, -6h  - Subtract hours"));
    Serial.println(F("  +1d, +2d, +7d  - Add days"));
    Serial.println(F("  -1d, -2d, -7d  - Subtract days"));
    Serial.println(F("  set <sec>      - Set exact offset"));
    Serial.println(F("  reset          - Reset to default"));
    Serial.println(F("Type a number (1-7) or full command"));
    Serial.println(F("Type 'menu' or 'help' to see menu again"));
    Serial.printf("[DEBUG] Current offset: %ld seconds\n", debugTimeOffsetSeconds);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  #endif

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
  lastRound        = prefs.getUInt("lastRound", 0);
  if (lastRound > 0) {
    lastDate = prefs.getString("lastDate", "");
    lastTime = prefs.getString("lastTime", "");
    lastGP   = prefs.getString("lastGP", "");
  }

  // Initial data fetch and display
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    DrawTime();
    FetchCalendar();
    
    // Cache current time for this update cycle (optimization)
    cachedNowEpoch = getSimulatedTime();
    
    // Invalidate cached lastRaceEpoch if lastRound changed
    if (cachedLastRaceRound != lastRound) {
      cachedLastRaceEpoch = 0;
      cachedLastRaceRound = lastRound;
    }
    
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
  // Update current time before drawing (uses simulated time in debug mode)
  getSimulatedLocalTime(&timeinfo);
  
  // Cache current time for this update cycle (optimization)
  cachedNowEpoch = getSimulatedTime();
  
  // Invalidate cached lastRaceEpoch if lastRound changed
  if (cachedLastRaceRound != lastRound) {
    cachedLastRaceEpoch = 0;
    cachedLastRaceRound = lastRound;
  }
  
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
  
  // Use cached time if available, otherwise calculate (optimization)
  time_t now = (cachedNowEpoch > 0) ? cachedNowEpoch : getSimulatedTime();
  
  // Check if last race just finished (within 4 hours) - need to check for results
  // This check must happen even if nextRaceEpoch is 0 (race moved from "next" to "last" in API)
  bool lastRaceJustFinished = false;
  if (lastRound > 0) {
    // Use cached lastRaceEpoch if available (optimization)
    if (cachedLastRaceEpoch == 0 && lastDate.length() > 0) {
      cachedLastRaceEpoch = isoUtcToEpoch(lastDate, lastTime);
    }
    time_t lastRaceStartEpoch = cachedLastRaceEpoch;
    if (lastRaceStartEpoch > 0) {
      long diffSinceLastRace = now - lastRaceStartEpoch;
      lastRaceJustFinished = (diffSinceLastRace >= 0 && diffSinceLastRace <= RACE_IN_PROGRESS_WINDOW_SEC);
      if (lastRaceJustFinished) {
        Serial.printf("[Scheduler] Last race finished %ld seconds ago (within 4h window)\n", diffSinceLastRace);
      }
    } else {
      // Fallback: If we have lastRound but no lastDate/lastTime (calendar not fetched yet),
      // check if the persisted nextEpoch was recently cleared (within last 4 hours)
      // This handles the case where the race just finished and calendar hasn't been fetched yet
      if (nextRaceEpoch == 0) {
        time_t persistedNextEpoch = (time_t)prefs.getULong64("nextEpoch", 0);
        if (persistedNextEpoch > 0) {
          long diffSincePersistedRace = now - persistedNextEpoch;
          // If persisted race was within last 4 hours, it might have just finished
          if (diffSincePersistedRace >= 0 && diffSincePersistedRace <= RACE_IN_PROGRESS_WINDOW_SEC) {
            lastRaceJustFinished = true;
            Serial.printf("[Scheduler] Fallback: Using persisted nextEpoch (cleared %ld seconds ago)\n", diffSincePersistedRace);
          }
        }
      }
    }
  }
  
  // Priority 1: Last race just finished - update every 30 minutes to catch results
  if (lastRaceJustFinished) {
    if (currentMinute % UPDATE_INTERVAL_RACE_WINDOW_MIN == 0) {
      Serial.printf("[Scheduler] Last race just finished - update triggered at minute %d\n", currentMinute);
      return true;
    }
  }
  
  if (nextRaceEpoch == 0) {
    // No next race scheduled
    // But if we're still within 4 hours of last race, continue frequent updates (handled above)
    // Otherwise use normal 2-hour updates
    if (currentMinute == 0 && (timeinfo.tm_hour % 2 == 0)) {
      Serial.printf("[Scheduler] No next race - normal 2h update at hour %d\n", timeinfo.tm_hour);
      return true;
    }
    return false;
  }
  
  long diffToRace = (long)(nextRaceEpoch - now);
  
  // Check if race is in progress (within 4 hours of start) - need frequent updates to catch results
  bool raceInProgress = isRaceInProgress();
  
  // Determine if we're in race window (6h before to 6h after race)
  bool inRaceWindow = isInRaceWindow();
  
  // Priority 2: Race in progress - update every 30 minutes to catch results
  if (raceInProgress) {
    if (currentMinute % UPDATE_INTERVAL_RACE_WINDOW_MIN == 0) {
      Serial.printf("[Scheduler] Race in progress - update triggered at minute %d\n", currentMinute);
      return true;
    }
  }
  
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
  // In debug mode, keep WiFi connected for faster testing
  #if DEBUG_MODE
    // Keep WiFi connected in debug mode for interactive commands
    return;
  #endif
  
  // Disconnect WiFi after updates to save power
  // WiFi will be reconnected automatically when needed
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);  // Disconnect and turn off radio
    WiFi.mode(WIFI_OFF);
    wifiConnected = false;
    Serial.println(F("[WiFi] Disconnected to save power"));
  }
}

// ------------------- DEBUG COMMAND HANDLER -------------------
#if DEBUG_MODE
void printDebugMenu() {
  Serial.println(F("\n=== DEBUG MODE MENU ==="));
  Serial.println(F("1 - Normal (3 days before race)"));
  Serial.println(F("2 - Before (2h before race)"));
  Serial.println(F("3 - During (race start)"));
  Serial.println(F("4 - After (1h after start)"));
  Serial.println(F("5 - Refresh display"));
  Serial.println(F("6 - Show status"));
  Serial.println(F("7 - Reset offset"));
  Serial.println(F("8 - Toggle auto-refresh"));
  Serial.printf("   (Auto-refresh: %s)\n", debugAutoRefresh ? "ON" : "OFF");
  Serial.println(F("\nTime Adjustments:"));
  Serial.println(F("+1h, +2h, +6h  - Add hours"));
  Serial.println(F("-1h, -2h, -6h  - Subtract hours"));
  Serial.println(F("+1d, +2d, +7d  - Add days"));
  Serial.println(F("-1d, -2d, -7d  - Subtract days"));
  Serial.println(F("set <sec>      - Set exact offset"));
  Serial.println(F("\nType number or command, then press Enter"));
  Serial.println();
}

void handleDebugCommands() {
  if (!Serial.available()) return;
  
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  
  if (cmd.length() == 0) return;
  
  // Handle numeric menu selections
  if (cmd.length() == 1 && cmd[0] >= '1' && cmd[0] <= '8') {
    int menuChoice = cmd[0] - '0';
    switch(menuChoice) {
      case 1:
        cmd = "normal";
        break;
      case 2:
        cmd = "before";
        break;
      case 3:
        cmd = "during";
        break;
      case 4:
        cmd = "after";
        break;
      case 5:
        cmd = "refresh";
        break;
      case 6:
        cmd = "status";
        break;
      case 7:
        cmd = "reset";
        break;
      case 8:
        cmd = "autorefresh";
        break;
    }
  }
  
  // Handle "menu" or "help" command
  if (cmd == "menu" || cmd == "help" || cmd == "?") {
    printDebugMenu();
    return;
  }
  
  // Parse hour/day adjustments: +1h, -2h, +1d, -7d
  if (cmd.endsWith("h")) {
    int hours = cmd.substring(0, cmd.length() - 1).toInt();
    debugTimeOffsetSeconds += hours * SECONDS_PER_HOUR;
    Serial.printf("[DEBUG] Adjusted by %d hours. New offset: %ld seconds\n", hours, debugTimeOffsetSeconds);
    getSimulatedLocalTime(&timeinfo);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return;
  }
  
  if (cmd.endsWith("d")) {
    int days = cmd.substring(0, cmd.length() - 1).toInt();
    debugTimeOffsetSeconds += days * SECONDS_PER_DAY;
    Serial.printf("[DEBUG] Adjusted by %d days. New offset: %ld seconds\n", days, debugTimeOffsetSeconds);
    getSimulatedLocalTime(&timeinfo);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return;
  }
  
  // Parse "set <seconds>"
  if (cmd.startsWith("set ")) {
    long newOffset = cmd.substring(4).toInt();
    debugTimeOffsetSeconds = newOffset;
    Serial.printf("[DEBUG] Set offset to %ld seconds\n", debugTimeOffsetSeconds);
    getSimulatedLocalTime(&timeinfo);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return;
  }
  
  // Reset to default
  if (cmd == "reset") {
    debugTimeOffsetSeconds = -2592000;  // -30 days default
    Serial.println(F("[DEBUG] Reset to default offset (-30 days)"));
    getSimulatedLocalTime(&timeinfo);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return;
  }
  
  // Show status
  if (cmd == "status") {
    time_t realNow = time(nullptr);
    time_t simNow = getSimulatedTime();
    getSimulatedLocalTime(&timeinfo);
    
    Serial.println(F("\n=== DEBUG STATUS ==="));
    Serial.printf("Offset: %ld seconds (%.2f days)\n", debugTimeOffsetSeconds, debugTimeOffsetSeconds / 86400.0);
    Serial.printf("Auto-refresh: %s\n", debugAutoRefresh ? "ON" : "OFF");
    
    struct tm realTm = {};
    localtime_r(&realNow, &realTm);
    Serial.printf("Real time:    %04d-%02d-%02d %02d:%02d:%02d\n",
                  realTm.tm_year + 1900, realTm.tm_mon + 1, realTm.tm_mday,
                  realTm.tm_hour, realTm.tm_min, realTm.tm_sec);
    Serial.printf("Simulated:    %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    if (nextRaceEpoch > 0) {
      long diffToRace = (long)(nextRaceEpoch - simNow);
      Serial.printf("Next race:    Round %u, %s\n", nextRound, nextGP.c_str());
      Serial.printf("Time to race: %ld seconds (%.2f hours)\n", diffToRace, diffToRace / 3600.0);
      Serial.printf("Race state:   %s\n", isRaceInProgress() ? "IN PROGRESS" : 
                    (diffToRace > 0 ? "UPCOMING" : "PAST"));
    }
    if (lastRound > 0) {
      Serial.printf("Last race:    Round %u, %s\n", lastRound, lastGP.c_str());
    }
    Serial.println();
    printDebugMenu();
    return;
  }
  
  // Force refresh
  if (cmd == "refresh") {
    Serial.println(F("[DEBUG] Forcing immediate refresh..."));
    Serial.println(F("[DEBUG] Connecting WiFi..."));
    ensureWiFiConnected();
    Serial.println(F("[DEBUG] Fetching calendar..."));
    FetchCalendar();
    Serial.println(F("[DEBUG] Refreshing display (this takes 15-30 seconds)..."));
    fullRefreshAndReschedule();
    disconnectWiFiIfIdle();
    Serial.println(F("[DEBUG] Refresh complete!"));
    printDebugMenu();
    return;
  }
  
  // Jump to scenarios (relative to next race)
  if (cmd == "normal" && nextRaceEpoch > 0) {
    time_t realNow = time(nullptr);
    debugTimeOffsetSeconds = (nextRaceEpoch - 259200) - realNow;  // 3 days before race (normal mode)
    Serial.println(F("[DEBUG] Jumped to 3 days before race (Normal mode)"));
    getSimulatedLocalTime(&timeinfo);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    Serial.println(F("[DEBUG] Connecting WiFi..."));
    ensureWiFiConnected();
    Serial.println(F("[DEBUG] Fetching calendar..."));
    FetchCalendar();
    Serial.println(F("[DEBUG] Refreshing display (this takes 15-30 seconds)..."));
    fullRefreshAndReschedule();
    disconnectWiFiIfIdle();
    Serial.println(F("[DEBUG] Refresh complete!"));
    printDebugMenu();
    return;
  }
  
  if (cmd == "before" && nextRaceEpoch > 0) {
    time_t realNow = time(nullptr);
    debugTimeOffsetSeconds = (nextRaceEpoch - 7200) - realNow;  // 2 hours before race
    Serial.println(F("[DEBUG] Jumped to 2 hours before race (Race Approaching)"));
    getSimulatedLocalTime(&timeinfo);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    Serial.println(F("[DEBUG] Connecting WiFi..."));
    ensureWiFiConnected();
    Serial.println(F("[DEBUG] Fetching calendar..."));
    FetchCalendar();
    Serial.println(F("[DEBUG] Refreshing display (this takes 15-30 seconds)..."));
    fullRefreshAndReschedule();
    disconnectWiFiIfIdle();
    Serial.println(F("[DEBUG] Refresh complete!"));
    printDebugMenu();
    return;
  }
  
  if (cmd == "during" && nextRaceEpoch > 0) {
    time_t realNow = time(nullptr);
    debugTimeOffsetSeconds = nextRaceEpoch - realNow;  // At race start
    Serial.println(F("[DEBUG] Jumped to race start time (Race In Progress)"));
    getSimulatedLocalTime(&timeinfo);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    Serial.println(F("[DEBUG] Connecting WiFi..."));
    ensureWiFiConnected();
    Serial.println(F("[DEBUG] Fetching calendar..."));
    FetchCalendar();
    Serial.println(F("[DEBUG] Refreshing display (this takes 15-30 seconds)..."));
    fullRefreshAndReschedule();
    disconnectWiFiIfIdle();
    Serial.println(F("[DEBUG] Refresh complete!"));
    printDebugMenu();
    return;
  }
  
  if (cmd == "after" && nextRaceEpoch > 0) {
    time_t realNow = time(nullptr);
    // Jump to 5 hours after race start to ensure we're past the race window
    // This ensures Mode 4 is detected (requires >4h past race and not in race window)
    debugTimeOffsetSeconds = (nextRaceEpoch + 18000) - realNow;  // 5 hours after race start
    Serial.println(F("[DEBUG] Jumped to 5 hours after race start (Post-Race)"));
    getSimulatedLocalTime(&timeinfo);
    Serial.printf("[DEBUG] Simulated time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    Serial.println(F("[DEBUG] Connecting WiFi..."));
    ensureWiFiConnected();
    Serial.println(F("[DEBUG] Fetching calendar..."));
    FetchCalendar();
    Serial.println(F("[DEBUG] Refreshing display (this takes 15-30 seconds)..."));
    fullRefreshAndReschedule();
    disconnectWiFiIfIdle();
    Serial.println(F("[DEBUG] Refresh complete!"));
    printDebugMenu();
    return;
  }
  
  // Toggle auto-refresh
  if (cmd == "autorefresh" || cmd == "auto") {
    debugAutoRefresh = !debugAutoRefresh;
    Serial.printf("[DEBUG] Auto-refresh %s\n", debugAutoRefresh ? "ENABLED" : "DISABLED");
    if (!debugAutoRefresh) {
      Serial.println(F("[DEBUG] Scheduler updates disabled. Use 'refresh' or mode commands (1-4) to update manually."));
    } else {
      Serial.println(F("[DEBUG] Scheduler updates enabled. Display will auto-refresh on schedule."));
    }
    printDebugMenu();
    return;
  }
  
  if ((cmd == "normal" || cmd == "before" || cmd == "during" || cmd == "after") && nextRaceEpoch == 0) {
    Serial.println(F("[DEBUG] No race detected. Run 'refresh' first to fetch calendar."));
    return;
  }
  
  // Unknown command
  Serial.println(F("[DEBUG] Unknown command."));
  printDebugMenu();
}
#endif

// ------------------- LOOP -------------------
void loop() {
  // Get current time (doesn't require WiFi) - uses simulated time in debug mode
  if (!getSimulatedLocalTime(&timeinfo)) {
    // Time sync failed - reconnect WiFi and retry
    ensureWiFiConnected();
    if (!getSimulatedLocalTime(&timeinfo)) {
      delay(1000);
      return;
    }
  }
  
  int currentMinute = timeinfo.tm_min;
  
  // Check if it's time to update (skip auto-updates in debug mode if disabled)
  #if DEBUG_MODE
    bool shouldAutoUpdate = debugAutoRefresh && shouldUpdateNow(currentMinute);
  #else
    bool shouldAutoUpdate = shouldUpdateNow(currentMinute);
  #endif
  
  if (shouldAutoUpdate) {
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
  // In debug mode, check for commands every second for responsive interaction
  #if DEBUG_MODE
    // Check for commands every second in debug mode (60 checks = 60 seconds)
    for (int i = 0; i < 60; i++) {
      handleDebugCommands();  // Check for commands every second
      delay(1000);
    }
  #else
    delay(60000);
  #endif
}
