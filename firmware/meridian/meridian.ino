/*
  Meridian Screen
  Waveshare ESP32-S3-Touch-LCD-7, 8MB flash, 8MB PSRAM, 800x480.
  Weather, planes, quakes, and the night sky, on one screen.
  Prefs name merid. The first boot may copy Wi-Fi and place from ultwx, skywx, or astro7.
  It does not clear those. A factory reset does not copy them again.
  LCD reset is pulsed, and that wait finishes before the RGB clock.
  Downloads read 1024 bytes, then wait 1 ms.
  Taps run when the finger lifts.
  Version is MERID_VER. CHECK reads MERID_VER_URL. INSTALL reads MERID_BIN_URL.
  Leave that address out of the manual and the listing.
*/

#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include <Preferences.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <TJpg_Decoder.h>
#include "stars.h"
#include "rocks.h"
#include "sats.h"
#include "iss_world.h"
#include "world_map.h"
#include "usa_map.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PW 800
#define PH 480
#define RAIL 96
#define X0 112
#define TOUCH_SDA 8
#define TOUCH_SCL 9
#define TOUCH_INT 4
#define GT911_ADDR 0x5D
#define CH422_WR_SET 0x24
#define CH422_WR_IO 0x38
#define EXIO_TP_RST 1
#define EXIO_LCD_BL 2
#define EXIO_LCD_RST 3
#define EXIO_SD_CS 4
#define EXIO_USB_SEL 5
#define MERID_VER "1.1"
#define MERID_VER_URL "https://raw.githubusercontent.com/stefi01/weather-plane-quakes-alerts-7-inch-waveshare-esp32-s3-screen/main/ota/version.txt"
#define MERID_BIN_URL "https://raw.githubusercontent.com/stefi01/weather-plane-quakes-alerts-7-inch-waveshare-esp32-s3-screen/main/ota/meridian.bin"
#define DEG 0.01745329251994
#define MISS -999

enum {
  PG_NOW = 0, PG_WEEK, PG_AIR, PG_SKY, PG_MORE,
  PG_PLACE, PG_WIFI, PG_SETUP, PG_ABOUT, PG_QUAKE, PG_LAUNCH,
  PG_STARS, PG_TONIGHT, PG_PLANETS, PG_MOON, PG_ROCKS, PG_SATS, PG_ISS, PG_METEOR, PG_QFACT
};
enum {
  NET_NONE = 0, NET_PACK, NET_AQI, NET_ALERT, NET_FIND,
  NET_PLANE, NET_QUAKE, NET_ISS, NET_LAUNCH, NET_KP, NET_TIDE, NET_OTA
};

struct Hit { char label[64]; float lat, lon; };
struct Plane {
  char call[12];
  char hex[8];
  char kind[20];
  int alt, gs, track;
  float lat, lon, dist;
  bool hasPos, gnd;
};
struct Quake {
  float mag, depth, lat, lon;
  int miles;
  time_t when;
  char place[48];
};
struct Launch { char name[44]; char when[24]; char where[40]; };
struct ZoneRow { const char *iana; const char *posix; };

static const ZoneRow ZONES[] = {
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Detroit", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Kentucky/Louisville", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Kentucky/Monticello", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Indiana/Indianapolis", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Menominee", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Boise", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "MST7"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Juneau", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Nome", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Adak", "HST10HDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu", "HST10"},
    {"America/Puerto_Rico", "AST4"},
    {"Pacific/Guam", "ChST-10"},
};
static const char *MON[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *WDAY[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *RAIL_NAME[] = {"NOW", "WEEK", "AIR", "SKY", "MORE"};
static const char *PLANET_NAME[] = {"MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"};
static const double PEL[5][11] = {
    {48.3313, 3.24587e-5, 7.0047, 5.00e-8, 29.1241, 1.01444e-5, 0.387098, 0.205635, 5.59e-10, 168.6562, 4.0923344368},
    {76.6799, 2.46590e-5, 3.3946, 2.75e-8, 54.8910, 1.38374e-5, 0.723330, 0.006773, -1.302e-9, 48.0052, 1.6021302244},
    {49.5574, 2.11081e-5, 1.8497, -1.78e-8, 286.5016, 2.92961e-5, 1.523688, 0.093405, 2.516e-9, 18.6021, 0.5240207766},
    {100.4542, 2.76854e-5, 1.3030, -1.557e-7, 273.8777, 1.64505e-5, 5.20256, 0.048498, 4.469e-9, 19.8950, 0.0830853001},
    {113.6634, 2.38980e-5, 2.4886, -1.081e-7, 339.3939, 2.97661e-5, 9.55475, 0.055546, -9.499e-9, 316.9670, 0.0334442282},
};
static const int16_t COAST[] = {
    72, -160, 68, -166, 60, -148, 55, -133, 48, -125, 40, -124, 32, -117, 23, -110, 16, -95, 21, -87, 26, -82, 30, -84, 36, -76, 42, -70, 47, -60, 53, -56, 60, -64, 68, -80, 72, -100, 70, -140, 72, -160, 127,
    15, -92, 10, -84, 8, -77, 0, -50, -5, -35, -15, -39, -23, -42, -35, -58, -50, -68, -55, -68, -40, -64, -22, -70, -18, -70, -5, -80, 8, -77, 15, -92, 127,
    36, -10, 43, -9, 48, -5, 51, 2, 54, 8, 58, 12, 62, 5, 60, 5, 56, 8, 52, 4, 46, -2, 43, 3, 38, -1, 36, -6, 36, -10, 127,
    32, -10, 28, -13, 20, -17, 14, -17, 5, 2, 4, 9, 0, 10, -10, 14, -25, 15, -34, 20, -35, 19, -30, 30, -22, 15, -10, 14, 5, 12, 12, 43, 10, 45, 0, 42, -15, 40, -25, 35, -34, 26, -30, 18, 32, -10, 127,
    12, 44, 18, 56, 22, 70, 8, 78, 6, 80, 15, 74, 22, 88, 16, 95, 8, 98, 1, 104, -6, 106, -8, 115, 0, 118, 5, 108, 15, 100, 22, 92, 16, 80, 12, 44, 127,
    -12, 130, -20, 120, -32, 116, -35, 137, -38, 145, -32, 152, -25, 153, -16, 145, -12, 136, -12, 130, 127,
    -128,
};

Arduino_ESP32RGBPanel *rgbBus = nullptr;
Arduino_RGB_Display *gfx = nullptr;
uint8_t ch422Io = 0;
SemaphoreHandle_t i2cLock = nullptr;
SemaphoreHandle_t dataMux = nullptr;
SemaphoreHandle_t jobMux = nullptr;
Preferences prefs;
char *packBuf = nullptr;

uint16_t COL_BG, COL_PANEL, COL_INK, COL_MUTE, COL_HOT, COL_SEA, COL_CORAL, COL_LINE, COL_ON;

volatile int page = PG_NOW;
int blLevel = 2;
bool dayMode = false;
bool useC = false;
volatile bool wifiOk = false;
bool placeSet = false;
float placeLat = 0, placeLon = 0;
char placeName[64] = "";
char placeTz[48] = "";
char placeQuery[64] = "";
char placeMsg[28] = "";
char wifiSsid[33] = "";
char wifiPass[65] = "";
char wifiMsg[20] = "";
Hit hits[4];
int nHits = 0;

bool wxOk = false;
int wxTemp = MISS, wxFeel = MISS, wxRh = MISS, wxWind = MISS, wxDir = 0, wxCode = MISS, wxUv = MISS, wxAqi = -1;
float pressHpa = 0, pressBase = 0, rainToday = 0;
time_t pressStamp = 0;
int pressTrend = 0;
int hourT[24], hourPop[24], hourCode[24], nHour = 0;
int dayHi[7], dayLo[7], dayCode[7], dayPop[7], dayWday[7], nDay = 0;
float dayRain[7];
char sunUp[16] = "", sunDown[16] = "";
bool alertOn = false;
char alertEvent[40] = "", alertEnds[28] = "";

Plane planes[12];
int nPlane = 0;
bool planesOk = false;
int rangeNm = 40;
int planePick = -1;

Quake quakes[24];
int nQuake = 0;
bool quakeOk = false;
int quakePin = -1;
int quakeBack = PG_NOW;

Launch launches[4];
int nLaunch = 0;
bool launchOk = false;

float issLat = 0, issLon = 0, issAlt = 0, issVel = 0;
bool issOk = false;
float kp = -1;
bool kpOk = false;
bool quakeUsa = false;
bool quakePhoto = false;
bool pinAlerts = true;
bool pinOn = false;
char pinText[56] = "";
char pinSeen[56] = "";
bool showTide = true;
char tideId[12] = "";
char tideName[24] = "";
char tideHigh[28] = "";
char tideLow[28] = "";
bool tideKnown = false;
bool tideDone = false;
float tideBestKm = 99999.0f;
double satEp[3] = {0, 0, 0};
double satN[3] = {0, 0, 0};
double satEcc[3] = {0, 0, 0};
double satInc[3] = {0, 0, 0};
double satRaan[3] = {0, 0, 0};
double satArg[3] = {0, 0, 0};
double satM0[3] = {0, 0, 0};
bool satOrbOk[3] = {false, false, false};
float issTrackLat[64];
float issTrackLon[64];
int nIssTrack = 0;
float tgTrackLat[64];
float tgTrackLon[64];
int nTgTrack = 0;
float tgLat = 0, tgLon = 0, tgAlt = 0;
bool tgOk = false, tgVisNow = false;
char issPassLine[6][56];
int nIssPass = 0;
int issCrewN = -1, tgCrewN = -1;
uint32_t lastCrew = 0;
bool issVisNow = false;
int issAzNow = 0, issElNow = -90;
float sunLat = 0, sunLon = 0;
bool sunMapOk = false;
uint32_t lastPassBuild = 0, lastTle = 0, lastTide = 0;
int jpgMode = 0;
uint16_t *issPx = nullptr;
bool issPicOk = false;
char upName[14][20];
int upEl[14], upAz[14], nUp = 0;

char otaMsg[48] = "";
char otaLatest[16] = "";
volatile int otaPct = -1;
volatile bool otaBusy = false;
bool otaNewer = false;

volatile bool needDraw = true;
volatile bool netPaint = false;
volatile bool netBusy = false;
bool paintFresh = true;
volatile bool wifiBusy = false;
int shownMin = -99;
bool clockWas = false;
int jobs[12];
int nJob = 0;
int focus = 1;
bool caps = false;
bool symbols = false;
bool joining = false;
bool scanOn = false;
int nScan = 0, scanTop = 0;
char scanName[8][33];
int keyX[48], keyY[48], keyW[48], keyH[48], keyCode[48];
char keyCh[48];
int nKeys = 0;
uint32_t lastPack = 0, lastPlane = 0, lastQuake = 0, lastIss = 0, lastLaunch = 0, lastKp = 0, lastOtaDraw = 0;

volatile int blDuty = 10;
TaskHandle_t blTask = nullptr;

struct I2cHold {
  I2cHold() { if (i2cLock) xSemaphoreTake(i2cLock, portMAX_DELAY); }
  ~I2cHold() { if (i2cLock) xSemaphoreGive(i2cLock); }
};

void cols() {
  if (dayMode) {
    COL_BG = RGB565(244, 236, 224);
    COL_PANEL = RGB565(255, 250, 244);
    COL_INK = RGB565(42, 32, 40);
    COL_MUTE = RGB565(120, 104, 96);
    COL_HOT = RGB565(186, 84, 28);
    COL_SEA = RGB565(18, 122, 112);
    COL_CORAL = RGB565(176, 48, 42);
    COL_LINE = RGB565(210, 196, 180);
    COL_ON = RGB565(255, 248, 240);
  } else {
    COL_BG = RGB565(22, 18, 32);
    COL_PANEL = RGB565(42, 34, 58);
    COL_INK = RGB565(236, 230, 220);
    COL_MUTE = RGB565(158, 148, 168);
    COL_HOT = RGB565(255, 176, 96);
    COL_SEA = RGB565(110, 214, 186);
    COL_CORAL = RGB565(255, 110, 92);
    COL_LINE = RGB565(78, 66, 102);
    COL_ON = RGB565(42, 28, 24);
  }
}

void centre(const char *s, int cx, int y, int sz) {
  int w = (int)strlen(s) * 6 * sz;
  gfx->setTextSize(sz);
  gfx->setCursor(cx - w / 2, y);
  gfx->print(s);
}

void rightText(const char *s, int right, int y, int sz) {
  int w = (int)strlen(s) * 6 * sz;
  gfx->setTextSize(sz);
  gfx->setCursor(right - w, y);
  gfx->print(s);
}

void chip(int x, int y, int w, int h, const char *t, bool on) {
  gfx->fillRoundRect(x, y, w, h, 8, on ? COL_HOT : COL_PANEL);
  gfx->drawRoundRect(x, y, w, h, 8, on ? COL_HOT : COL_LINE);
  gfx->setTextColor(on ? COL_ON : COL_INK, on ? COL_HOT : COL_PANEL);
  int sz = 2;
  if ((int)strlen(t) * 12 > w - 16) sz = 1;
  centre(t, x + w / 2, y + (h - 8 * sz) / 2, sz);
}

void ch422Write(uint8_t addr, uint8_t val) {
  I2cHold hold;
  Wire.beginTransmission(addr);
  Wire.write(val);
  Wire.endTransmission();
}

void ch422Flush() { ch422Write(CH422_WR_IO, ch422Io); }

void ch422Bit(int bit, bool on) {
  I2cHold hold;
  if (on) ch422Io |= (uint8_t)(1u << bit);
  else ch422Io &= (uint8_t)~(1u << bit);
  Wire.beginTransmission(CH422_WR_IO);
  Wire.write(ch422Io);
  Wire.endTransmission();
}

void boardIoBegin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  ch422Write(CH422_WR_SET, 0x01);
  delay(2);
  ch422Io = (uint8_t)((1u << EXIO_TP_RST) | (1u << EXIO_LCD_RST) | (1u << EXIO_SD_CS));
  ch422Flush();
  pinMode(TOUCH_INT, OUTPUT);
  digitalWrite(TOUCH_INT, LOW);
  ch422Bit(EXIO_TP_RST, false);
  delay(10);
  ch422Bit(EXIO_TP_RST, true);
  delay(50);
  pinMode(TOUCH_INT, INPUT);
  ch422Bit(EXIO_LCD_RST, false);
  delay(20);
  ch422Bit(EXIO_LCD_RST, true);
  delay(120);
}

void blPwm(void *) {
  for (;;) {
    int duty = blDuty;
    if (duty >= 10) {
      ch422Bit(EXIO_LCD_BL, true);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (duty < 1) duty = 1;
    ch422Bit(EXIO_LCD_BL, true);
    vTaskDelay(pdMS_TO_TICKS(duty));
    if (blDuty >= 10) continue;
    ch422Bit(EXIO_LCD_BL, false);
    vTaskDelay(pdMS_TO_TICKS(10 - duty));
  }
}

void applyBl() {
  if (blLevel <= 0) blDuty = 3;
  else if (blLevel == 1) blDuty = 6;
  else blDuty = 10;
  if (blDuty >= 10) ch422Bit(EXIO_LCD_BL, true);
  if (!blTask) xTaskCreatePinnedToCore(blPwm, "blpwm", 4096, nullptr, 2, &blTask, 0);
}

void applyPlaceTz() {
  if (!placeTz[0]) return;
  setenv("TZ", placeTz, 1);
  tzset();
}

void adoptOffset(int offsetSec) {
  int h = -(offsetSec / 3600);
  if (h > 14) h = 14;
  if (h < -12) h = -12;
  snprintf(placeTz, sizeof(placeTz), "UTC%d", h);
  applyPlaceTz();
}

void adoptLon() {
  const char *tz = "UTC0";
  float la = placeLat, lo = placeLon;
  if (la >= 51.0f && la < 72.0f && lo >= -170.0f && lo < -130.0f) tz = "AKST9AKDT,M3.2.0,M11.1.0";
  else if (la >= 18.0f && la < 23.0f && lo >= -161.0f && lo < -154.0f) tz = "HST10";
  else if (lo <= -115.0f && lo > -130.0f && la < 51.0f) tz = "PST8PDT,M3.2.0,M11.1.0";
  else if (la >= 31.3f && la <= 37.0f && lo >= -114.9f && lo <= -109.0f) tz = "MST7";
  else if (lo <= -102.5f && la >= 25.0f) tz = "MST7MDT,M3.2.0,M11.1.0";
  else if (lo <= -86.6f && la >= 24.0f) tz = "CST6CDT,M3.2.0,M11.1.0";
  else if (lo > -130.0f && lo <= -67.0f && la >= 24.0f && la < 72.0f) tz = "EST5EDT,M3.2.0,M11.1.0";
  else {
    int h = (int)lroundf(-lo / 15.0f);
    if (h > 12) h = 12;
    if (h < -12) h = -12;
    snprintf(placeTz, sizeof(placeTz), "UTC%d", h);
    applyPlaceTz();
    return;
  }
  strlcpy(placeTz, tz, sizeof(placeTz));
  applyPlaceTz();
}

void adoptZone(const char *iana, int offsetSec) {
  if (iana && iana[0]) {
    for (int i = 0; i < (int)(sizeof(ZONES) / sizeof(ZONES[0])); i++) {
      if (!strcmp(ZONES[i].iana, iana)) {
        strlcpy(placeTz, ZONES[i].posix, sizeof(placeTz));
        applyPlaceTz();
        return;
      }
    }
  }
  if (placeLat > 17 && placeLat < 72 && placeLon < -65 && placeLon > -170) {
    adoptLon();
    return;
  }
  if (offsetSec > -50000 && offsetSec < 50000) adoptOffset(offsetSec);
}

bool liveClock(time_t *out) {
  if (!placeTz[0]) return false;
  time_t n = time(nullptr);
  if (n < 1700000000) return false;
  *out = n;
  return true;
}

void fmtClock(char *out, int n) {
  time_t now;
  if (!liveClock(&now)) {
    strlcpy(out, "--:--", n);
    return;
  }
  struct tm t;
  localtime_r(&now, &t);
  int h = t.tm_hour % 12;
  if (h == 0) h = 12;
  snprintf(out, n, "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "am" : "pm");
}

void fmtDate(char *out, int n) {
  time_t now;
  if (!liveClock(&now)) {
    out[0] = 0;
    return;
  }
  struct tm t;
  localtime_r(&now, &t);
  snprintf(out, n, "%s  %s %d", WDAY[t.tm_wday], MON[t.tm_mon], t.tm_mday);
}

int ymdWday(int y, int m, int d) {
  static const int tt[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + tt[m - 1] + d) % 7;
}

void clockFromIso(const char *iso, char *out, int n) {
  const char *t = strchr(iso, 'T');
  if (!t || strlen(t) < 6) {
    out[0] = 0;
    return;
  }
  int hh = atoi(t + 1);
  int mm = atoi(t + 4);
  int h = hh % 12;
  if (h == 0) h = 12;
  snprintf(out, n, "%d:%02d %s", h, mm, hh < 12 ? "am" : "pm");
}

int showDeg(int f) {
  if (!useC) return f;
  return (int)lroundf((f - 32) * 5.0f / 9.0f);
}

int showWind(int mph) {
  if (!useC) return mph;
  return (int)lroundf(mph * 1.60934f);
}

const char *windUnit() { return useC ? "km/h" : "mph"; }

const char *dirName(int deg) {
  static const char *n[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int i = ((deg + 22) / 45) & 7;
  return n[i];
}

const char *lookWord(int az) {
  static const char *d[] = {"north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"};
  if (az < 0) az += 360;
  return d[((az + 22) / 45) & 7];
}

const char *wxWord(int code) {
  if (code < 0) return "waiting";
  if (code == 0) return "clear";
  if (code == 1) return "mainly clear";
  if (code == 2) return "partly cloudy";
  if (code == 3) return "cloudy";
  if (code == 45 || code == 48) return "fog";
  if (code >= 51 && code <= 67) return "rain";
  if (code >= 71 && code <= 77) return "snow";
  if (code >= 80 && code <= 82) return "showers";
  if (code == 85 || code == 86) return "snow showers";
  if (code >= 95) return "storm";
  return "cloud";
}

const char *kpWord(float k) {
  if (k < 2) return "quiet";
  if (k < 4) return "unsettled";
  if (k < 5) return "active";
  if (k < 6) return "minor storm";
  if (k < 8) return "storm";
  return "severe";
}

double wrap360(double a) {
  a = fmod(a, 360.0);
  if (a < 0) a += 360.0;
  return a;
}

void eclToRaDec(double xg, double yg, double zg, double ecl, double *ra, double *dec) {
  double xe = xg;
  double ye = yg * cos(ecl) - zg * sin(ecl);
  double ze = yg * sin(ecl) + zg * cos(ecl);
  *ra = atan2(ye, xe);
  if (*ra < 0) *ra += 2 * M_PI;
  *dec = atan2(ze, hypot(xe, ye));
}

void sunPos(double d, double *ra, double *dec, double *lon, double *rs) {
  double M = wrap360(356.0470 + 0.9856002585 * d) * DEG;
  double w = (282.9404 + 4.70935e-5 * d) * DEG;
  double e = 0.016709 - 1.151e-9 * d;
  double E = M + e * sin(M) * (1.0 + e * cos(M));
  double xv = cos(E) - e;
  double yv = sqrt(1.0 - e * e) * sin(E);
  double v = atan2(yv, xv);
  double r = hypot(xv, yv);
  double lonr = v + w;
  *lon = lonr;
  *rs = r;
  double ecl = (23.4393 - 3.563e-7 * d) * DEG;
  eclToRaDec(r * cos(lonr), r * sin(lonr), 0, ecl, ra, dec);
}

void planetPos(int which, double d, double sunLon, double sunR, double *ra, double *dec) {
  const double *e = PEL[which];
  double N = (e[0] + e[1] * d) * DEG;
  double i = (e[2] + e[3] * d) * DEG;
  double w = (e[4] + e[5] * d) * DEG;
  double a = e[6];
  double ec = e[7] + e[8] * d;
  double M = wrap360(e[9] + e[10] * d) * DEG;
  double E = M + ec * sin(M) * (1.0 + ec * cos(M));
  double xv = a * (cos(E) - ec);
  double yv = a * (sqrt(1.0 - ec * ec) * sin(E));
  double v = atan2(yv, xv);
  double r = hypot(xv, yv);
  double vw = v + w;
  double xh = r * (cos(N) * cos(vw) - sin(N) * sin(vw) * cos(i));
  double yh = r * (sin(N) * cos(vw) + cos(N) * sin(vw) * cos(i));
  double zh = r * (sin(vw) * sin(i));
  double ecl = (23.4393 - 3.563e-7 * d) * DEG;
  eclToRaDec(xh + sunR * cos(sunLon), yh + sunR * sin(sunLon), zh, ecl, ra, dec);
}

void moonPos(double d, double *ra, double *dec, double *lon) {
  double N = (125.1228 - 0.0529538083 * d) * DEG;
  double i = 5.1454 * DEG;
  double w = (318.0634 + 0.1643573223 * d) * DEG;
  double a = 60.2666;
  double ec = 0.054900;
  double M = wrap360(115.3654 + 13.0649929509 * d) * DEG;
  double E = M + ec * sin(M) * (1.0 + ec * cos(M));
  double xv = a * (cos(E) - ec);
  double yv = a * (sqrt(1.0 - ec * ec) * sin(E));
  double v = atan2(yv, xv);
  double r = hypot(xv, yv);
  double vw = v + w;
  double xh = r * (cos(N) * cos(vw) - sin(N) * sin(vw) * cos(i));
  double yh = r * (sin(N) * cos(vw) + cos(N) * sin(vw) * cos(i));
  double zh = r * (sin(vw) * sin(i));
  *lon = atan2(yh, xh);
  double ecl = (23.4393 - 3.563e-7 * d) * DEG;
  eclToRaDec(xh, yh, zh, ecl, ra, dec);
}

void altAz(double ra, double dec, time_t t, int *azDeg, int *elDeg) {
  double d = (2440587.5 + (double)t / 86400.0) - 2451545.0;
  double lst = wrap360(280.46061837 + 360.98564736629 * d + placeLon) * DEG;
  double ha = lst - ra;
  double lat = placeLat * DEG;
  double sinAlt = sin(dec) * sin(lat) + cos(dec) * cos(lat) * cos(ha);
  if (sinAlt > 1) sinAlt = 1;
  if (sinAlt < -1) sinAlt = -1;
  double alt = asin(sinAlt);
  double cosAlt = cos(alt);
  double saz = 0, caz = 1;
  if (fabs(cosAlt) > 1e-4) {
    saz = -sin(ha) * cos(dec) / cosAlt;
    caz = (sin(dec) - sin(alt) * sin(lat)) / (cosAlt * cos(lat));
  }
  double az = atan2(saz, caz);
  if (az < 0) az += 2 * M_PI;
  *azDeg = (int)lround(az * 180.0 / M_PI) % 360;
  *elDeg = (int)lround(alt * 180.0 / M_PI);
}

float distKm(float la1, float lo1, float la2, float lo2) {
  double a1 = la1 * DEG, a2 = la2 * DEG;
  double dla = (la2 - la1) * DEG, dlo = (lo2 - lo1) * DEG;
  double h = sin(dla / 2) * sin(dla / 2) + cos(a1) * cos(a2) * sin(dlo / 2) * sin(dlo / 2);
  if (h > 1) h = 1;
  return (float)(2 * 6371.0 * asin(sqrt(h)));
}

float bearing(float la1, float lo1, float la2, float lo2) {
  double a1 = la1 * DEG, a2 = la2 * DEG, dlo = (lo2 - lo1) * DEG;
  double y = sin(dlo) * cos(a2);
  double x = cos(a1) * sin(a2) - sin(a1) * cos(a2) * cos(dlo);
  double b = atan2(y, x);
  if (b < 0) b += 2 * M_PI;
  return (float)b;
}

void savePrefs() {
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  prefs.begin("merid", false);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putBool("set", placeSet);
  prefs.putFloat("lat", placeLat);
  prefs.putFloat("lon", placeLon);
  prefs.putString("name", placeName);
  prefs.putString("tzp", placeTz);
  prefs.putInt("bl", blLevel);
  prefs.putBool("day", dayMode);
  prefs.putBool("c", useC);
  prefs.putInt("nm", rangeNm);
  prefs.putBool("pin", pinAlerts);
  prefs.putString("seen", pinSeen);
  prefs.putBool("tideon", showTide);
  prefs.putString("tide", tideId);
  prefs.putString("tname", tideName);
  prefs.putFloat("pbase", pressBase);
  prefs.putInt("pstamp", (int)pressStamp);
  prefs.end();
  if (dataMux) xSemaphoreGive(dataMux);
}

void markOwn() {
  prefs.begin("meridx", false);
  prefs.putBool("own", true);
  prefs.end();
}

void borrowOne(const char *ns, bool takePlace) {
  if (!prefs.begin(ns, true)) return;
  if (!wifiSsid[0]) {
    String ssid = prefs.getString("ssid", "");
    if (ssid.length()) {
      strlcpy(wifiSsid, ssid.c_str(), sizeof(wifiSsid));
      strlcpy(wifiPass, prefs.getString("pass", "").c_str(), sizeof(wifiPass));
    }
  }
  if (takePlace && !placeSet) {
    String name = prefs.getString("name", "");
    if (prefs.getBool("set", false) && name.length()) {
      placeSet = true;
      placeLat = prefs.getFloat("lat", 0);
      placeLon = prefs.getFloat("lon", 0);
      strlcpy(placeName, name.c_str(), sizeof(placeName));
      strlcpy(placeTz, prefs.getString("tzp", "").c_str(), sizeof(placeTz));
    }
  }
  prefs.end();
}

void loadPrefs() {
  prefs.begin("meridx", true);
  bool own = prefs.getBool("own", false);
  prefs.end();
  prefs.begin("merid", true);
  strlcpy(wifiSsid, prefs.getString("ssid", "").c_str(), sizeof(wifiSsid));
  strlcpy(wifiPass, prefs.getString("pass", "").c_str(), sizeof(wifiPass));
  placeSet = prefs.getBool("set", false);
  placeLat = prefs.getFloat("lat", 0);
  placeLon = prefs.getFloat("lon", 0);
  strlcpy(placeName, prefs.getString("name", "").c_str(), sizeof(placeName));
  strlcpy(placeTz, prefs.getString("tzp", "").c_str(), sizeof(placeTz));
  blLevel = prefs.getInt("bl", 2);
  dayMode = prefs.getBool("day", false);
  useC = prefs.getBool("c", false);
  rangeNm = prefs.getInt("nm", 40);
  pinAlerts = prefs.getBool("pin", true);
  strlcpy(pinSeen, prefs.getString("seen", "").c_str(), sizeof(pinSeen));
  showTide = prefs.getBool("tideon", true);
  strlcpy(tideId, prefs.getString("tide", "").c_str(), sizeof(tideId));
  strlcpy(tideName, prefs.getString("tname", "").c_str(), sizeof(tideName));
  tideKnown = tideId[0] != 0;
  pressBase = prefs.getFloat("pbase", 0);
  pressStamp = (time_t)prefs.getInt("pstamp", 0);
  prefs.end();
  if (!placeName[0]) placeSet = false;
  if (blLevel < 0 || blLevel > 2) blLevel = 2;
  if (rangeNm < 5 || rangeNm > 150) rangeNm = 40;
  if (!own) {
    borrowOne("ultwx", true);
    borrowOne("skywx", true);
    borrowOne("astro7", true);
    borrowOne("scope", false);
    markOwn();
    savePrefs();
  }
  applyPlaceTz();
  cols();
}

void factoryReset() {
  markOwn();
  prefs.begin("merid", false);
  prefs.clear();
  prefs.end();
  ESP.restart();
}

bool quietPage() { return page == PG_WIFI || page == PG_PLACE; }

void askNet(int job) {
  if (!job || !jobMux) return;
  xSemaphoreTake(jobMux, portMAX_DELAY);
  bool have = false;
  for (int i = 0; i < nJob; i++)
    if (jobs[i] == job) have = true;
  if (!have && nJob < 12) jobs[nJob++] = job;
  xSemaphoreGive(jobMux);
}

int takeJob(bool quiet) {
  if (!jobMux) return 0;
  xSemaphoreTake(jobMux, portMAX_DELAY);
  int job = 0;
  if (quiet) {
    for (int i = 0; i < nJob; i++) {
      if (jobs[i] == NET_FIND) {
        job = NET_FIND;
        for (int k = i; k < nJob - 1; k++) jobs[k] = jobs[k + 1];
        nJob--;
        break;
      }
    }
  } else if (nJob) {
    job = jobs[0];
    for (int k = 0; k < nJob - 1; k++) jobs[k] = jobs[k + 1];
    nJob--;
  }
  xSemaphoreGive(jobMux);
  return job;
}

void noteLinked() {
  uint32_t t = millis();
  lastPack = lastPlane = lastQuake = lastIss = lastLaunch = lastKp = t;
  if (!placeSet) return;
  askNet(NET_PACK);
  askNet(NET_AQI);
  askNet(NET_ALERT);
  askNet(NET_PLANE);
  askNet(NET_QUAKE);
  askNet(NET_ISS);
  askNet(NET_LAUNCH);
  askNet(NET_KP);
  if (showTide) askNet(NET_TIDE);
}

int railIndex() {
  if (page == PG_NOW) return 0;
  if (page == PG_WEEK) return 1;
  if (page == PG_AIR) return 2;
  if (page == PG_SKY || page == PG_STARS || page == PG_TONIGHT || page == PG_PLANETS || page == PG_MOON || page == PG_ROCKS || page == PG_SATS || page == PG_ISS || page == PG_METEOR) return 3;
  return 4;
}

void pageClear() {
  if (paintFresh) gfx->fillScreen(COL_BG);
}

void drawRail() {
  int on = railIndex();
  for (int i = 0; i < 5; i++) {
    int y = i * 96;
    gfx->fillRect(0, y, RAIL, 96, i == on ? COL_PANEL : COL_BG);
    if (i == on) gfx->fillRect(0, y + 18, 8, 60, COL_HOT);
    gfx->setTextColor(i == on ? COL_HOT : COL_MUTE, i == on ? COL_PANEL : COL_BG);
    centre(RAIL_NAME[i], 52, y + 40, 2);
  }
  gfx->fillRect(RAIL, 0, 2, PH, COL_LINE);
}

void drawBack(const char *title) {
  chip(X0, 8, 132, 36, "< BACK", false);
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(X0 + 148, 16);
  gfx->print(title);
}

void drawWxIcon(int cx, int cy, int code) {
  if (code < 0) return;
  bool storm = code >= 95;
  bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || storm;
  bool cloud = code >= 2;
  if (code <= 2) gfx->fillCircle(cx - 8, cy + 8, 6, COL_HOT);
  if (cloud || rain || snow || code == 45) {
    gfx->fillCircle(cx - 2, cy + 2, 8, COL_MUTE);
    gfx->fillCircle(cx + 8, cy + 4, 6, COL_MUTE);
    gfx->fillRect(cx - 8, cy + 4, 24, 8, COL_MUTE);
  }
  if (rain) {
    gfx->drawLine(cx - 6, cy + 16, cx - 10, cy + 24, COL_SEA);
    gfx->drawLine(cx + 2, cy + 16, cx - 2, cy + 24, COL_SEA);
  }
  if (snow) {
    gfx->fillCircle(cx - 6, cy + 20, 2, COL_INK);
    gfx->fillCircle(cx + 6, cy + 20, 2, COL_INK);
  }
  if (storm) {
    gfx->drawLine(cx + 10, cy + 14, cx + 4, cy + 22, COL_HOT);
    gfx->drawLine(cx + 4, cy + 22, cx + 12, cy + 22, COL_HOT);
    gfx->drawLine(cx + 12, cy + 22, cx + 6, cy + 30, COL_HOT);
  }
}

void drawMoonPhase(int cx, int cy, int r, float phase) {
  uint16_t lit = RGB565(236, 232, 214);
  uint16_t dark = dayMode ? RGB565(70, 78, 96) : RGB565(28, 32, 48);
  gfx->fillCircle(cx, cy, r, dark);
  if (phase >= 0 && phase <= 1) {
    float k = cosf(phase * 2.0f * (float)M_PI);
    for (int dy = -r; dy <= r; dy++) {
      int span = r * r - dy * dy;
      if (span <= 0) continue;
      int xe = (int)sqrtf((float)span);
      int x1, x2;
      if (phase < 0.5f) {
        x1 = (int)(k * (float)xe);
        x2 = xe;
      } else {
        x1 = -xe;
        x2 = (int)(-k * (float)xe);
      }
      if (x2 > x1) gfx->drawFastHLine(cx + x1, cy + dy, x2 - x1, lit);
    }
  }
  gfx->drawCircle(cx, cy, r, COL_LINE);
}

int clockMin(const char *s) {
  int h = 0, m = 0;
  char ap = 'a';
  if (!s || sscanf(s, "%d:%d %c", &h, &m, &ap) < 2) return -1;
  if (ap == 'P' || ap == 'p') h = h % 12 + 12;
  else h = h % 12;
  return h * 60 + m;
}

void drawSunArc(int x, int y, int w, int h) {
  int cx = x + w / 2;
  int base = y + h - 16;
  int rad = w / 2 - 10;
  if (rad > h - 28) rad = h - 28;
  if (rad < 18) rad = 18;
  for (int deg = 0; deg <= 180; deg += 8) {
    float a = (180 - deg) * (float)M_PI / 180.0f;
    int px = cx + (int)(cosf(a) * rad);
    int py = base - (int)(sinf(a) * rad);
    gfx->fillCircle(px, py, (deg % 40 == 0) ? 2 : 1, COL_LINE);
  }
  int rise = clockMin(sunUp);
  int setm = clockMin(sunDown);
  time_t now;
  int nowMin = 12 * 60;
  if (liveClock(&now)) {
    struct tm t;
    localtime_r(&now, &t);
    nowMin = t.tm_hour * 60 + t.tm_min;
  }
  float frac = 0.5f;
  if (rise >= 0 && setm > rise) {
    if (nowMin <= rise) frac = 0;
    else if (nowMin >= setm) frac = 1;
    else frac = (float)(nowMin - rise) / (float)(setm - rise);
  }
  bool onArc = !(rise >= 0 && setm > rise) || (nowMin >= rise && nowMin < setm);
  if (onArc) {
    float sa = (180.0f - frac * 180.0f) * (float)M_PI / 180.0f;
    int sx = cx + (int)(cosf(sa) * rad);
    int sy = base - (int)(sinf(sa) * rad);
    gfx->fillCircle(sx, sy, 6, COL_HOT);
  }
  gfx->setTextColor(COL_MUTE, COL_PANEL);
  gfx->setTextSize(1);
  gfx->setCursor(x + 2, base + 2);
  gfx->print(sunUp[0] ? sunUp : "--");
  rightText(sunDown[0] ? sunDown : "--", x + w - 2, base + 2, 1);
}

void drawSpark(int x, int y, int w, int h) {
  gfx->fillRoundRect(x, y, w, h, 12, COL_PANEL);
  int sunW = w >= 400 ? 156 : 0;
  int plotW = w - sunW;
  gfx->setTextColor(COL_HOT, COL_PANEL);
  gfx->setTextSize(2);
  gfx->setCursor(x + 14, y + 8);
  gfx->print("NEXT 24 HOURS");
  if (pressHpa > 0) {
    char p[28], u[16];
    if (!useC) snprintf(u, sizeof(u), "%.2f in", pressHpa / 33.8639f);
    else snprintf(u, sizeof(u), "%.0f hPa", pressHpa);
    const char *tr = pressTrend > 0 ? "rising" : (pressTrend < 0 ? "falling" : "steady");
    snprintf(p, sizeof(p), "%s  %s", u, tr);
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->setTextSize(1);
    gfx->setCursor(x + 14, y + 28);
    gfx->print(p);
  }
  if (sunW) drawSunArc(x + plotW + 4, y + 8, sunW - 12, h - 16);
  if (nHour < 2) {
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(x + 14, y + 58);
    gfx->print(placeSet ? "waiting" : "set a place");
    return;
  }
  int minT = hourT[0], maxT = hourT[0];
  for (int i = 1; i < nHour; i++) {
    if (hourT[i] < minT) minT = hourT[i];
    if (hourT[i] > maxT) maxT = hourT[i];
  }
  if (maxT <= minT) maxT = minT + 1;
  int x0 = x + 18, y0 = y + 44, iw = plotW - 36, ih = h - 66;
  int prevx = 0, prevy = 0;
  for (int i = 0; i < nHour; i++) {
    int px = x0 + (nHour == 1 ? 0 : i * iw / (nHour - 1));
    int py = y0 + ih - (hourT[i] - minT) * ih / (maxT - minT);
    if (i) gfx->drawLine(prevx, prevy, px, py, COL_SEA);
    gfx->fillCircle(px, py, nHour > 16 ? 2 : 3, COL_HOT);
    if (hourPop[i] >= 40) {
      uint16_t c = hourPop[i] >= 70 ? COL_CORAL : COL_SEA;
      gfx->drawLine(px, y + h - 14, px, y + h - 8, c);
    }
    prevx = px;
    prevy = py;
  }
}

void drawStat(int x, int y, int w, int h, const char *title, const char *big, const char *sub) {
  if (paintFresh) gfx->fillRoundRect(x, y, w, h, 12, COL_PANEL);
  gfx->setTextColor(COL_MUTE, COL_PANEL);
  gfx->setTextSize(2);
  gfx->setCursor(x + 10, y + 8);
  gfx->print(title);
  gfx->setTextColor(COL_INK, COL_PANEL);
  gfx->setCursor(x + 10, y + 30);
  gfx->print(big);
  gfx->setTextColor(COL_SEA, COL_PANEL);
  gfx->setCursor(x + 10, y + 52);
  gfx->print(sub);
}

void drawNow() {
  pageClear();
  drawRail();
  char clk[16], date[24], name[64];
  fmtClock(clk, sizeof(clk));
  fmtDate(date, sizeof(date));
  strlcpy(name, placeSet ? placeName : "SET PLACE", sizeof(name));
  int nameSz = 2;
  if ((int)strlen(name) * 12 > 500) nameSz = 1;
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(nameSz);
  gfx->setCursor(X0, nameSz == 2 ? 10 : 14);
  gfx->print(name);
  if (date[0]) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(X0, 32);
    gfx->print(date);
  }
  gfx->setTextColor(COL_INK, COL_BG);
  gfx->fillRect(630, 8, 162, 34, COL_BG);
  rightText(clk, 788, 14, 3);

  if (paintFresh) gfx->fillRoundRect(X0, 64, 240, 220, 14, COL_PANEL);
  else gfx->fillRect(X0 + 12, 100, 210, 150, COL_PANEL);
  gfx->setTextColor(COL_MUTE, COL_PANEL);
  gfx->setTextSize(2);
  gfx->setCursor(X0 + 16, 76);
  gfx->print("NOW");
  char big[8];
  if (wxOk && wxTemp != MISS) snprintf(big, sizeof(big), "%d", showDeg(wxTemp));
  else strlcpy(big, "--", sizeof(big));
  int sz = strlen(big) >= 3 ? 6 : 7;
  gfx->setTextColor(COL_INK, COL_PANEL);
  gfx->setTextSize(sz);
  gfx->setCursor(X0 + 16, 108);
  gfx->print(big);
  gfx->setTextColor(COL_HOT, COL_PANEL);
  gfx->setTextSize(3);
  gfx->setCursor(X0 + 20 + (int)strlen(big) * 6 * sz, 120);
  gfx->print(useC ? "C" : "F");
  gfx->setTextColor(COL_INK, COL_PANEL);
  gfx->setTextSize(2);
  gfx->setCursor(X0 + 16, 176);
  gfx->print(wxOk ? wxWord(wxCode) : (placeSet ? "waiting" : "city or ZIP"));
  if (wxOk && wxFeel != MISS) {
    char fl[24];
    snprintf(fl, sizeof(fl), "feels %d", showDeg(wxFeel));
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->setCursor(X0 + 16, 202);
    gfx->print(fl);
  }
  if (wxUv >= 0) {
    char uv[16];
    snprintf(uv, sizeof(uv), "UV %d", wxUv);
    gfx->setTextColor(COL_HOT, COL_PANEL);
    gfx->setCursor(X0 + 16, 228);
    gfx->print(uv);
  }
  if (!wifiOk) chip(X0 + 16, 248, 200, 28, "WI-FI", true);
  else if (!placeSet) chip(X0 + 16, 248, 200, 28, "CITY OR ZIP", true);

  drawSpark(364, 64, 424, 136);
  char b1[20], s1[20], b2[20], s2[20], b3[20], s3[20];
  if (wxOk && wxWind != MISS) {
    snprintf(b1, sizeof(b1), "%d %s", showWind(wxWind), windUnit());
    snprintf(s1, sizeof(s1), "%s", dirName(wxDir));
  } else {
    strlcpy(b1, "--", sizeof(b1));
    strlcpy(s1, "", sizeof(s1));
  }
  if (wxOk && wxRh != MISS) snprintf(b2, sizeof(b2), "%d%%", wxRh);
  else strlcpy(b2, "--", sizeof(b2));
  if (wxAqi >= 0) snprintf(s2, sizeof(s2), "aqi %d", wxAqi);
  else strlcpy(s2, "aqi --", sizeof(s2));
  if (wxOk) {
    if (!useC) snprintf(b3, sizeof(b3), "%.2f in", rainToday);
    else snprintf(b3, sizeof(b3), "%.1f mm", rainToday * 25.4f);
    if (nDay > 0 && dayPop[0] >= 0) snprintf(s3, sizeof(s3), "%d%% today", dayPop[0]);
    else strlcpy(s3, "today", sizeof(s3));
  } else {
    strlcpy(b3, "--", sizeof(b3));
    strlcpy(s3, "", sizeof(s3));
  }
  drawStat(364, 208, 136, 76, "WIND", b1, s1);
  drawStat(508, 208, 136, 76, "HUMID", b2, s2);
  drawStat(652, 208, 136, 76, "RAIN", b3, s3);

  if (paintFresh) gfx->fillRoundRect(X0, 296, 676, 112, 12, COL_PANEL);
  int shown = nDay < 6 ? nDay : 6;
  for (int i = 0; i < 6; i++) {
    int cx = X0 + 56 + i * 110;
    gfx->setTextSize(2);
    if (i < shown) {
      gfx->setTextColor(COL_MUTE, COL_PANEL);
      int wd = dayWday[i];
      if (wd < 0 || wd > 6) wd = 0;
      centre(WDAY[wd], cx, 306, 2);
      gfx->fillRect(cx - 24, 312, 48, 46, COL_PANEL);
      drawWxIcon(cx, 328, dayCode[i]);
      char hl[8];
      gfx->setTextColor(COL_CORAL, COL_PANEL);
      snprintf(hl, sizeof(hl), "%d", showDeg(dayHi[i]));
      centre(hl, cx, 364, 2);
      gfx->setTextColor(COL_SEA, COL_PANEL);
      snprintf(hl, sizeof(hl), "%d", showDeg(dayLo[i]));
      centre(hl, cx, 384, 2);
    }
  }
  if (!shown) {
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->setCursor(X0 + 16, 340);
    gfx->print(placeSet ? "forecast waiting" : "set a place");
  }

  char alarm[56] = "";
  bool alarmOn = false;
  quakePin = -1;
  if (alertOn && alertEvent[0]) {
    if (alertEnds[0]) snprintf(alarm, sizeof(alarm), "%s  until %s", alertEvent, alertEnds);
    else snprintf(alarm, sizeof(alarm), "%s", alertEvent);
    alarmOn = true;
  } else if (quakeOk && nQuake > 0) {
    int near = -1, nearMi = 10000;
    for (int qi = 0; qi < nQuake; qi++) {
      if (quakes[qi].miles >= 0 && quakes[qi].miles < 200 && quakes[qi].miles < nearMi) {
        near = qi;
        nearMi = quakes[qi].miles;
      }
    }
    if (near >= 0) {
      snprintf(alarm, sizeof(alarm), "quake M%.1f   %d mi", quakes[near].mag, quakes[near].miles);
      alarmOn = true;
      quakePin = near;
    }
  }
  if (pinAlerts && alarmOn && !(pinSeen[0] && !strcmp(alarm, pinSeen))) {
    if (!pinOn || strcmp(pinText, alarm)) {
      strlcpy(pinText, alarm, sizeof(pinText));
      pinOn = true;
    }
  }
  bool showingPin = pinAlerts && pinOn && pinText[0];
  bool showQuake = (showingPin && !strncmp(pinText, "quake", 5)) || (!pinAlerts && alarmOn && !strncmp(alarm, "quake", 5));
  if (!showQuake) quakePin = -1;
  gfx->fillRoundRect(X0, 418, 676, 52, 12, COL_PANEL);
  gfx->setTextSize(2);
  char line[56];
  if (!placeSet) {
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    strlcpy(line, "Type a city or ZIP", sizeof(line));
  } else if (!wifiOk) {
    gfx->setTextColor(COL_CORAL, COL_PANEL);
    strlcpy(line, "Join Wi-Fi", sizeof(line));
  } else if (showingPin) {
    gfx->setTextColor(COL_CORAL, COL_PANEL);
    strlcpy(line, pinText, sizeof(line));
  } else if (!pinAlerts && alarmOn) {
    gfx->setTextColor(COL_CORAL, COL_PANEL);
    strlcpy(line, alarm, sizeof(line));
  } else if (wxOk && wxCode >= 45) {
    gfx->setTextColor(COL_INK, COL_PANEL);
    strlcpy(line, "NO WARNINGS", sizeof(line));
  } else {
    gfx->setTextColor(COL_SEA, COL_PANEL);
    strlcpy(line, "ALL CLEAR", sizeof(line));
  }
  int cap = showingPin ? 28 : 48;
  if ((int)strlen(line) > cap) line[cap] = 0;
  gfx->setCursor(X0 + 16, showTide || showingPin ? 424 : 434);
  gfx->print(line);
  if (showingPin) chip(X0 + 520, 424, 140, 36, "CLEAR", true);
  if (showTide) {
    gfx->setTextSize(1);
    gfx->setTextColor(COL_SEA, COL_PANEL);
    gfx->setCursor(X0 + 16, 448);
    if (tideHigh[0] || tideLow[0]) {
      char row[56];
      snprintf(row, sizeof(row), "%s    %s", tideHigh, tideLow);
      gfx->print(row);
    } else gfx->print(tideDone && !tideKnown ? "no coast tide" : "tide waiting");
  }
}

void drawWeek() {
  pageClear();
  drawRail();
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(X0, 14);
  gfx->print("7 DAYS");
  if (placeName[0]) {
    int nameSz = ((int)strlen(placeName) * 12 > 540) ? 1 : 2;
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setTextSize(nameSz);
    gfx->setCursor(X0 + 110, nameSz == 2 ? 14 : 18);
    gfx->print(placeName);
  }
  for (int i = 0; i < 7; i++) {
    int y = 48 + i * 60;
    if (paintFresh) gfx->fillRoundRect(X0, y, 676, 54, 10, COL_PANEL);
    gfx->setTextSize(2);
    if (i < nDay) {
      char left[16], hi[12], lo[12], pop[12], rain[16];
      int wd = dayWday[i];
      if (wd < 0 || wd > 6) wd = 0;
      snprintf(left, sizeof(left), "%s", WDAY[wd]);
      gfx->setTextColor(COL_HOT, COL_PANEL);
      gfx->setCursor(X0 + 12, y + 8);
      gfx->print(left);
      gfx->setTextColor(COL_INK, COL_PANEL);
      gfx->setCursor(X0 + 12, y + 30);
      gfx->print(wxWord(dayCode[i]));
      gfx->fillRect(X0 + 220, y + 6, 70, 42, COL_PANEL);
      drawWxIcon(X0 + 250, y + 24, dayCode[i]);
      snprintf(hi, sizeof(hi), "%d", showDeg(dayHi[i]));
      snprintf(lo, sizeof(lo), "%d", showDeg(dayLo[i]));
      gfx->setTextColor(COL_CORAL, COL_PANEL);
      centre(hi, X0 + 360, y + 18, 2);
      gfx->setTextColor(COL_SEA, COL_PANEL);
      centre(lo, X0 + 430, y + 18, 2);
      if (dayPop[i] >= 0) snprintf(pop, sizeof(pop), "%d%%", dayPop[i]);
      else strlcpy(pop, "--", sizeof(pop));
      gfx->setTextColor(COL_INK, COL_PANEL);
      centre(pop, X0 + 520, y + 18, 2);
      if (!useC) snprintf(rain, sizeof(rain), "%.2f in", dayRain[i]);
      else snprintf(rain, sizeof(rain), "%.1f mm", dayRain[i] * 25.4f);
      gfx->setTextColor(COL_MUTE, COL_PANEL);
      rightText(rain, X0 + 660, y + 18, 2);
    } else if (i == 0) {
      gfx->setTextColor(COL_MUTE, COL_PANEL);
      gfx->setCursor(X0 + 12, y + 18);
      gfx->print(placeSet ? "waiting" : "set a place");
    }
  }
}

void drawAir() {
  pageClear();
  drawRail();
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(X0, 14);
  gfx->print("PLANES");
  const int ranges[] = {20, 40, 80};
  for (int i = 0; i < 3; i++) {
    char lab[8];
    snprintf(lab, sizeof(lab), "%d", ranges[i]);
    chip(500 + i * 96, 8, 88, 36, lab, rangeNm == ranges[i]);
  }
  int shown = nPlane < 8 ? nPlane : 8;
  for (int i = 0; i < shown; i++) {
    int y = 56 + i * 46;
    gfx->fillRoundRect(X0, y, 360, 42, 8, COL_PANEL);
    if (i == planePick) gfx->drawRoundRect(X0, y, 360, 42, 8, COL_CORAL);
    gfx->setTextSize(2);
    gfx->setTextColor(COL_INK, COL_PANEL);
    gfx->setCursor(X0 + 10, y + 4);
    gfx->print(planes[i].call[0] ? planes[i].call : planes[i].hex);
    char sub[32], alt[16];
    if (planes[i].gnd) strlcpy(alt, "GND", sizeof(alt));
    else if (planes[i].alt > 0) snprintf(alt, sizeof(alt), "%d ft", planes[i].alt);
    else strlcpy(alt, "--", sizeof(alt));
    snprintf(sub, sizeof(sub), "%s   %d kt", alt, planes[i].gs);
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->setCursor(X0 + 10, y + 22);
    gfx->print(sub);
    if (planes[i].hasPos) {
      char d[12];
      snprintf(d, sizeof(d), "%.0f nm", planes[i].dist);
      gfx->setTextColor(COL_SEA, COL_PANEL);
      rightText(d, X0 + 348, y + 12, 2);
    }
  }
  if (!shown) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setCursor(X0, 80);
    gfx->print(!placeSet ? "set a place" : (!planesOk ? "waiting" : "quiet sky"));
  }
  int cx = 640, cy = 270, R = 150;
  gfx->fillCircle(cx, cy, R, COL_PANEL);
  gfx->drawCircle(cx, cy, R, COL_LINE);
  gfx->drawCircle(cx, cy, R * 2 / 3, COL_LINE);
  gfx->drawCircle(cx, cy, R / 3, COL_LINE);
  gfx->drawLine(cx - R, cy, cx + R, cy, COL_LINE);
  gfx->drawLine(cx, cy - R, cx, cy + R, COL_LINE);
  gfx->setTextColor(COL_HOT, COL_PANEL);
  centre("N", cx, cy - R + 8, 2);
  centre("S", cx, cy + R - 22, 2);
  centre("E", cx + R - 18, cy - 8, 2);
  centre("W", cx - R + 8, cy - 8, 2);
  gfx->fillCircle(cx, cy, 4, COL_CORAL);
  for (int i = 0; i < nPlane; i++) {
    if (!planes[i].hasPos || rangeNm <= 0) continue;
    float frac = planes[i].dist / (float)rangeNm;
    if (frac > 1) frac = 1;
    float brg = bearing(placeLat, placeLon, planes[i].lat, planes[i].lon);
    int px = cx + (int)(sinf(brg) * frac * (R - 14));
    int py = cy - (int)(cosf(brg) * frac * (R - 14));
    gfx->fillCircle(px, py, i == planePick ? 8 : 5, i == planePick ? COL_CORAL : COL_HOT);
    float hd = planes[i].track * DEG;
    gfx->drawLine(px, py, px + (int)(sinf(hd) * 14), py - (int)(cosf(hd) * 14), COL_SEA);
  }
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setTextSize(1);
  gfx->setCursor(X0, 436);
  if (planePick >= 0 && planePick < nPlane) {
    char alt[16], fact[96];
    if (planes[planePick].gnd) strlcpy(alt, "GND", sizeof(alt));
    else if (planes[planePick].alt > 0) snprintf(alt, sizeof(alt), "%d ft", planes[planePick].alt);
    else strlcpy(alt, "--", sizeof(alt));
    snprintf(fact, sizeof(fact), "type %s   flight %s   %d kt   %s",
             planes[planePick].kind[0] ? planes[planePick].kind : "--",
             planes[planePick].call[0] ? planes[planePick].call : (planes[planePick].hex[0] ? planes[planePick].hex : "--"),
             planes[planePick].gs, alt);
    gfx->print(fact);
  } else gfx->print("tap a plane");
  gfx->setCursor(X0, 462);
  gfx->print("not for navigation    range in nautical miles    height in feet    speed in knots");
}

void skyProject(int cx, int cy, int R, double ra, double dec, time_t now, int *px, int *py, int *el) {
  int az = 0, eld = -90;
  altAz(ra, dec, now, &az, &eld);
  *el = eld;
  if (eld <= 0) return;
  float rr = (90.0f - eld) / 90.0f * (float)(R - 8);
  float azr = az * DEG;
  *px = cx + (int)(sinf(azr) * rr);
  *py = cy - (int)(cosf(azr) * rr);
}

bool jpgBlit(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (jpgMode == 1) {
    if (!issPx) return 0;
    for (int row = 0; row < (int)h; row++) {
      int yy = (int)y + row;
      if (yy < 0 || yy >= 119) continue;
      for (int col = 0; col < (int)w; col++) {
        int xx = (int)x + col;
        if (xx < 0 || xx >= 320) continue;
        issPx[yy * 320 + xx] = bitmap[row * (int)w + col];
      }
    }
    return 1;
  }
  if (!gfx || y >= PH) return 0;
  gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
  return 1;
}

bool showJpg(int x, int y, const uint8_t *jpg, uint32_t len) {
  jpgMode = 0;
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(jpgBlit);
  TJpgDec.setSwapBytes(false);
  if (TJpgDec.drawJpg(x, y, jpg, len) == 0) return true;
  TJpgDec.setSwapBytes(true);
  bool ok = TJpgDec.drawJpg(x, y, jpg, len) == 0;
  TJpgDec.setSwapBytes(false);
  return ok;
}

void ensureIssPic() {
  if (issPicOk) return;
  if (!issPx) issPx = (uint16_t *)heap_caps_malloc((size_t)320 * 119 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!issPx) return;
  jpgMode = 1;
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(jpgBlit);
  TJpgDec.setSwapBytes(false);
  bool ok = TJpgDec.drawJpg(0, 0, ISS_WORLD_JPG, ISS_WORLD_JPG_LEN) == 0;
  if (!ok) {
    TJpgDec.setSwapBytes(true);
    ok = TJpgDec.drawJpg(0, 0, ISS_WORLD_JPG, ISS_WORLD_JPG_LEN) == 0;
  }
  jpgMode = 0;
  TJpgDec.setSwapBytes(false);
  issPicOk = ok;
}

void paintIss(int x0, int y0, int bw, int bh) {
  if (!issPicOk || !issPx || bw < 8 || bh < 8) return;
  uint16_t *row = (uint16_t *)malloc((size_t)bw * 2);
  if (!row) return;
  for (int j = 0; j < bh; j++) {
    int sy = j * 119 / bh;
    if (sy > 118) sy = 118;
    for (int i = 0; i < bw; i++) {
      int sx = i * 320 / bw;
      if (sx > 319) sx = 319;
      uint16_t c = issPx[sy * 320 + sx];
      if (sunMapOk) {
        float lat = 72.0f - (float)sy * 134.0f / 119.0f;
        float lon = (float)sx * 360.0f / 320.0f - 180.0f;
        float alt = sinf(lat * DEG) * sinf(sunLat * DEG) + cosf(lat * DEG) * cosf(sunLat * DEG) * cosf((lon - sunLon) * DEG);
        if (alt < 0) {
          int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
          c = (uint16_t)((r / 3) << 11 | (g / 3) << 5 | (b / 3));
        }
      }
      row[i] = c;
    }
    gfx->draw16bitRGBBitmap(x0, y0 + j, row, bw, 1);
    if ((j & 15) == 15) delay(0);
  }
  free(row);
}

bool worldDot(float lat, float lon, int y0, int bh, int *ox, int *oy) {
  if (lat > 72.0f || lat < -62.0f || lon < -180.0f || lon > 180.0f) return false;
  *ox = (int)((lon + 180.0f) * (800.0f / 360.0f));
  *oy = y0 + (int)((72.0f - lat) * ((float)bh / 134.0f));
  if (*ox < 2) *ox = 2;
  if (*ox > 797) *ox = 797;
  return *oy >= y0 + 2 && *oy < y0 + bh - 2;
}

bool usaDot(float lat, float lon, int *sx, int *sy) {
  float ix, iy;
  if (lat >= 51.0f && lat <= 71.5f && lon >= -170.0f && lon <= -129.0f) {
    ix = 16.0f + (lon + 170.0f) / 41.0f * 152.0f;
    iy = 16.0f + (71.5f - lat) / 20.5f * 112.0f;
  } else if (lat >= 18.7f && lat <= 22.4f && lon >= -160.3f && lon <= -154.7f) {
    ix = 16.0f + (lon + 160.3f) / 5.6f * 152.0f;
    iy = 156.0f + (22.4f - lat) / 3.7f * 108.0f;
  } else if (lat >= 24.2f && lat <= 49.6f && lon >= -125.0f && lon <= -66.5f) {
    ix = 188.0f + (lon + 125.0f) / 58.5f * 604.0f;
    iy = 8.0f + (49.6f - lat) / 25.4f * 264.0f;
  } else return false;
  int x = (int)ix;
  int y = 52 + (int)iy;
  if (x < 4 || x >= PW - 4 || y < 56 || y >= 332) return false;
  *sx = x;
  *sy = y;
  return true;
}

void addUp(const char *name, int az, int el) {
  if (!name || el <= 0) return;
  if (nUp >= 14 && el <= upEl[13]) return;
  int at = nUp < 14 ? nUp : 13;
  for (int i = 0; i < nUp && i < 14; i++) {
    if (el > upEl[i]) {
      at = i;
      break;
    }
  }
  int last = nUp < 14 ? nUp : 13;
  for (int i = last; i > at; i--) {
    strlcpy(upName[i], upName[i - 1], 20);
    upEl[i] = upEl[i - 1];
    upAz[i] = upAz[i - 1];
  }
  strlcpy(upName[at], name, 20);
  upEl[at] = el;
  upAz[at] = az;
  if (nUp < 14) nUp++;
}

void rockPos(int which, double d, double sunLon, double sunR, double *ra, double *dec) {
  double dt = d - (ROCKS[which].epochJd - 2451545.0);
  double n = 0.9856076686 / pow(ROCKS[which].a, 1.5);
  double N = ROCKS[which].om * DEG;
  double inc = ROCKS[which].inc * DEG;
  double w = ROCKS[which].w * DEG;
  double a = ROCKS[which].a;
  double ec = ROCKS[which].e;
  double M = wrap360(ROCKS[which].ma + n * dt) * DEG;
  double E = M + ec * sin(M) * (1.0 + ec * cos(M));
  double xv = a * (cos(E) - ec);
  double yv = a * (sqrt(1.0 - ec * ec) * sin(E));
  double v = atan2(yv, xv);
  double r = hypot(xv, yv);
  double vw = v + w;
  double xh = r * (cos(N) * cos(vw) - sin(N) * sin(vw) * cos(inc));
  double yh = r * (sin(N) * cos(vw) + cos(N) * sin(vw) * cos(inc));
  double zh = r * (sin(vw) * sin(inc));
  double ecl = (23.4393 - 3.563e-7 * d) * DEG;
  eclToRaDec(xh + sunR * cos(sunLon), yh + sunR * sin(sunLon), zh, ecl, ra, dec);
}

int starNamed(int name) {
  if (name < 0) return -1;
  int n = (int)(sizeof(STARS) / sizeof(STARS[0]));
  for (int i = 0; i < n; i++)
    if ((int)STARS[i].name == name) return i;
  return -1;
}

void starRad(int i, double *ra, double *dec) {
  *ra = (STARS[i].ra / 100.0) * 15.0 * DEG;
  *dec = (STARS[i].dec / 10.0) * DEG;
}

bool issLook(int *az, int *el) {
  if (!issOk || !placeSet) return false;
  double la1 = placeLat * DEG, lo1 = placeLon * DEG;
  double la = issLat * DEG, lo = issLon * DEG, dlo = lo - lo1;
  double by = sin(dlo) * cos(la);
  double bx = cos(la1) * sin(la) - sin(la1) * cos(la) * cos(dlo);
  double azr = atan2(by, bx);
  if (azr < 0) azr += 2 * M_PI;
  double cosg = sin(la1) * sin(la) + cos(la1) * cos(la) * cos(dlo);
  if (cosg > 1) cosg = 1;
  if (cosg < -1) cosg = -1;
  double gamma = acos(cosg);
  double elr = atan2(cos(gamma) - 6371.0 / (6371.0 + issAlt), sin(gamma));
  *az = (int)lround(azr * 180.0 / M_PI);
  *el = (int)lround(elr * 180.0 / M_PI);
  return true;
}

void skyTile(int x, int y, const char *title, const char *hint) {
  gfx->fillRoundRect(x, y, 336, 96, 12, COL_PANEL);
  gfx->drawRoundRect(x, y, 336, 96, 12, COL_LINE);
  gfx->setTextColor(COL_HOT, COL_PANEL);
  gfx->setTextSize(2);
  gfx->setCursor(x + 16, y + 18);
  gfx->print(title);
  gfx->setTextColor(COL_MUTE, COL_PANEL);
  gfx->setCursor(x + 16, y + 52);
  gfx->print(hint);
}

void drawSky() {
  pageClear();
  drawRail();
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(X0, 12);
  gfx->print("SKY");
  const char *title[] = {"STAR MAP", "TONIGHT", "PLANETS", "MOON", "ROCKS", "SATELLITES", "ISS WORLD", "METEORS"};
  const char *hint[] = {"constellations", "what is up", "moon and five planets", "moon phase", "asteroids, comets", "space station", "see it pass", "yearly showers"};
  for (int i = 0; i < 8; i++) skyTile(X0 + (i % 2) * 348, 48 + (i / 2) * 106, title[i], hint[i]);
}

void drawStars() {
  pageClear();
  drawRail();
  drawBack("STAR MAP");
  int cx = 300, cy = 268, R = 168;
  uint16_t well = dayMode ? RGB565(186, 206, 214) : RGB565(12, 16, 36);
  gfx->fillCircle(cx, cy, R, well);
  gfx->drawCircle(cx, cy, R, COL_LINE);
  gfx->drawCircle(cx, cy, R * 2 / 3, COL_LINE);
  gfx->drawCircle(cx, cy, R / 3, COL_LINE);
  gfx->setTextColor(COL_HOT, well);
  centre("N", cx, cy - R + 8, 2);
  centre("S", cx, cy + R - 26, 2);
  gfx->setTextColor(COL_MUTE, well);
  centre("E", cx + R - 22, cy - 8, 2);
  centre("W", cx - R + 8, cy - 8, 2);

  time_t now = 0;
  bool have = liveClock(&now);
  int sunEl = -90;
  if (have && placeSet) {
    double d = (2440587.5 + (double)now / 86400.0) - 2451545.0;
    double sra, sdec, slon, srs, mra, mdec, mlon;
    sunPos(d, &sra, &sdec, &slon, &srs);
    moonPos(d, &mra, &mdec, &mlon);
    int px, py, el;
    skyProject(cx, cy, R, sra, sdec, now, &px, &py, &el);
    sunEl = el;
    if (el > 0) gfx->fillCircle(px, py, 8, COL_HOT);
    skyProject(cx, cy, R, mra, mdec, now, &px, &py, &el);
    if (el > 0) {
      gfx->fillCircle(px, py, 6, RGB565(236, 232, 214));
      gfx->setTextColor(COL_HOT, well);
      gfx->setTextSize(1);
      gfx->setCursor(px + 8, py - 4);
      gfx->print("moon");
    }
    static const char *plab[] = {"mercury", "venus", "mars", "jupiter", "saturn"};
    for (int i = 0; i < 5; i++) {
      double ra, dec;
      planetPos(i, d, slon, srs, &ra, &dec);
      skyProject(cx, cy, R, ra, dec, now, &px, &py, &el);
      if (el > 0) {
        gfx->fillCircle(px, py, 4, COL_SEA);
        gfx->setTextColor(COL_SEA, well);
        gfx->setTextSize(1);
        gfx->setCursor(px + 6, py - 4);
        gfx->print(plab[i]);
      }
    }
    if (sunEl <= 0) {
      int nStar = (int)(sizeof(STARS) / sizeof(STARS[0]));
      int nName = (int)(sizeof(STAR_NAME) / sizeof(STAR_NAME[0]));
      int nLine = (int)(sizeof(STAR_LINE) / sizeof(STAR_LINE[0]));
      uint16_t link = dayMode ? RGB565(90, 110, 130) : RGB565(110, 130, 170);
      for (int i = 0; i + 1 < nLine && STAR_LINE[i] != 255; i += 2) {
        if (STAR_LINE[i + 1] == 255) break;
        int a = starNamed(STAR_LINE[i]);
        int b = starNamed(STAR_LINE[i + 1]);
        if (a < 0 || b < 0) continue;
        double ra1, dec1, ra2, dec2;
        int x1, y1, e1, x2, y2, e2;
        starRad(a, &ra1, &dec1);
        starRad(b, &ra2, &dec2);
        skyProject(cx, cy, R, ra1, dec1, now, &x1, &y1, &e1);
        skyProject(cx, cy, R, ra2, dec2, now, &x2, &y2, &e2);
        if (e1 <= 0 || e2 <= 0) continue;
        gfx->drawLine(x1, y1, x2, y2, link);
      }
      for (int i = 0; i < nStar; i++) {
        if (STARS[i].mag > 40) continue;
        double ra, dec;
        starRad(i, &ra, &dec);
        skyProject(cx, cy, R, ra, dec, now, &px, &py, &el);
        if (el <= 0) continue;
        int mag = STARS[i].mag;
        int rad = mag <= 0 ? 3 : (mag <= 15 ? 2 : 1);
        gfx->fillCircle(px, py, rad, RGB565(236, 230, 210));
        int nm = STARS[i].name;
        if (nm >= 0 && nm < nName && mag <= 15) {
          gfx->setTextColor(COL_INK, well);
          gfx->setTextSize(1);
          gfx->setCursor(px + 4, py - 6);
          gfx->print(STAR_NAME[nm].n);
        }
      }
    }
    int az, elIss;
    if (issLook(&az, &elIss) && elIss > 0) {
      float rr = (90.0f - elIss) / 90.0f * (float)(R - 8);
      float azr = az * DEG;
      gfx->fillRoundRect(cx + (int)(sinf(azr) * rr) - 4, cy - (int)(cosf(azr) * rr) - 4, 8, 8, 2, COL_CORAL);
    }
  } else {
    gfx->setTextColor(COL_MUTE, well);
    centre(placeSet ? "waiting for the clock" : "set a place", cx, cy - 8, 2);
  }
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setTextSize(1);
  gfx->setCursor(X0, 458);
  gfx->print(sunEl > 0 ? "daylight hides the stars" : "center is overhead    rim is the horizon");
}

bool hasFold(const char *hay, const char *needle) {
  if (!hay || !needle || !needle[0]) return false;
  int nlen = (int)strlen(needle);
  for (const char *p = hay; *p; p++) {
    int i = 0;
    while (i < nlen && p[i]) {
      char a = p[i], b = needle[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
      if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
      if (a != b) break;
      i++;
    }
    if (i == nlen) return true;
  }
  return false;
}

time_t unixUtc(int y, int m, int d, int hh, int mm) {
  static const int md[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = 0;
  for (int yy = 1970; yy < y; yy++) days += ((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0) ? 366 : 365;
  for (int i = 1; i < m && i <= 12; i++) days += md[i];
  if (m > 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days++;
  days += d - 1;
  return (time_t)days * 86400 + hh * 3600 + mm * 60;
}

bool usLaunchTonight(char *rocket, int nr, char *city, int nc) {
  rocket[0] = 0;
  city[0] = 0;
  time_t now;
  if (!nLaunch || !placeSet || !liveClock(&now)) return false;
  static const char *keys[] = {"vandenberg", "cape canaveral", "kennedy", "wallops", "starbase", "boca", "kodiak", "mojave"};
  static const float lats[] = {34.63f, 28.47f, 28.57f, 37.94f, 25.99f, 25.99f, 57.44f, 35.06f};
  static const float lons[] = {-120.61f, -80.56f, -80.65f, -75.47f, -97.16f, -97.16f, -152.34f, -118.15f};
  int best = -1;
  time_t bestT = 0;
  for (int i = 0; i < nLaunch; i++) {
    if (!launches[i].where[0] || !launches[i].when[0]) continue;
    int site = -1;
    for (int s = 0; s < 8; s++) {
      if (hasFold(launches[i].where, keys[s])) {
        site = s;
        break;
      }
    }
    if (site < 0) continue;
    float miles = distKm(placeLat, placeLon, lats[site], lons[site]) * 0.621371f;
    if (miles > 400.0f) continue;
    int y, mo, d, h, mi;
    if (sscanf(launches[i].when, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5) continue;
    time_t t = unixUtc(y, mo, d, h, mi);
    if (t + 20 * 60 < now || t > now + 14 * 3600) continue;
    if (best < 0 || t < bestT) {
      best = i;
      bestT = t;
    }
  }
  if (best < 0) return false;
  const char *nm = launches[best].name;
  const char *bar = strstr(nm, " | ");
  if (bar) {
    int n = (int)(bar - nm);
    if (n > nr - 1) n = nr - 1;
    memcpy(rocket, nm, n);
    rocket[n] = 0;
  } else strlcpy(rocket, nm, nr);
  strlcpy(city, launches[best].where, nc);
  return rocket[0] != 0;
}

void drawTonight() {
  pageClear();
  drawRail();
  drawBack("TONIGHT");
  nUp = 0;
  time_t now = 0;
  if (!placeSet || !liveClock(&now)) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(X0, 80);
    gfx->print(!placeSet ? "set a place" : "waiting for the clock");
    return;
  }
  double d = (2440587.5 + (double)now / 86400.0) - 2451545.0;
  double sra, sdec, slon, srs, mra, mdec, mlon;
  sunPos(d, &sra, &sdec, &slon, &srs);
  moonPos(d, &mra, &mdec, &mlon);
  int az, el, sunEl;
  altAz(sra, sdec, now, &az, &sunEl);
  altAz(mra, mdec, now, &az, &el);
  addUp("Moon", az, el);
  for (int i = 0; i < 5; i++) {
    double ra, dec;
    planetPos(i, d, slon, srs, &ra, &dec);
    altAz(ra, dec, now, &az, &el);
    addUp(PLANET_NAME[i], az, el);
  }
  if (sunEl <= 0) {
    int nStar = (int)(sizeof(STARS) / sizeof(STARS[0]));
    int nName = (int)(sizeof(STAR_NAME) / sizeof(STAR_NAME[0]));
    for (int i = 0; i < nStar; i++) {
      int nm = STARS[i].name;
      if (nm < 0 || nm >= nName || STARS[i].mag > 15) continue;
      double ra, dec;
      starRad(i, &ra, &dec);
      altAz(ra, dec, now, &az, &el);
      addUp(STAR_NAME[nm].n, az, el);
    }
  }
  char rocket[24] = "", city[28] = "";
  bool showLaunch = usLaunchTonight(rocket, sizeof(rocket), city, sizeof(city));
  if (!nUp && !showLaunch) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(X0, 80);
    gfx->print("nothing bright is up");
    return;
  }
  int y0 = 56;
  if (showLaunch) {
    gfx->fillRoundRect(X0, 56, 676, 40, 8, COL_PANEL);
    gfx->setTextColor(COL_HOT, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(X0 + 12, 68);
    gfx->print(rocket);
    gfx->setTextColor(COL_INK, COL_PANEL);
    rightText(city, X0 + 660, 68, 2);
    y0 = 104;
  }
  int room = showLaunch ? 8 : 9;
  int rows = nUp < room ? nUp : room;
  for (int i = 0; i < rows; i++) {
    int y = y0 + i * 44;
    gfx->fillRoundRect(X0, y, 676, 40, 8, COL_PANEL);
    gfx->setTextColor(COL_HOT, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(X0 + 12, y + 12);
    gfx->print(upName[i]);
    char row[48];
    snprintf(row, sizeof(row), "up %d    %d degrees %s", upEl[i], upAz[i], lookWord(upAz[i]));
    gfx->setTextColor(COL_INK, COL_PANEL);
    rightText(row, X0 + 660, y + 12, 2);
  }
}

void drawPlanets() {
  pageClear();
  drawRail();
  drawBack("PLANETS");
  time_t now = 0;
  if (!placeSet || !liveClock(&now)) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(X0, 80);
    gfx->print(!placeSet ? "set a place" : "waiting for the clock");
    return;
  }
  double d = (2440587.5 + (double)now / 86400.0) - 2451545.0;
  double sra, sdec, slon, srs, mra, mdec, mlon;
  sunPos(d, &sra, &sdec, &slon, &srs);
  moonPos(d, &mra, &mdec, &mlon);
  const char *name[] = {"MOON", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"};
  double ra[6], dec[6];
  ra[0] = mra;
  dec[0] = mdec;
  for (int i = 0; i < 5; i++) planetPos(i, d, slon, srs, &ra[i + 1], &dec[i + 1]);
  for (int i = 0; i < 6; i++) {
    int az, el;
    altAz(ra[i], dec[i], now, &az, &el);
    int y = 52 + i * 58;
    gfx->fillRoundRect(X0, y, 676, 52, 8, COL_PANEL);
    gfx->setTextColor(el > 0 ? COL_HOT : COL_MUTE, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(X0 + 14, y + 16);
    gfx->print(name[i]);
    char row[48];
    if (el > 0) snprintf(row, sizeof(row), "up %d    %d degrees %s", el, az, lookWord(az));
    else snprintf(row, sizeof(row), "below horizon");
    gfx->setTextColor(el > 0 ? COL_INK : COL_MUTE, COL_PANEL);
    rightText(row, X0 + 660, y + 16, 2);
  }
}

void moonTimes(time_t now, char *rise, int nr, char *setb, int ns) {
  rise[0] = 0;
  setb[0] = 0;
  int prev = -999;
  for (int s = 0; s <= 36 * 30; s++) {
    time_t t = now + (time_t)s * 120;
    double day = (2440587.5 + (double)t / 86400.0) - 2451545.0;
    double mra, mdec, mlon;
    moonPos(day, &mra, &mdec, &mlon);
    int az, el;
    altAz(mra, mdec, t, &az, &el);
    if (prev > -900) {
      struct tm ti;
      localtime_r(&t, &ti);
      int h = ti.tm_hour % 12;
      if (!h) h = 12;
      if (prev <= 0 && el > 0 && !rise[0]) snprintf(rise, nr, "rises %d:%02d %s", h, ti.tm_min, ti.tm_hour < 12 ? "am" : "pm");
      if (prev > 0 && el <= 0 && !setb[0]) snprintf(setb, ns, "sets %d:%02d %s", h, ti.tm_min, ti.tm_hour < 12 ? "am" : "pm");
    }
    prev = el;
    if (rise[0] && setb[0]) return;
  }
}

void drawMoonPage() {
  pageClear();
  drawRail();
  drawBack("MOON");
  time_t now = 0;
  float phase = 0;
  int el = -90, az = 0;
  bool have = placeSet && liveClock(&now);
  if (have) {
    double d = (2440587.5 + (double)now / 86400.0) - 2451545.0;
    double sra, sdec, slon, srs, mra, mdec, mlon;
    sunPos(d, &sra, &sdec, &slon, &srs);
    moonPos(d, &mra, &mdec, &mlon);
    double dang = mlon - slon;
    while (dang < 0) dang += 2 * M_PI;
    while (dang >= 2 * M_PI) dang -= 2 * M_PI;
    phase = (float)(dang / (2 * M_PI));
    altAz(mra, mdec, now, &az, &el);
  }
  drawMoonPhase(300, 270, 110, phase);
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(3);
  gfx->setCursor(480, 160);
  gfx->print(have ? "moon phase" : "--");
  int pct = (int)lroundf((1.0f - cosf(phase * 2.0f * (float)M_PI)) * 50.0f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  char row[48];
  snprintf(row, sizeof(row), "%d%% lit", pct);
  gfx->setTextColor(COL_INK, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(480, 210);
  gfx->print(have ? row : "");
  gfx->setCursor(480, 246);
  if (!have) gfx->print(!placeSet ? "set a place" : "waiting for the clock");
  else if (el > 0) {
    snprintf(row, sizeof(row), "up %d    %d degrees %s", el, az, lookWord(az));
    gfx->print(row);
  } else gfx->print("below horizon");
  if (have) {
    char rise[28], setb[28];
    moonTimes(now, rise, sizeof(rise), setb, sizeof(setb));
    gfx->setTextColor(COL_INK, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(480, 286);
    gfx->print(rise[0] ? rise : "rises --");
    gfx->setCursor(480, 318);
    gfx->print(setb[0] ? setb : "sets --");
  }
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setTextSize(1);
  gfx->setCursor(X0, 458);
  gfx->print("the bright part is the lit side");
}

void drawRocks() {
  pageClear();
  drawRail();
  drawBack("ROCKS");
  time_t now = 0;
  bool have = placeSet && liveClock(&now);
  double d = 0, slon = 0, srs = 1, sra, sdec;
  if (have) {
    d = (2440587.5 + (double)now / 86400.0) - 2451545.0;
    sunPos(d, &sra, &sdec, &slon, &srs);
  }
  int n = (int)(sizeof(ROCKS) / sizeof(ROCKS[0]));
  if (n > 6) n = 6;
  for (int i = 0; i < n; i++) {
    int y = 52 + i * 68;
    gfx->fillRoundRect(X0, y, 676, 62, 8, COL_PANEL);
    gfx->setTextColor(COL_HOT, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(X0 + 12, y + 8);
    gfx->print(ROCKS[i].name);
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->print("  ");
    gfx->print(ROCKS[i].kind);
    char note[42];
    strlcpy(note, ROCKS[i].note, sizeof(note));
    gfx->setTextSize(1);
    gfx->setTextColor(COL_INK, COL_PANEL);
    gfx->setCursor(X0 + 12, y + 36);
    gfx->print(note);
    gfx->setTextSize(2);
    if (!have) {
      gfx->setTextColor(COL_MUTE, COL_PANEL);
      rightText(!placeSet ? "set a place" : "waiting", X0 + 660, y + 18, 2);
    } else {
      double ra, dec;
      int az, el;
      rockPos(i, d, slon, srs, &ra, &dec);
      altAz(ra, dec, now, &az, &el);
      char row[24];
      if (el > 0) snprintf(row, sizeof(row), "up %d degrees", el);
      else snprintf(row, sizeof(row), "below horizon");
      gfx->setTextColor(el > 0 ? COL_SEA : COL_MUTE, COL_PANEL);
      rightText(row, X0 + 660, y + 18, 2);
    }
  }
}

void drawSats() {
  pageClear();
  drawRail();
  drawBack("SATELLITES");
  int n = (int)(sizeof(SATS) / sizeof(SATS[0]));
  if (n > 3) n = 3;
  for (int i = 0; i < n; i++) {
    int y = 56 + i * 128;
    gfx->fillRoundRect(X0, y, 676, 118, 12, COL_PANEL);
    gfx->setTextColor(COL_HOT, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(X0 + 16, y + 16);
    gfx->print(SATS[i].name);
    char id[16];
    snprintf(id, sizeof(id), "%d", SATS[i].cat);
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    rightText(id, X0 + 656, y + 10, 2);
    static const char *satSize[] = {"about 357 feet long", "about 180 feet long", "about 43 feet long"};
    float al = -1;
    int mph = 0;
    time_t nowt = 0;
    bool haveT = liveClock(&nowt);
    float pla = 0, plo = 0;
    if (i == 0 && issOk) {
      al = issAlt;
      mph = issVel > 0 ? (int)lroundf(issVel * 0.621371f) : craftMph(0, issAlt);
    } else if (haveT && craftPosAt(i, nowt, &pla, &plo, &al)) mph = craftMph(i, al);
    char row[72];
    if (al > 0) snprintf(row, sizeof(row), "height %.0f km    %d mph", al, mph);
    else strlcpy(row, "height --    speed --", sizeof(row));
    gfx->setTextColor(COL_INK, COL_PANEL);
    gfx->setCursor(X0 + 16, y + 36);
    gfx->print(row);
    int crew = i == 0 ? issCrewN : (i == 1 ? tgCrewN : 0);
    if (crew < 0) snprintf(row, sizeof(row), "size %s    crew --", satSize[i]);
    else snprintf(row, sizeof(row), "size %s    crew %d", satSize[i], crew);
    gfx->setCursor(X0 + 16, y + 60);
    gfx->print(row);
    gfx->setTextColor(COL_SEA, COL_PANEL);
    gfx->setCursor(X0 + 16, y + 86);
    if (i == 0) {
      int az, el;
      if (issLook(&az, &el)) {
        if (el > 0) snprintf(row, sizeof(row), "live    %d degrees %s    up %d", az, lookWord(az), el);
        else snprintf(row, sizeof(row), "live    below horizon");
        gfx->print(row);
      } else gfx->print(placeSet ? "waiting for a live fix" : "set a place");
    } else {
      int az, el;
      if (placeSet && haveT && craftLookAt(i, nowt, &az, &el)) {
        if (el > 0) snprintf(row, sizeof(row), "live    %d degrees %s    up %d", az, lookWord(az), el);
        else snprintf(row, sizeof(row), "live    below horizon");
        gfx->print(row);
      } else gfx->print(placeSet ? "saved orbit" : "set a place");
    }
  }
}

void drawGroundTrack(float *lats, float *lons, int n, int mapY, int mapH, uint16_t col) {
  int px = -999, py = -999;
  for (int i = 0; i < n; i++) {
    int x, y;
    if (!worldDot(lats[i], lons[i], mapY, mapH, &x, &y)) {
      px = -999;
      continue;
    }
    if (px > -900 && abs(x - px) < 280) {
      gfx->drawLine(px, py, x, y, col);
      gfx->drawLine(px, py + 1, x, y + 1, col);
    }
    px = x;
    py = y;
  }
}

void tagMap(int x, int y, const char *word, uint16_t col) {
  gfx->setTextSize(1);
  gfx->setTextColor(col, RGB565(12, 16, 28));
  int tx = x + 10;
  if (tx > 760) tx = x - 52;
  if (tx < 4) tx = 4;
  gfx->setCursor(tx, y - 8);
  gfx->print(word);
}

void drawIss() {
  pageClear();
  chip(12, 8, 132, 36, "< BACK", false);
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(160, 16);
  gfx->print("ISS WORLD");
  const int mapY = 48, mapH = 156;
  time_t now = 0;
  sunMapOk = false;
  if (liveClock(&now)) {
    double jd = 2440587.5 + (double)now / 86400.0;
    double T = (jd - 2451545.0) / 36525.0;
    double L = 280.460 + 36000.770 * T;
    double M = 357.528 + 35999.050 * T;
    L = wrap360(L);
    M = wrap360(M);
    double Mr = M * DEG;
    double lam = L + 1.915 * sin(Mr) + 0.020 * sin(2 * Mr);
    double eps = 23.439 - 0.013 * T;
    double lr = lam * DEG, er = eps * DEG;
    double ra = atan2(cos(er) * sin(lr), cos(lr));
    double dec = asin(sin(er) * sin(lr));
    double gmst = wrap360(280.46061837 + 360.98564736629 * (jd - 2451545.0));
    double raDeg = ra * 57.295779513;
    if (raDeg < 0) raDeg += 360.0;
    double gha = wrap360(gmst - raDeg);
    sunLon = (float)(-gha);
    if (sunLon < -180) sunLon += 360;
    if (sunLon > 180) sunLon -= 360;
    sunLat = (float)(dec * 57.295779513);
    sunMapOk = true;
  }
  ensureIssPic();
  if (issPicOk) paintIss(0, mapY, 800, mapH);
  else {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setCursor(160, 110);
    gfx->print("photo not ready");
  }
  if (issPicOk && nIssTrack > 1) drawGroundTrack(issTrackLat, issTrackLon, nIssTrack, mapY, mapH, COL_CORAL);
  if (issPicOk && nTgTrack > 1) drawGroundTrack(tgTrackLat, tgTrackLon, nTgTrack, mapY, mapH, RGB565(80, 150, 255));
  if (issPicOk && sunMapOk) {
    int sx, sy;
    if (worldDot(sunLat, sunLon, mapY, mapH, &sx, &sy)) {
      gfx->fillCircle(sx, sy, 7, COL_HOT);
      gfx->fillCircle(sx, sy, 3, RGB565(255, 236, 170));
      tagMap(sx, sy, "sun", COL_HOT);
    }
  }
  if (issPicOk && placeSet) {
    int hx, hy;
    if (worldDot(placeLat, placeLon, mapY, mapH, &hx, &hy)) {
      gfx->fillCircle(hx, hy, 5, RGB565(255, 255, 255));
      tagMap(hx, hy, "you", RGB565(255, 255, 255));
    }
  }
  if (issPicOk && issOk) {
    int hx, hy;
    if (worldDot(issLat, issLon, mapY, mapH, &hx, &hy)) {
      gfx->fillRoundRect(hx - 6, hy - 6, 12, 12, 2, COL_CORAL);
      tagMap(hx, hy, "ISS", COL_CORAL);
    }
  }
  if (issPicOk && tgOk) {
    int hx, hy;
    if (worldDot(tgLat, tgLon, mapY, mapH, &hx, &hy)) {
      gfx->fillRoundRect(hx - 6, hy - 6, 12, 12, 2, RGB565(80, 150, 255));
      tagMap(hx, hy, "Tiangong", RGB565(80, 150, 255));
    }
  }
  if (issVisNow) chip(16, 56, 150, 32, "ISS UP", true);
  if (tgVisNow) chip(180, 56, 210, 32, "TIANGONG UP", true);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_INK, COL_BG);
  gfx->setCursor(16, 214);
  if (issOk) {
    char row[72];
    int az, el;
    if (issLook(&az, &el) && el > 0) snprintf(row, sizeof(row), "ISS  %.1f %.1f   %.0f km   %d degrees %s  up %d", issLat, issLon, issAlt, az, lookWord(az), el);
    else if (issOk) snprintf(row, sizeof(row), "ISS  %.1f %.1f   %.0f km   below horizon", issLat, issLon, issAlt);
    else strlcpy(row, "waiting", sizeof(row));
    gfx->print(row);
  } else gfx->print(placeSet ? "waiting for a live fix" : "set a place");
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setCursor(16, 244);
  gfx->print("VISIBLE FROM HERE");
  gfx->setTextColor(COL_INK, COL_BG);
  if (!nIssPass) {
    gfx->setCursor(16, 272);
    gfx->print(placeSet ? "checking passes" : "set a place");
  } else {
    for (int i = 0; i < nIssPass && i < 4; i++) {
      gfx->setCursor(16, 272 + i * 28);
      gfx->print(issPassLine[i]);
    }
  }
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setCursor(16, 400);
  gfx->print("Kp");
  gfx->setTextColor(COL_INK, COL_BG);
  gfx->setCursor(70, 400);
  if (kpOk && kp >= 0) {
    char row[28];
    snprintf(row, sizeof(row), "%.1f  %s", kp, kpWord(kp));
    gfx->print(row);
  } else gfx->print("--");
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setTextSize(1);
  gfx->setCursor(16, 440);
  gfx->print("red line is ISS    blue line is Tiangong    gold dot is the sun    white dot is you");
  gfx->setCursor(16, 458);
  gfx->print("each pass names the station you can see from this place");
}

void drawMeteors() {
  pageClear();
  drawRail();
  drawBack("METEORS");
  static const char *name[] = {"Quadrantids", "Lyrids", "Eta Aquariids", "Perseids", "Orionids", "Leonids", "Geminids", "Ursids"};
  static const char *when[] = {"early January", "late April", "early May", "mid August", "late October", "mid November", "mid December", "late December"};
  static const char *note[] = {"a short sharp peak", "modest, sometimes a burst", "bits of Halley", "the summer peak", "bits of Halley", "can storm some years", "the strong winter one", "a quiet close to the year"};
  for (int i = 0; i < 8; i++) {
    int y = 52 + i * 52;
    gfx->fillRoundRect(X0, y, 676, 48, 8, COL_PANEL);
    gfx->setTextColor(COL_HOT, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(X0 + 12, y + 6);
    gfx->print(name[i]);
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->setTextSize(1);
    gfx->setCursor(X0 + 12, y + 30);
    gfx->print(when[i]);
    gfx->print("    ");
    gfx->print(note[i]);
  }
}
void mapXY(int x, int y, int w, int h, float lat, float lon, int *ox, int *oy) {
  if (lon < -180) lon = -180;
  if (lon > 180) lon = 180;
  if (lat < -90) lat = -90;
  if (lat > 90) lat = 90;
  *ox = x + (int)((lon + 180.0f) / 360.0f * (float)(w - 1));
  *oy = y + (int)((90.0f - lat) / 180.0f * (float)(h - 1));
}

void drawQuake() {
  pageClear();
  bool photo = quakeUsa ? showJpg(0, 52, USA_MAP_JPG, USA_MAP_JPG_LEN) : showJpg(0, 52, WORLD_MAP_JPG, WORLD_MAP_JPG_LEN);
  quakePhoto = photo;
  if (photo) {
    chip(X0, 8, 132, 36, "< BACK", false);
    gfx->setTextColor(COL_HOT, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(X0 + 148, 16);
    gfx->print("QUAKES");
  } else {
    drawRail();
    drawBack("QUAKES");
  }
  chip(500, 8, 120, 36, "WORLD", !quakeUsa);
  chip(640, 8, 140, 36, "USA", quakeUsa);
  if (!photo) {
    int x = X0, y = 54, w = 676, h = 188;
    gfx->fillRoundRect(x, y, w, h, 12, COL_PANEL);
    for (int lon = -120; lon <= 120; lon += 60) {
      int px, py;
      mapXY(x, y, w, h, 0, (float)lon, &px, &py);
      gfx->drawLine(px, y + 8, px, y + h - 8, COL_LINE);
    }
    for (int lat = -60; lat <= 60; lat += 30) {
      int px, py;
      mapXY(x, y, w, h, (float)lat, 0, &px, &py);
      gfx->drawLine(x + 8, py, x + w - 8, py, COL_LINE);
    }
    bool pen = false;
    int prevx = 0, prevy = 0;
    int coastN = (int)(sizeof(COAST) / sizeof(COAST[0]));
    for (int i = 0; i < coastN && COAST[i] != -128;) {
      if (COAST[i] == 127) {
        pen = false;
        i++;
        continue;
      }
      if (i + 1 >= coastN) break;
      int px, py;
      mapXY(x, y, w, h, COAST[i], COAST[i + 1], &px, &py);
      if (pen) gfx->drawLine(prevx, prevy, px, py, COL_SEA);
      prevx = px;
      prevy = py;
      pen = true;
      i += 2;
    }
    if (placeSet) {
      int px, py;
      mapXY(x, y, w, h, placeLat, placeLon, &px, &py);
      gfx->fillCircle(px, py, 4, COL_SEA);
    }
    for (int i = 0; i < nQuake; i++) {
      int px, py;
      mapXY(x, y, w, h, quakes[i].lat, quakes[i].lon, &px, &py);
      int r = 3 + (int)quakes[i].mag;
      if (r > 10) r = 10;
      gfx->fillCircle(px, py, r, quakes[i].mag >= 5 ? COL_CORAL : COL_HOT);
    }
  } else if (quakeUsa) {
    if (placeSet) {
      int px, py;
      if (usaDot(placeLat, placeLon, &px, &py)) gfx->fillCircle(px, py, 5, COL_SEA);
    }
    for (int i = 0; i < nQuake; i++) {
      int px, py;
      if (!usaDot(quakes[i].lat, quakes[i].lon, &px, &py)) continue;
      int r = quakes[i].mag >= 6 ? 7 : 5;
      gfx->fillCircle(px, py, r, quakes[i].mag >= 5 ? COL_CORAL : COL_HOT);
    }
    uint16_t sea = RGB565(13, 48, 90);
    uint16_t land = RGB565(196, 186, 150);
    gfx->fillRect(18, 69, 56, 10, land);
    gfx->fillRect(18, 209, 58, 22, sea);
    gfx->setTextColor(RGB565(236, 232, 214), sea);
    gfx->setTextSize(1);
    gfx->setCursor(128, 96);
    gfx->print("ALASKA");
    gfx->setCursor(24, 244);
    gfx->print("HAWAII");
  } else {
    if (placeSet) {
      int px, py;
      if (worldDot(placeLat, placeLon, 52, 200, &px, &py)) gfx->fillCircle(px, py, 5, COL_SEA);
    }
    for (int i = 0; i < nQuake; i++) {
      int px, py;
      if (!worldDot(quakes[i].lat, quakes[i].lon, 52, 200, &px, &py)) continue;
      int r = quakes[i].mag >= 6 ? 7 : 5;
      gfx->fillCircle(px, py, r, quakes[i].mag >= 5 ? COL_CORAL : COL_HOT);
    }
  }
  int y0 = photo ? (quakeUsa ? 340 : 260) : 254;
  int maxRows = photo ? (quakeUsa ? 2 : 4) : 5;
  int shown = 0;
  for (int i = 0; i < nQuake && shown < maxRows; i++) {
    if (photo && quakeUsa) {
      int px, py;
      if (!usaDot(quakes[i].lat, quakes[i].lon, &px, &py)) continue;
    }
    int yy = y0 + shown * 42;
    shown++;
    if (yy > 430) break;
    gfx->fillRoundRect(photo ? 8 : X0, yy, photo ? 784 : 676, 38, 8, COL_PANEL);
    char mag[12], mid[24];
    snprintf(mag, sizeof(mag), "M%.1f", quakes[i].mag);
    gfx->setTextSize(2);
    gfx->setTextColor(quakes[i].mag >= 5 ? COL_CORAL : COL_HOT, COL_PANEL);
    gfx->setCursor(X0 + 10, yy + 10);
    gfx->print(mag);
    snprintf(mid, sizeof(mid), "%d mi   %.0f km", quakes[i].miles, quakes[i].depth);
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->setCursor(X0 + 90, yy + 10);
    gfx->print(mid);
    gfx->setTextColor(COL_INK, COL_PANEL);
    char cut[28];
    strlcpy(cut, quakes[i].place, sizeof(cut));
    rightText(cut, X0 + 660, yy + 10, 2);
  }
  if (!shown) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(photo ? 16 : X0, y0);
    gfx->print(!placeSet ? "set a place" : (!quakeOk ? "waiting" : "none today"));
  }
  gfx->setTextSize(1);
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setCursor(X0, 464);
  gfx->print("a list of recent earthquakes    not a warning alarm");
}

void drawQuakeFact() {
  pageClear();
  drawRail();
  drawBack("QUAKE");
  if (quakePin < 0 || quakePin >= nQuake) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(X0, 80);
    gfx->print("that quake is gone");
    return;
  }
  Quake q = quakes[quakePin];
  char row[64];
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(3);
  gfx->setCursor(X0, 70);
  snprintf(row, sizeof(row), "M%.1f", q.mag);
  gfx->print(row);
  gfx->setTextColor(COL_INK, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(X0, 120);
  gfx->print(q.place[0] ? q.place : "place not listed");
  gfx->setCursor(X0, 160);
  snprintf(row, sizeof(row), "%d miles from here", q.miles);
  gfx->print(row);
  gfx->setCursor(X0, 196);
  snprintf(row, sizeof(row), "depth %.0f km", q.depth);
  gfx->print(row);
  gfx->setCursor(X0, 232);
  if (q.when > 0) {
    struct tm ti;
    localtime_r(&q.when, &ti);
    int h = ti.tm_hour % 12;
    if (!h) h = 12;
    snprintf(row, sizeof(row), "%s %s %d   %d:%02d %s", WDAY[ti.tm_wday], MON[ti.tm_mon], ti.tm_mday, h, ti.tm_min,
             ti.tm_hour < 12 ? "am" : "pm");
    gfx->print(row);
  } else gfx->print("time not listed");
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setCursor(X0, 280);
  snprintf(row, sizeof(row), "%.2f   %.2f", q.lat, q.lon);
  gfx->print(row);
}

void drawLaunch() {
  pageClear();
  drawRail();
  drawBack("LAUNCHES");
  if (!nLaunch) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(X0, 80);
    gfx->print(!wifiOk ? "join wifi" : (!launchOk ? "waiting" : "none posted"));
    return;
  }
  for (int i = 0; i < nLaunch; i++) {
    int y = 52 + i * 104;
    gfx->fillRoundRect(X0, y, 676, 96, 12, COL_PANEL);
    gfx->setTextColor(COL_INK, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(X0 + 16, y + 10);
    gfx->print(launches[i].name);
    gfx->setTextColor(COL_SEA, COL_PANEL);
    gfx->setCursor(X0 + 16, y + 36);
    gfx->print(launches[i].where[0] ? launches[i].where : "site not listed");
    gfx->setTextColor(COL_HOT, COL_PANEL);
    gfx->setCursor(X0 + 16, y + 62);
    gfx->print(launches[i].when);
    gfx->print("  UTC");
  }
}

void addKey(int x, int y, int w, int h, int code, char ch) {
  if (nKeys >= 48) return;
  keyX[nKeys] = x;
  keyY[nKeys] = y;
  keyW[nKeys] = w;
  keyH[nKeys] = h;
  keyCode[nKeys] = code;
  keyCh[nKeys] = ch;
  nKeys++;
}

void buildKeys() {
  nKeys = 0;
  const char *rows[3];
  if (!symbols) {
    rows[0] = caps ? "QWERTYUIOP" : "qwertyuiop";
    rows[1] = caps ? "ASDFGHJKL" : "asdfghjkl";
    rows[2] = caps ? "ZXCVBNM" : "zxcvbnm";
  } else {
    rows[0] = "1234567890";
    rows[1] = "-/:;()$&@\"";
    rows[2] = ".,?!'+=";
  }
  int kw = 64, kh = 36, yg = 300;
  for (int r = 0; r < 3; r++) {
    int len = (int)strlen(rows[r]);
    int span = len * kw + (len - 1) * 4;
    int x = X0;
    if (r == 1) x = X0 + (676 - span) / 2;
    for (int i = 0; i < len; i++) addKey(x + i * (kw + 4), yg + r * 42, kw, kh, 0, rows[r][i]);
    if (r == 2 && !symbols) addKey(x + len * (kw + 4), yg + r * 42, 140, kh, 1, 0);
  }
  int y = yg + 3 * 42;
  addKey(X0, y, 90, kh, 4, 0);
  addKey(X0 + 98, y, 90, kh, 3, 0);
  addKey(X0 + 196, y, 280, kh, 2, 0);
  addKey(X0 + 484, y, 192, kh, 1, 0);
}

void drawKeys() {
  buildKeys();
  for (int i = 0; i < nKeys; i++) {
    const char *lab = 0;
    char one[2] = {keyCh[i], 0};
    if (keyCode[i] == 0) lab = one;
    else if (keyCode[i] == 1) lab = "DEL";
    else if (keyCode[i] == 2) lab = "SPACE";
    else if (keyCode[i] == 3) lab = caps ? "abc" : "ABC";
    else lab = symbols ? "ABC" : "!#";
    chip(keyX[i], keyY[i], keyW[i], keyH[i], lab, false);
  }
}

void drawPlace() {
  pageClear();
  drawRail();
  drawBack("PLACE");
  gfx->fillRoundRect(X0, 52, 500, 42, 8, COL_PANEL);
  gfx->drawRoundRect(X0, 52, 500, 42, 8, COL_HOT);
  const char *shown = placeQuery[0] ? placeQuery : "city or ZIP";
  int qSz = ((int)strlen(shown) * 12 > 470) ? 1 : 2;
  gfx->setTextSize(qSz);
  gfx->setTextColor(placeQuery[0] ? COL_INK : COL_MUTE, COL_PANEL);
  gfx->setCursor(X0 + 12, qSz == 2 ? 64 : 66);
  gfx->print(shown);
  chip(624, 52, 160, 42, "SEARCH", true);
  if (placeMsg[0] && !nHits) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setCursor(X0, 112);
    gfx->print(placeMsg);
  }
  for (int i = 0; i < nHits && i < 3; i++) {
    int y = 108 + i * 52;
    chip(X0, y, 676, 46, hits[i].label, false);
  }
  drawKeys();
}

void drawWifi() {
  pageClear();
  drawRail();
  chip(X0, 8, 120, 36, "SCAN", scanOn);
  chip(250, 8, 120, 36, "SAVE", true);
  chip(390, 8, 160, 36, "RESET", false);
  if (wifiMsg[0]) {
    gfx->setTextColor(COL_CORAL, COL_BG);
    gfx->setTextSize(2);
    gfx->setCursor(570, 16);
    gfx->print(wifiMsg);
  }
  gfx->fillRoundRect(X0, 52, 676, 40, 8, COL_PANEL);
  gfx->drawRoundRect(X0, 52, 676, 40, 8, focus == 1 ? COL_HOT : COL_LINE);
  gfx->setTextSize(2);
  gfx->setTextColor(wifiSsid[0] ? COL_INK : COL_MUTE, COL_PANEL);
  gfx->setCursor(X0 + 12, 64);
  gfx->print(wifiSsid[0] ? wifiSsid : "network name");
  gfx->fillRoundRect(X0, 100, 676, 40, 8, COL_PANEL);
  gfx->drawRoundRect(X0, 100, 676, 40, 8, focus == 2 ? COL_HOT : COL_LINE);
  gfx->setTextColor(wifiPass[0] ? COL_INK : COL_MUTE, COL_PANEL);
  gfx->setCursor(X0 + 12, 112);
  gfx->print(wifiPass[0] ? wifiPass : "password");
  int rows = nScan - scanTop;
  if (rows > 4) rows = 4;
  if (scanOn) {
    gfx->setTextColor(COL_MUTE, COL_BG);
    gfx->setCursor(X0, 156);
    gfx->print("scanning");
  }
  for (int i = 0; i < rows; i++) {
    int idx = scanTop + i;
    chip(X0, 152 + i * 36, 520, 32, scanName[idx], false);
  }
  if (nScan > 4) chip(640, 188, 140, 36, "MORE", false);
  drawKeys();
}

void drawSetup() {
  pageClear();
  drawRail();
  drawBack("SETUP");
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(X0, 56);
  gfx->print("LOOK");
  chip(250, 46, 140, 36, "DARK", !dayMode);
  chip(404, 46, 140, 36, "LIGHT", dayMode);
  gfx->setCursor(X0, 100);
  gfx->print("LAMP");
  chip(250, 90, 120, 36, "DIM", blLevel == 0);
  chip(382, 90, 120, 36, "MID", blLevel == 1);
  chip(514, 90, 150, 36, "BRIGHT", blLevel == 2);
  gfx->setCursor(X0, 144);
  gfx->print("TEMP");
  chip(250, 134, 120, 36, "F", !useC);
  chip(382, 134, 120, 36, "C", useC);
  gfx->setCursor(X0, 188);
  gfx->print("ALERTS");
  chip(250, 178, 120, 36, "ON", pinAlerts);
  chip(382, 178, 120, 36, "OFF", !pinAlerts);
  gfx->setCursor(X0, 232);
  gfx->print("TIDES");
  chip(250, 222, 120, 36, "SHOW", showTide);
  chip(382, 222, 120, 36, "HIDE", !showTide);
  chip(X0, 270, 200, 44, "ABOUT", false);
  chip(328, 270, 200, 44, "WI-FI", false);
  chip(544, 270, 240, 44, "PLACE", false);
  chip(X0, 326, 320, 44, "FACTORY RESET", false);
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setTextSize(1);
  gfx->setCursor(X0, 390);
  gfx->print("Alerts stay on the main page until CLEAR. Tides follow the nearest coast.");
  gfx->setCursor(X0, 412);
  gfx->print("Bright is full. Factory reset clears this screen only.");
}

void drawAbout() {
  pageClear();
  drawRail();
  drawBack("ABOUT");
  gfx->setTextColor(COL_HOT, COL_BG);
  gfx->setTextSize(3);
  gfx->setCursor(X0, 70);
  gfx->print("Meridian Screen");
  gfx->setTextColor(COL_INK, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(X0, 120);
  gfx->print("version ");
  gfx->print(MERID_VER);
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setCursor(X0, 160);
  gfx->print("Weather, planes, quakes, and the night sky.");
  gfx->setTextColor(otaNewer ? COL_HOT : COL_INK, COL_BG);
  gfx->setCursor(X0, 210);
  gfx->print(otaMsg[0] ? otaMsg : "Tap check for a newer copy.");
  if (otaPct >= 0) {
    char p[24];
    snprintf(p, sizeof(p), "downloading %d%%", otaPct);
    gfx->setTextColor(COL_SEA, COL_BG);
    gfx->setCursor(X0, 242);
    gfx->print(p);
  }
  chip(X0, 300, 280, 52, "CHECK", false);
  if (otaNewer) chip(420, 300, 280, 52, "INSTALL", true);
  gfx->setTextColor(COL_MUTE, COL_BG);
  gfx->setCursor(X0, 380);
  gfx->print("Leave it plugged in while it downloads.");
  gfx->setCursor(X0, 406);
  gfx->print("It restarts on its own when the copy is in.");
}

void drawMore() {
  pageClear();
  drawRail();
  const char *name[] = {"PLACE", "WI-FI", "QUAKES", "LAUNCHES", "SETUP", "ABOUT"};
  const char *hint[] = {"city or ZIP", "network", "nearby", "rockets", "look and lamp", "updates"};
  for (int i = 0; i < 6; i++) {
    int x = X0 + (i % 2) * 348;
    int y = 20 + (i / 2) * 150;
    gfx->fillRoundRect(x, y, 332, 134, 14, COL_PANEL);
    gfx->drawRoundRect(x, y, 332, 134, 14, COL_LINE);
    gfx->setTextColor(COL_HOT, COL_PANEL);
    gfx->setTextSize(3);
    gfx->setCursor(x + 18, y + 36);
    gfx->print(name[i]);
    gfx->setTextColor(COL_MUTE, COL_PANEL);
    gfx->setTextSize(2);
    gfx->setCursor(x + 18, y + 84);
    gfx->print(hint[i]);
  }
}

void drawLinking() {
  gfx->fillScreen(COL_BG);
  gfx->setTextColor(COL_HOT, COL_BG);
  centre("linking", 400, 220, 3);
}

void drawCurrent() {
  if (!gfx) return;
  static int painted = -1;
  paintFresh = (painted != (int)page);
  painted = (int)page;
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  if (page == PG_NOW) drawNow();
  else if (page == PG_WEEK) drawWeek();
  else if (page == PG_AIR) drawAir();
  else if (page == PG_SKY) drawSky();
  else if (page == PG_STARS) drawStars();
  else if (page == PG_TONIGHT) drawTonight();
  else if (page == PG_PLANETS) drawPlanets();
  else if (page == PG_MOON) drawMoonPage();
  else if (page == PG_ROCKS) drawRocks();
  else if (page == PG_SATS) drawSats();
  else if (page == PG_ISS) drawIss();
  else if (page == PG_METEOR) drawMeteors();
  else if (page == PG_MORE) drawMore();
  else if (page == PG_PLACE) drawPlace();
  else if (page == PG_WIFI) drawWifi();
  else if (page == PG_SETUP) drawSetup();
  else if (page == PG_ABOUT) drawAbout();
  else if (page == PG_QUAKE) drawQuake();
  else if (page == PG_LAUNCH) drawLaunch();
  else if (page == PG_QFACT) drawQuakeFact();
  if (dataMux) xSemaphoreGive(dataMux);
}

bool copyQuoted(const char *p, char *out, int n) {
  int i = 0;
  while (*p && *p != '"' && i < n - 1) {
    if (*p == '\\' && p[1]) p++;
    out[i++] = *p++;
  }
  out[i] = 0;
  return i > 0;
}

void trimEnd(char *s) {
  int n = (int)strlen(s);
  while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
}

float numKey(const char *s, const char *end, const char *key) {
  if (!s || !end || s >= end) return MISS;
  int klen = (int)strlen(key);
  const char *p = s;
  while (p && p < end) {
    p = strstr(p, key);
    if (!p || p >= end) return MISS;
    if (p + klen < end && p[klen] == ':') {
      p += klen + 1;
      while (p < end && (*p == ' ' || *p == '"')) p++;
      return (float)atof(p);
    }
    p += klen;
  }
  return MISS;
}

int readArr(const char *s, const char *end, const char *key, float *out, int maxn) {
  if (!s || !end) return 0;
  const char *p = strstr(s, key);
  if (!p || p >= end) return 0;
  p = strchr(p, '[');
  if (!p || p >= end) return 0;
  p++;
  int n = 0;
  while (n < maxn && p < end && *p && *p != ']') {
    while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r') p++;
    if (*p == ']' || !*p) break;
    if (!strncmp(p, "null", 4)) {
      out[n++] = MISS;
      p += 4;
      continue;
    }
    out[n++] = (float)atof(p);
    while (*p && *p != ',' && *p != ']') p++;
  }
  return n;
}

bool readStr0(const char *s, const char *end, const char *key, char *out, int n) {
  out[0] = 0;
  if (!s || !end) return false;
  const char *p = strstr(s, key);
  if (!p || p >= end) return false;
  p = strchr(p, '[');
  if (!p) return false;
  p = strchr(p, '"');
  if (!p) return false;
  return copyQuoted(p + 1, out, n);
}

bool httpGet(const char *url, char *body, int cap) {
  body[0] = 0;
  if (WiFi.status() != WL_CONNECTED || cap < 16 || wifiBusy) return false;
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  bool ok = false;
  if (http.begin(cli, url)) {
    http.setTimeout(12000);
    http.addHeader("User-Agent", "MeridianScreen/1.1");
    http.addHeader("Accept", "application/json,text/plain,*/*");
    int code = http.GET();
    if (code == 200) {
      int got = 0;
      uint32_t t0 = millis();
      while (got < cap - 1 && millis() - t0 < 12000) {
        int n = http.getStream().available();
        if (n > 0) {
          if (n > 1024) n = 1024;
          if (got + n > cap - 1) n = cap - 1 - got;
          int r = http.getStream().readBytes(body + got, n);
          if (r <= 0) break;
          got += r;
          delay(1);
          continue;
        }
        if (!http.connected()) break;
        delay(2);
      }
      body[got] = 0;
      ok = got > 2;
    }
    http.end();
  }
  cli.stop();
  return ok;
}

void urlEncode(const char *in, char *out, int n) {
  int j = 0;
  for (int i = 0; in[i] && j < n - 4; i++) {
    char c = in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out[j++] = c;
    else if (c == ' ') {
      out[j++] = '%';
      out[j++] = '2';
      out[j++] = '0';
    } else if (c == ',') {
      out[j++] = '%';
      out[j++] = '2';
      out[j++] = 'C';
    }
  }
  out[j] = 0;
}

void shortPlace(const char *display, char *out, int n) {
  char buf[140];
  strlcpy(buf, display, sizeof(buf));
  char *parts[6];
  int np = 0;
  parts[np++] = buf;
  for (char *p = buf; *p && np < 6; p++) {
    if (*p == ',') {
      *p = 0;
      char *q = p + 1;
      while (*q == ' ') q++;
      parts[np++] = q;
      p = q - 1;
    }
  }
  for (int i = 0; i < np; i++) {
    int len = (int)strlen(parts[i]);
    while (len && parts[i][len - 1] == ' ') parts[i][--len] = 0;
  }
  bool zip = parts[0][0] && strlen(parts[0]) == 5;
  if (zip) {
    for (int i = 0; parts[0][i]; i++)
      if (parts[0][i] < '0' || parts[0][i] > '9') zip = false;
  }
  if (zip && np >= 4) snprintf(out, n, "%s, %s", parts[1], parts[3]);
  else if (np >= 4) snprintf(out, n, "%s, %s", parts[0], parts[2]);
  else if (np >= 2) snprintf(out, n, "%s, %s", parts[0], parts[1]);
  else strlcpy(out, display, n);
}

void pullFind() {
  char enc[120], url[280];
  urlEncode(placeQuery, enc, sizeof(enc));
  snprintf(url, sizeof(url),
           "https://nominatim.openstreetmap.org/search?q=%s&format=json&limit=4&countrycodes=us&addressdetails=0",
           enc);
  nHits = 0;
  if (!packBuf || !httpGet(url, packBuf, 6000)) {
    strlcpy(placeMsg, "search failed", sizeof(placeMsg));
    needDraw = true;
    return;
  }
  const char *p = packBuf;
  Hit tmp[4];
  int n = 0;
  while (n < 4) {
    const char *latk = strstr(p, "\"lat\":\"");
    if (!latk) break;
    float la = (float)atof(latk + 7);
    const char *lonk = strstr(latk, "\"lon\":\"");
    if (!lonk) break;
    float lo = (float)atof(lonk + 7);
    const char *nk = strstr(lonk, "\"display_name\":\"");
    if (!nk) break;
    char raw[120];
    if (!copyQuoted(nk + 16, raw, sizeof(raw))) break;
    shortPlace(raw, tmp[n].label, sizeof(tmp[n].label));
    tmp[n].lat = la;
    tmp[n].lon = lo;
    n++;
    p = nk + 16;
  }
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  nHits = n;
  for (int i = 0; i < n; i++) hits[i] = tmp[i];
  if (!n) strlcpy(placeMsg, "nothing in the US", sizeof(placeMsg));
  else placeMsg[0] = 0;
  if (dataMux) xSemaphoreGive(dataMux);
  needDraw = true;
}

void pullPack() {
  if (!packBuf || !placeSet) return;
  char url[760];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,surface_pressure,precipitation,is_day,uv_index"
           "&hourly=temperature_2m,precipitation_probability,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_sum,precipitation_probability_max"
           "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch"
           "&timezone=auto&forecast_days=7&forecast_hours=24&past_hours=0",
           placeLat, placeLon);
  if (!httpGet(url, packBuf, 20000)) return;
  const char *cur = strstr(packBuf, "\"current\"");
  const char *hour = strstr(packBuf, "\"hourly\"");
  const char *day = strstr(packBuf, "\"daily\"");
  const char *end = packBuf + strlen(packBuf);
  const char *curEnd = hour ? hour : end;
  const char *hourEnd = day ? day : end;
  char iana[48] = "";
  const char *tz = strstr(packBuf, "\"timezone\":\"");
  if (tz) copyQuoted(tz + 12, iana, sizeof(iana));
  int off = (int)numKey(packBuf, cur ? cur : end, "\"utc_offset_seconds\"");
  float t = numKey(cur, curEnd, "\"temperature_2m\"");
  float feel = numKey(cur, curEnd, "\"apparent_temperature\"");
  float rh = numKey(cur, curEnd, "\"relative_humidity_2m\"");
  float code = numKey(cur, curEnd, "\"weather_code\"");
  float wind = numKey(cur, curEnd, "\"wind_speed_10m\"");
  float dir = numKey(cur, curEnd, "\"wind_direction_10m\"");
  float press = numKey(cur, curEnd, "\"surface_pressure\"");
  float ht[24], hp[24], hc[24], dhi[8], dlo[8], dc[8], dp[8], dr[8];
  for (int i = 0; i < 24; i++) ht[i] = hp[i] = hc[i] = MISS;
  for (int i = 0; i < 8; i++) {
    dhi[i] = dlo[i] = dc[i] = dp[i] = MISS;
    dr[i] = 0;
  }
  int nh = readArr(hour, hourEnd, "\"temperature_2m\"", ht, 24);
  int np = readArr(hour, hourEnd, "\"precipitation_probability\"", hp, 24);
  int nc = readArr(hour, hourEnd, "\"weather_code\"", hc, 24);
  int nd = readArr(day, end, "\"temperature_2m_max\"", dhi, 7);
  readArr(day, end, "\"temperature_2m_min\"", dlo, 7);
  readArr(day, end, "\"weather_code\"", dc, 7);
  readArr(day, end, "\"precipitation_probability_max\"", dp, 7);
  readArr(day, end, "\"precipitation_sum\"", dr, 7);
  char up[28] = "", down[28] = "";
  readStr0(day, end, "\"sunrise\"", up, sizeof(up));
  readStr0(day, end, "\"sunset\"", down, sizeof(down));
  float uv = numKey(cur, curEnd, "\"uv_index\"");
  float isDay = numKey(cur, curEnd, "\"is_day\"");
  if (isDay == 0) uv = 0;
  char up12[16], dn12[16];
  clockFromIso(up, up12, sizeof(up12));
  clockFromIso(down, dn12, sizeof(dn12));
  int wdays[7];
  int base = 0;
  time_t nowt;
  if (liveClock(&nowt)) {
    struct tm tt;
    localtime_r(&nowt, &tt);
    base = tt.tm_wday;
  }
  for (int i = 0; i < 7; i++) wdays[i] = (base + i) % 7;
  const char *tp = nullptr;
  if (day) {
    tp = strstr(day, "\"time\":[");
    if (!tp) tp = strstr(day, "\"time\": [");
  }
  if (tp) {
    const char *q = strchr(tp, '[');
    for (int i = 0; q && i < nd; i++) {
      q = strchr(q, '"');
      if (!q) break;
      int y = atoi(q + 1), m = atoi(q + 6), dd = atoi(q + 9);
      if (y > 2000 && m >= 1 && m <= 12 && dd >= 1 && dd <= 31) wdays[i] = ymdWday(y, m, dd);
      const char *endq = strchr(q + 1, '"');
      if (!endq) break;
      q = endq + 1;
    }
  }
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  if (iana[0] || off != MISS) adoptZone(iana, off == MISS ? 0 : off);
  if (t != MISS) wxTemp = (int)lroundf(t);
  if (feel != MISS) wxFeel = (int)lroundf(feel);
  if (rh != MISS) wxRh = (int)lroundf(rh);
  if (code != MISS) wxCode = (int)lroundf(code);
  if (wind != MISS) wxWind = (int)lroundf(wind);
  if (dir != MISS) wxDir = (int)lroundf(dir);
  if (press > 0) {
    pressHpa = press;
    time_t now = time(nullptr);
    if (pressBase <= 0) {
      pressBase = pressHpa;
      pressStamp = now;
      pressTrend = 0;
    } else {
      float dlt = pressHpa - pressBase;
      pressTrend = dlt > 1.2f ? 1 : (dlt < -1.2f ? -1 : 0);
      if (now > 1700000000 && pressStamp > 0 && now - pressStamp > 10800) {
        pressBase = pressHpa;
        pressStamp = now;
      }
    }
  }
  if (uv != MISS) wxUv = (int)lroundf(uv);
  nHour = nh;
  if (np < nHour) nHour = np;
  if (nc < nHour) nHour = nc;
  for (int i = 0; i < nHour; i++) {
    hourT[i] = (int)lroundf(ht[i]);
    hourPop[i] = hp[i] == MISS ? 0 : (int)lroundf(hp[i]);
    hourCode[i] = (int)lroundf(hc[i]);
  }
  nDay = nd;
  if (nDay > 7) nDay = 7;
  for (int i = 0; i < nDay; i++) {
    dayHi[i] = (int)lroundf(dhi[i]);
    dayLo[i] = (int)lroundf(dlo[i]);
    dayCode[i] = (int)lroundf(dc[i]);
    dayPop[i] = dp[i] == MISS ? -1 : (int)lroundf(dp[i]);
    dayRain[i] = dr[i] == MISS ? 0 : dr[i];
    dayWday[i] = wdays[i];
  }
  if (nDay > 0) rainToday = dayRain[0];
  strlcpy(sunUp, up12, sizeof(sunUp));
  strlcpy(sunDown, dn12, sizeof(sunDown));
  wxOk = t != MISS;
  if (dataMux) xSemaphoreGive(dataMux);
  savePrefs();
  netPaint = true;
}

void pullAqi() {
  if (!packBuf || !placeSet) return;
  char url[180];
  snprintf(url, sizeof(url),
           "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f&current=us_aqi",
           placeLat, placeLon);
  if (!httpGet(url, packBuf, 2500)) return;
  float a = numKey(packBuf, packBuf + strlen(packBuf), "\"us_aqi\"");
  if (a == MISS) return;
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  wxAqi = (int)lroundf(a);
  if (dataMux) xSemaphoreGive(dataMux);
  netPaint = true;
}

void pullAlert() {
  if (!packBuf || !placeSet) return;
  char url[160];
  snprintf(url, sizeof(url), "https://api.weather.gov/alerts/active?point=%.4f,%.4f", placeLat, placeLon);
  bool on = false;
  char ev[40] = "", ends[28] = "";
  if (httpGet(url, packBuf, 8000)) {
    const char *e = strstr(packBuf, "\"event\":\"");
    if (e && copyQuoted(e + 9, ev, sizeof(ev))) {
      on = true;
      const char *z = strstr(e, "\"ends\":\"");
      if (z && z < e + 800) {
        char iso[32];
        if (copyQuoted(z + 8, iso, sizeof(iso))) clockFromIso(iso, ends, sizeof(ends));
      }
    }
  }
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  alertOn = on;
  strlcpy(alertEvent, ev, sizeof(alertEvent));
  strlcpy(alertEnds, ends, sizeof(alertEnds));
  if (dataMux) xSemaphoreGive(dataMux);
  netPaint = true;
}

Plane scratchPlane;

void fillScratch(const char *obj, const char *end) {
  Plane *p = &scratchPlane;
  memset(p, 0, sizeof(*p));
  const char *f = strstr(obj, "\"flight\":");
  if (f && f < end && f[9] == '"') copyQuoted(f + 10, p->call, sizeof(p->call));
  int n = (int)strlen(p->call);
  while (n && p->call[n - 1] == ' ') p->call[--n] = 0;
  const char *hx = strstr(obj, "\"hex\":\"");
  if (hx && hx < end) copyQuoted(hx + 7, p->hex, sizeof(p->hex));
  const char *ds = strstr(obj, "\"desc\":\"");
  if (ds && ds < end) copyQuoted(ds + 8, p->kind, sizeof(p->kind));
  if (!p->kind[0]) {
    const char *tp = strstr(obj, "\"t\":\"");
    if (tp && tp < end) copyQuoted(tp + 5, p->kind, sizeof(p->kind));
  }
  const char *ab = strstr(obj, "\"alt_baro\":");
  if (ab && ab < end) {
    const char *v = ab + 11;
    if (*v == '"') p->gnd = true;
    else p->alt = (int)atof(v);
  }
  float gs = numKey(obj, end, "\"gs\"");
  float tr = numKey(obj, end, "\"track\"");
  float la = numKey(obj, end, "\"lat\"");
  float lo = numKey(obj, end, "\"lon\"");
  if (gs != MISS) p->gs = (int)lroundf(gs);
  if (tr != MISS) p->track = (int)lroundf(tr);
  if (la != MISS && lo != MISS) {
    p->lat = la;
    p->lon = lo;
    p->hasPos = true;
    p->dist = distKm(placeLat, placeLon, la, lo) / 1.852f;
  } else p->dist = 99999;
}

bool pullPlanesUrl(const char *fmt) {
  if (!packBuf || !placeSet) return false;
  char url[160];
  snprintf(url, sizeof(url), fmt, placeLat, placeLon, rangeNm);
  if (!httpGet(url, packBuf, 16000)) return false;
  Plane tmp[12];
  int n = 0;
  const char *p = strstr(packBuf, "\"ac\"");
  if (p) p = strchr(p, '[');
  if (p) {
    while (n < 12 && p) {
      const char *obj = strchr(p, '{');
      if (!obj) break;
      const char *e = obj;
      int depth = 0;
      for (const char *q = obj; *q && q < obj + 1800; q++) {
        if (*q == '{') depth++;
        else if (*q == '}') {
          if (--depth == 0) {
            e = q;
            break;
          }
        }
      }
      if (e == obj) break;
      fillScratch(obj, e);
      if (scratchPlane.call[0] || scratchPlane.hex[0]) tmp[n++] = scratchPlane;
      p = e + 1;
    }
  }
  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      if (tmp[j].dist < tmp[i].dist) {
        Plane s = tmp[i];
        tmp[i] = tmp[j];
        tmp[j] = s;
      }
    }
  }
  int want = planePick;
  if (want >= 0 && want < n && !tmp[want].kind[0] && tmp[want].hex[0]) {
    static char typeMiss[8] = "";
    if (strcmp(tmp[want].hex, typeMiss)) {
      char hex[8], look[96];
      strlcpy(hex, tmp[want].hex, sizeof(hex));
      snprintf(look, sizeof(look), "https://api.adsbdb.com/v0/aircraft/%s", hex);
      if (httpGet(look, packBuf, 4000)) {
        const char *k = strstr(packBuf, "\"icao_type\":\"");
        if (k) copyQuoted(k + 13, tmp[want].kind, sizeof(tmp[want].kind));
      }
      if (!tmp[want].kind[0]) strlcpy(typeMiss, hex, sizeof(typeMiss));
    }
  }
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  nPlane = n;
  for (int i = 0; i < n; i++) planes[i] = tmp[i];
  planesOk = true;
  if (dataMux) xSemaphoreGive(dataMux);
  netPaint = true;
  return true;
}

void pullPlanes() {
  if (pullPlanesUrl("https://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/%d")) return;
  pullPlanesUrl("https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d");
}

void pullQuake() {
  if (!packBuf || !placeSet) return;
  char day[48] = "";
  time_t now = 0;
  if (liveClock(&now)) {
    time_t start = now - 86400;
    struct tm t;
    gmtime_r(&start, &t);
    snprintf(day, sizeof(day), "&starttime=%04d-%02d-%02dT%02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }
  Quake tmp[24];
  int n = 0;
  bool got = false;
  char url[420];
  for (int pass = 0; pass < 3; pass++) {
    if (pass == 0) {
      snprintf(url, sizeof(url),
               "https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&orderby=magnitude&limit=8&minmagnitude=4.5%s",
               day);
    } else if (pass == 1) {
      snprintf(url, sizeof(url),
               "https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&latitude=%.4f&longitude=%.4f&maxradiuskm=800&minmagnitude=2.5&orderby=time&limit=8%s",
               placeLat, placeLon, day);
    } else {
      snprintf(url, sizeof(url),
               "https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&minlatitude=15&maxlatitude=72&minlongitude=-180&maxlongitude=-65&orderby=time&limit=8%s",
               day);
    }
    if (!httpGet(url, packBuf, 14000)) continue;
    got = true;
    const char *p = packBuf;
    while (n < 24) {
      const char *c = strstr(p, "\"coordinates\":[");
      if (!c) break;
      float lon = (float)atof(c + 15);
      const char *c1 = strchr(c + 15, ',');
      if (!c1) break;
      float lat = (float)atof(c1 + 1);
      const char *c2 = strchr(c1 + 1, ',');
      float depth = c2 ? (float)atof(c2 + 1) : 0;
      bool same = false;
      for (int k = 0; k < n; k++) {
        float dlat = tmp[k].lat - lat;
        float dlon = tmp[k].lon - lon;
        if (dlat < 0.2f && dlat > -0.2f && dlon < 0.2f && dlon > -0.2f) same = true;
      }
      if (same) {
        p = c + 15;
        continue;
      }
      float mag = numKey(p, c, "\"mag\"");
      const char *pk = strstr(p, "\"place\":\"");
      char where[48] = "";
      if (pk && pk < c) copyQuoted(pk + 9, where, sizeof(where));
      float ms = numKey(p, c, "\"time\"");
      tmp[n].mag = mag == MISS ? 0 : mag;
      tmp[n].lat = lat;
      tmp[n].lon = lon;
      tmp[n].depth = depth;
      tmp[n].when = ms == MISS ? 0 : (time_t)(ms / 1000.0f);
      tmp[n].miles = (int)lroundf(distKm(placeLat, placeLon, lat, lon) * 0.621371f);
      strlcpy(tmp[n].place, where, sizeof(tmp[n].place));
      n++;
      p = c + 15;
    }
  }
  if (!got) return;
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  nQuake = n;
  for (int i = 0; i < n; i++) quakes[i] = tmp[i];
  quakeOk = true;
  if (dataMux) xSemaphoreGive(dataMux);
  netPaint = true;
}

double tleJd(int year, double doy) {
  int y = year;
  int m = 1;
  if (m <= 2) {
    y -= 1;
    m += 12;
  }
  int A = y / 100;
  int B = 2 - A + A / 4;
  double jd = floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + 1 + B - 1524.5;
  return jd + (doy - 1.0);
}

void loadCraft(int which, const char *l1, const char *l2) {
  if (which < 0 || which > 2) return;
  if (!l1 || !l2 || strlen(l1) < 32 || strlen(l2) < 63) return;
  int yy = atoi(l1 + 18);
  double doy = atof(l1 + 20);
  int year = yy < 57 ? 2000 + yy : 1900 + yy;
  char ecc[12];
  memcpy(ecc, "0.", 2);
  memcpy(ecc + 2, l2 + 26, 7);
  ecc[9] = 0;
  satEp[which] = tleJd(year, doy);
  satN[which] = atof(l2 + 52);
  satEcc[which] = atof(ecc);
  satInc[which] = atof(l2 + 8);
  satRaan[which] = atof(l2 + 17);
  satArg[which] = atof(l2 + 34);
  satM0[which] = atof(l2 + 43);
  satOrbOk[which] = satN[which] > 1;
}

void loadIssLines(const char *l1, const char *l2) { loadCraft(0, l1, l2); }

bool craftPosAt(int which, time_t t, float *lat, float *lon, float *altKm) {
  if (which < 0 || which > 2 || !satOrbOk[which]) return false;
  double jd = 2440587.5 + (double)t / 86400.0;
  double dt = jd - satEp[which];
  double n = satN[which] * 2.0 * M_PI;
  double M = satM0[which] * DEG + n * dt;
  double E = M;
  for (int i = 0; i < 7; i++) E = M + satEcc[which] * sin(E);
  double nu = 2 * atan2(sqrt(1 + satEcc[which]) * sin(E / 2), sqrt(1 - satEcc[which]) * cos(E / 2));
  double ns = satN[which] * 2.0 * M_PI / 86400.0;
  double a = pow(398600.4418 / (ns * ns), 1.0 / 3.0);
  double r = a * (1 - satEcc[which] * cos(E));
  double inc = satInc[which] * DEG;
  double cosi = cos(inc);
  double raanDot = -1.5 * ns * 1.08263e-3 * pow(6378.137 / a, 2) * cosi / pow(1 - satEcc[which] * satEcc[which], 2);
  double raan = satRaan[which] * DEG + raanDot * dt * 86400.0;
  double arg = satArg[which] * DEG + nu;
  double x = r * cos(arg), y = r * sin(arg);
  double ecix = x * cos(raan) - y * cosi * sin(raan);
  double eciy = x * sin(raan) + y * cosi * cos(raan);
  double eciz = y * sin(inc);
  double gmst = wrap360(280.46061837 + 360.98564736629 * (jd - 2451545.0));
  double g = gmst * DEG;
  double ex = ecix * cos(g) + eciy * sin(g);
  double ey = -ecix * sin(g) + eciy * cos(g);
  *lon = (float)(atan2(ey, ex) * 57.2957795);
  *lat = (float)(atan2(eciz, sqrt(ex * ex + ey * ey)) * 57.2957795);
  *altKm = (float)(sqrt(ex * ex + ey * ey + eciz * eciz) - 6371.0);
  return true;
}

bool issPosAt(time_t t, float *lat, float *lon, float *altKm) {
  return craftPosAt(0, t, lat, lon, altKm);
}

int craftMph(int which, float altKm) {
  if (which < 0 || which > 2 || satN[which] < 1) return 0;
  double ns = satN[which] * 2.0 * M_PI / 86400.0;
  double a = 6371.0 + (altKm > 50 ? altKm : 400);
  return (int)lroundf((float)(ns * a * 3600.0 * 0.621371));
}

void sunAltAt(time_t t, float *altOut) {
  double jd = 2440587.5 + (double)t / 86400.0;
  double T = (jd - 2451545.0) / 36525.0;
  double L = wrap360(280.460 + 36000.770 * T);
  double M = wrap360(357.528 + 35999.050 * T);
  double Mr = M * DEG;
  double lam = L + 1.915 * sin(Mr) + 0.020 * sin(2 * Mr);
  double eps = (23.439 - 0.013 * T) * DEG;
  double lr = lam * DEG;
  double ra = atan2(cos(eps) * sin(lr), cos(lr));
  double dec = asin(sin(eps) * sin(lr));
  int az, el;
  altAz(ra, dec, t, &az, &el);
  if (altOut) *altOut = (float)el;
}

bool craftLookAt(int which, time_t t, int *az, int *el) {
  float la, lo, al;
  if (!craftPosAt(which, t, &la, &lo, &al)) return false;
  double la1 = placeLat * DEG, lo1 = placeLon * DEG;
  double la2 = la * DEG, lo2 = lo * DEG, dlo = lo2 - lo1;
  double by = sin(dlo) * cos(la2);
  double bx = cos(la1) * sin(la2) - sin(la1) * cos(la2) * cos(dlo);
  double azr = atan2(by, bx);
  if (azr < 0) azr += 2 * M_PI;
  double cosg = sin(la1) * sin(la2) + cos(la1) * cos(la2) * cos(dlo);
  if (cosg > 1) cosg = 1;
  if (cosg < -1) cosg = -1;
  double gamma = acos(cosg);
  double elr = atan2(cos(gamma) - 6371.0 / (6371.0 + al), sin(gamma));
  *az = (int)lround(azr * 180.0 / M_PI);
  *el = (int)lround(elr * 180.0 / M_PI);
  return true;
}

bool craftVisAt(int which, time_t t, int *az, int *el) {
  if (!craftLookAt(which, t, az, el)) return false;
  float sunA = 0;
  sunAltAt(t, &sunA);
  return *el >= 10 && sunA < -4;
}

void buildCraftPath(int which) {
  float *lats = which == 0 ? issTrackLat : tgTrackLat;
  float *lons = which == 0 ? issTrackLon : tgTrackLon;
  int *nOut = which == 0 ? &nIssTrack : &nTgTrack;
  *nOut = 0;
  time_t now;
  if (which < 0 || which > 1 || !satOrbOk[which] || !liveClock(&now)) return;
  double period = 86400.0 / satN[which];
  for (int i = 0; i < 64; i++) {
    float la, lo, al;
    if (!craftPosAt(which, now + (time_t)(period * i / 64.0), &la, &lo, &al)) break;
    lats[*nOut] = la;
    lons[*nOut] = lo;
    (*nOut)++;
  }
}

void addCraftPasses(int which, const char *tag, int room) {
  time_t now;
  if (!satOrbOk[which] || !placeSet || !liveClock(&now) || room < 1) return;
  bool inPass = false;
  time_t start = 0;
  char rise[16] = "";
  int added = 0;
  for (int s = 0; s < 7 * 24 * 30 && added < room && nIssPass < 6; s++) {
    if ((s & 31) == 0) vTaskDelay(pdMS_TO_TICKS(1));
    time_t t = now + (time_t)s * 120;
    int az = 0, el = 0;
    bool vis = craftVisAt(which, t, &az, &el);
    if (vis && !inPass) {
      inPass = true;
      start = t;
      strlcpy(rise, lookWord(az), sizeof(rise));
    } else if (!vis && inPass) {
      int dur = (int)(t - start);
      if (dur >= 60) {
        struct tm ti;
        localtime_r(&start, &ti);
        int h = ti.tm_hour % 12;
        if (!h) h = 12;
        snprintf(issPassLine[nIssPass], 56, "%s  %s %d  %d:%02d %s  from %s  %d min", tag, WDAY[ti.tm_wday], ti.tm_mday, h,
                 ti.tm_min, ti.tm_hour < 12 ? "am" : "pm", rise, dur / 60);
        nIssPass++;
        added++;
      }
      inPass = false;
      s += 4;
    }
  }
}

void buildIssPasses() {
  nIssPass = 0;
  addCraftPasses(0, "ISS", 2);
  addCraftPasses(1, "Tiangong", 2);
}

void pullCrewCounts() {
  if (!packBuf) return;
  if (lastCrew && millis() - lastCrew < 21600000UL) return;
  if (!httpGet("https://corquaid.github.io/international-space-station-APIs/JSON/people-in-space.json", packBuf, 18000)) return;
  if (!strstr(packBuf, "\"spacecraft\"")) return;
  int nIss = 0, nTg = 0;
  const char *p = packBuf;
  while ((p = strstr(p, "\"spacecraft\""))) {
    const char *nx = strstr(p + 12, "\"spacecraft\"");
    const char *colon = strchr(p, ':');
    char ship[28] = "";
    if (colon && (!nx || colon < nx)) {
      const char *q = strchr(colon, '"');
      if (q && (!nx || q < nx)) copyQuoted(q + 1, ship, sizeof(ship));
    }
    bool onIss = false;
    const char *flag = strstr(p, "\"iss\"");
    if (flag && (!nx || flag < nx)) {
      const char *ic = strchr(flag, ':');
      if (ic && (!nx || ic < nx)) {
        const char *v = ic + 1;
        while (*v == ' ' || *v == '\n' || *v == '\r') v++;
        onIss = !strncmp(v, "true", 4);
      }
    }
    if (onIss) nIss++;
    else if (strstr(ship, "Shenzhou") || strstr(ship, "Tiangong") || strstr(ship, "CSS")) nTg++;
    p += 12;
  }
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  issCrewN = nIss;
  tgCrewN = nTg;
  lastCrew = millis();
  if (dataMux) xSemaphoreGive(dataMux);
}

void pullIss() {
  if (!satOrbOk[0]) loadCraft(0, SATS[0].l1, SATS[0].l2);
  if (!satOrbOk[1]) loadCraft(1, SATS[1].l1, SATS[1].l2);
  if (!satOrbOk[2]) loadCraft(2, SATS[2].l1, SATS[2].l2);
  if (packBuf && (!lastTle || millis() - lastTle > 21600000UL)) {
    if (httpGet("https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE", packBuf, 400)) {
      const char *l1 = strstr(packBuf, "\n1 ");
      if (l1) l1++;
      else if (packBuf[0] == '1') l1 = packBuf;
      const char *l2 = l1 ? strchr(l1, '\n') : nullptr;
      if (l2) {
        l2++;
        if (*l2 == '\r') l2++;
        if (*l2 == '2') {
          loadIssLines(l1, l2);
          lastTle = millis();
        }
      }
    }
  }
  bool live = false;
  float la = 0, lo = 0, al = 420;
  if (packBuf && httpGet("https://api.wheretheiss.at/v1/satellites/25544", packBuf, 800)) {
    la = numKey(packBuf, packBuf + strlen(packBuf), "\"latitude\"");
    lo = numKey(packBuf, packBuf + strlen(packBuf), "\"longitude\"");
    al = numKey(packBuf, packBuf + strlen(packBuf), "\"altitude\"");
    float vl = numKey(packBuf, packBuf + strlen(packBuf), "\"velocity\"");
    if (la != MISS && lo != MISS) {
      live = true;
      if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
      issLat = la;
      issLon = lo;
      issAlt = al == MISS ? 400 : al;
      issVel = vl == MISS ? 0 : vl;
      issOk = true;
      if (dataMux) xSemaphoreGive(dataMux);
    }
  }
  if (!live && satOrbOk[0]) {
    time_t now;
    if (liveClock(&now) && issPosAt(now, &la, &lo, &al)) {
      if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
      issLat = la;
      issLon = lo;
      issAlt = al;
      issVel = (float)craftMph(0, al) / 0.621371f;
      issOk = true;
      if (dataMux) xSemaphoreGive(dataMux);
      live = true;
    }
  }
  if (live && placeSet) {
    int az = 0, el = -90;
    issLook(&az, &el);
    float sunA = 20;
    time_t now;
    if (liveClock(&now)) sunAltAt(now, &sunA);
    issAzNow = az;
    issElNow = el;
    issVisNow = el >= 10 && sunA < -4;
  }
  {
    time_t now;
    float tla = 0, tlo = 0, tal = 0;
    if (liveClock(&now) && craftPosAt(1, now, &tla, &tlo, &tal)) {
      int az = 0, el = -90;
      bool vis = placeSet && craftVisAt(1, now, &az, &el);
      if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
      tgLat = tla;
      tgLon = tlo;
      tgAlt = tal;
      tgOk = true;
      tgVisNow = vis;
      if (dataMux) xSemaphoreGive(dataMux);
    }
  }
  buildCraftPath(0);
  buildCraftPath(1);
  if (placeSet && satOrbOk[0] && (!lastPassBuild || millis() - lastPassBuild > 1800000UL)) {
    buildIssPasses();
    lastPassBuild = millis();
  }
  pullCrewCounts();
  netPaint = true;
}

void pullLaunch() {
  if (!packBuf) return;
  if (!httpGet("https://ll.thespacedevs.com/2.2.0/launch/upcoming/?limit=4&mode=list", packBuf, 12000)) return;
  Launch tmp[4];
  int n = 0;
  const char *p = packBuf;
  while (n < 4) {
    const char *nm = strstr(p, "\"name\":\"");
    if (!nm) break;
    char name[44];
    if (!copyQuoted(nm + 8, name, sizeof(name))) break;
    const char *net = strstr(nm, "\"net\":\"");
    char when[24] = "";
    if (net && net < nm + 500) {
      char iso[32];
      if (copyQuoted(net + 7, iso, sizeof(iso))) {
        snprintf(when, sizeof(when), "%.16s", iso);
      }
    }
    char where[40] = "";
    const char *loc = strstr(nm, "\"location\":\"");
    if (loc && loc < nm + 2200) copyQuoted(loc + 12, where, sizeof(where));
    if (!where[0]) {
      const char *pad = strstr(nm, "\"pad\":\"");
      if (pad && pad < nm + 2200) copyQuoted(pad + 7, where, sizeof(where));
    }
    char *comma = strchr(where, ',');
    if (comma) *comma = 0;
    if (!when[0]) where[0] = 0;
    if (name[0] && strcmp(name, "name")) {
      strlcpy(tmp[n].name, name, sizeof(tmp[n].name));
      strlcpy(tmp[n].when, when, sizeof(tmp[n].when));
      strlcpy(tmp[n].where, where, sizeof(tmp[n].where));
      n++;
    }
    p = nm + 8;
  }
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  nLaunch = n;
  for (int i = 0; i < n; i++) launches[i] = tmp[i];
  launchOk = true;
  if (dataMux) xSemaphoreGive(dataMux);
  netPaint = true;
}

void pullKp() {
  if (!packBuf) return;
  if (!httpGet("https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json", packBuf, 4000)) return;
  const char *p = packBuf, *hit = nullptr, *q;
  while ((q = strstr(p, "\"20")) != nullptr) {
    hit = q;
    p = q + 3;
  }
  if (!hit) return;
  const char *comma = strchr(hit, ',');
  if (!comma) return;
  float k = (float)atof(comma + 1);
  if (k < 0 || k > 9) return;
  if (dataMux) xSemaphoreTake(dataMux, portMAX_DELAY);
  kp = k;
  kpOk = true;
  if (dataMux) xSemaphoreGive(dataMux);
  netPaint = true;
}

int verPart(const char **p) {
  int v = atoi(*p);
  while (**p && **p != '.') (*p)++;
  if (**p == '.') (*p)++;
  return v;
}

bool verNewer(const char *remote, const char *local) {
  const char *a = remote, *b = local;
  for (int i = 0; i < 3; i++) {
    int av = verPart(&a), bv = verPart(&b);
    if (av != bv) return av > bv;
  }
  return false;
}

void pullOtaCheck() {
  if (!packBuf) return;
  if (!httpGet(MERID_VER_URL, packBuf, 64)) {
    strlcpy(otaMsg, "check failed", sizeof(otaMsg));
    needDraw = true;
    return;
  }
  trimEnd(packBuf);
  strlcpy(otaLatest, packBuf, sizeof(otaLatest));
  otaNewer = verNewer(otaLatest, MERID_VER);
  if (otaNewer) snprintf(otaMsg, sizeof(otaMsg), "newer %s is ready", otaLatest);
  else strlcpy(otaMsg, "newest firmware already installed", sizeof(otaMsg));
  needDraw = true;
}

void pullOtaInstall() {
  if (!otaNewer) {
    strlcpy(otaMsg, "check first", sizeof(otaMsg));
    needDraw = true;
    return;
  }
  const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
  if (!part) {
    strlcpy(otaMsg, "needs a second slot", sizeof(otaMsg));
    needDraw = true;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(otaMsg, "join wifi first", sizeof(otaMsg));
    needDraw = true;
    return;
  }
  otaBusy = true;
  otaPct = 0;
  needDraw = true;
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  bool good = false;
  if (http.begin(cli, MERID_BIN_URL)) {
    http.setTimeout(20000);
    http.addHeader("User-Agent", "MeridianScreen/1.1");
    int code = http.GET();
    int total = http.getSize();
    if (code == 200 && Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) {
      int got = 0;
      uint8_t buf[1024];
      uint32_t t0 = millis();
      while (millis() - t0 < 120000) {
        int n = http.getStream().available();
        if (n > 0) {
          if (n > 1024) n = 1024;
          int r = http.getStream().readBytes(buf, n);
          if (r <= 0) break;
          if (Update.write(buf, r) != (size_t)r) break;
          got += r;
          if (total > 0) otaPct = got * 100 / total;
          delay(1);
          if (total > 0 && got >= total) {
            good = true;
            break;
          }
          continue;
        }
        if (!http.connected()) {
          good = got > 100000;
          break;
        }
        delay(2);
      }
      if (good) good = Update.end(true);
      else Update.abort();
    }
    http.end();
  }
  cli.stop();
  otaBusy = false;
  if (good) {
    strlcpy(otaMsg, "restarting", sizeof(otaMsg));
    otaPct = 100;
    needDraw = true;
    delay(600);
    ESP.restart();
  } else {
    otaPct = -1;
    strlcpy(otaMsg, "update failed", sizeof(otaMsg));
    needDraw = true;
  }
}

void considerTide(const char *win, const char *latp) {
  float lat = (float)atof(latp + 6);
  const char *lngp = strstr(latp, "\"lng\":");
  if (!lngp || lngp > latp + 90) return;
  float lng = (float)atof(lngp + 6);
  const char *id = nullptr;
  for (const char *q = latp; q > win && latp - q < 220; q--) {
    if (!strncmp(q, "\"id\":", 5)) {
      id = q + 5;
      while (*id == ' ') id++;
      if (*id == '"') id++;
      break;
    }
  }
  if (!id) return;
  float km = distKm(placeLat, placeLon, lat, lng);
  if (km > 160.0f) return;
  if (tideId[0] && km >= tideBestKm) return;
  tideBestKm = km;
  int i = 0;
  while (id[i] && id[i] != '"' && i < (int)sizeof(tideId) - 1) {
    tideId[i] = id[i];
    i++;
  }
  tideId[i] = 0;
  tideKnown = tideId[0] != 0;
}

void pullTide() {
  if (!showTide || !placeSet || !packBuf) return;
  if (!tideId[0]) {
    WiFiClientSecure cli;
    cli.setInsecure();
    HTTPClient http;
    if (http.begin(cli, "https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=tidepredictions")) {
      http.setTimeout(20000);
      http.addHeader("User-Agent", "MeridianScreen/1.1");
      if (http.GET() == 200) {
        char win[900];
        int fill = 0;
        uint32_t t0 = millis();
        int got = 0;
        while (got < 1500000 && millis() - t0 < 20000) {
          int room = (int)sizeof(win) - 1 - fill;
          if (room < 120 && fill > 180) {
            memmove(win, win + fill - 180, 180);
            fill = 180;
            win[fill] = 0;
            room = (int)sizeof(win) - 1 - fill;
          }
          int n = http.getStream().available();
          if (n <= 0) {
            if (!http.connected()) break;
            delay(2);
            continue;
          }
          if (n > 1024) n = 1024;
          if (n > room) n = room;
          int r = http.getStream().readBytes(win + fill, n);
          if (r <= 0) break;
          fill += r;
          got += r;
          win[fill] = 0;
          char *p = win;
          while ((p = strstr(p, "\"lat\":"))) {
            considerTide(win, p);
            p += 6;
          }
          delay(1);
          if (tideKnown && tideBestKm < 40.0f) break;
        }
      }
      http.end();
    }
    cli.stop();
    tideDone = true;
    if (tideKnown) savePrefs();
  }
  if (!tideId[0]) {
    netPaint = true;
    return;
  }
  time_t now = 0;
  if (!liveClock(&now)) now = time(nullptr);
  struct tm begin;
  localtime_r(&now, &begin);
  char url[300];
  snprintf(url, sizeof(url),
           "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?begin_date=%04d%02d%02d&range=48"
           "&station=%s&product=predictions&datum=MLLW&time_zone=lst_ldt&interval=hilo"
           "&units=english&format=json&application=MeridianScreen",
           begin.tm_year + 1900, begin.tm_mon + 1, begin.tm_mday, tideId);
  if (httpGet(url, packBuf, 1800)) {
    tideHigh[0] = tideLow[0] = 0;
    long nowP = (begin.tm_year + 1900) * 100000000L + (begin.tm_mon + 1) * 1000000L + begin.tm_mday * 10000L +
                begin.tm_hour * 100L + begin.tm_min;
    const char *p = packBuf;
    while ((p = strstr(p, "\"t\":\""))) {
      int y, mo, d, h, mi;
      if (sscanf(p + 5, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) < 5) {
        p += 5;
        continue;
      }
      const char *ty = strstr(p, "\"type\":\"");
      if (!ty || ty > p + 90) break;
      char kind = ty[8];
      float v = 0;
      const char *vv = strstr(p, "\"v\":\"");
      if (vv && vv < ty) v = (float)atof(vv + 5);
      long tp = y * 100000000L + mo * 1000000L + d * 10000L + h * 100L + mi;
      if (tp + 30 < nowP) {
        p = ty + 8;
        continue;
      }
      int hr = h % 12;
      if (!hr) hr = 12;
      char line[28];
      snprintf(line, sizeof(line), "%s %d:%02d %s %.1f", kind == 'H' ? "high" : "low", hr, mi, h < 12 ? "am" : "pm", v);
      if (kind == 'H' && !tideHigh[0]) strlcpy(tideHigh, line, sizeof(tideHigh));
      if (kind == 'L' && !tideLow[0]) strlcpy(tideLow, line, sizeof(tideLow));
      if (tideHigh[0] && tideLow[0]) break;
      p = ty + 8;
    }
  }
  netPaint = true;
}

void netTask(void *) {
  for (;;) {
    if (wifiBusy) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    int job = takeJob(quietPage());
    if (!job) {
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }
    if (quietPage() && job != NET_FIND) {
      askNet(job);
      vTaskDelay(pdMS_TO_TICKS(80));
      continue;
    }
    netBusy = true;
    if (job == NET_PACK) pullPack();
    else if (job == NET_AQI) pullAqi();
    else if (job == NET_ALERT) pullAlert();
    else if (job == NET_FIND) pullFind();
    else if (job == NET_PLANE) pullPlanes();
    else if (job == NET_QUAKE) pullQuake();
    else if (job == NET_ISS) pullIss();
    else if (job == NET_LAUNCH) pullLaunch();
    else if (job == NET_KP) pullKp();
    else if (job == NET_TIDE) pullTide();
    else if (job == NET_OTA) {
      if (!otaLatest[0]) pullOtaCheck();
      else pullOtaInstall();
    }
    netBusy = false;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

int gt911Touches(int *xs, int *ys, int maxN) {
  I2cHold hold;
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81);
  Wire.write(0x4E);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)GT911_ADDR, (uint8_t)1) != 1) return 0;
  uint8_t st = Wire.read();
  if ((st & 0x80) == 0) return 0;
  int n = st & 0x0F;
  if (n == 0) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0);
    Wire.endTransmission();
    return 0;
  }
  if (n > maxN) n = maxN;
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81);
  Wire.write(0x50);
  if (Wire.endTransmission(false) != 0) return 0;
  uint8_t want = (uint8_t)(n * 8);
  if (Wire.requestFrom((uint8_t)GT911_ADDR, want) != want) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0);
    Wire.endTransmission();
    return 0;
  }
  uint8_t buf[16];
  for (int i = 0; i < want && i < 16; i++) buf[i] = Wire.read();
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0);
  Wire.endTransmission();
  for (int i = 0; i < n; i++) {
    int rx = buf[i * 8] | (buf[i * 8 + 1] << 8);
    int ry = buf[i * 8 + 2] | (buf[i * 8 + 3] << 8);
    if (rx < 0) rx = 0;
    if (ry < 0) ry = 0;
    if (rx > PW - 1) rx = PW - 1;
    if (ry > PH - 1) ry = PH - 1;
    xs[i] = rx;
    ys[i] = ry;
  }
  return n;
}

void typeInto(char *dest, int cap, int code, char ch) {
  int n = (int)strlen(dest);
  if (code == 1) {
    if (n) dest[n - 1] = 0;
  } else if (code == 2) {
    if (n < cap - 1) {
      dest[n] = ' ';
      dest[n + 1] = 0;
    }
  } else if (code == 0 && ch && n < cap - 1) {
    dest[n] = ch;
    dest[n + 1] = 0;
  }
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

int quakeAt(int x, int y) {
  bool photo = quakePhoto;
  for (int i = 0; i < nQuake; i++) {
    int px = 0, py = 0;
    bool on = false;
    if (photo && quakeUsa) on = usaDot(quakes[i].lat, quakes[i].lon, &px, &py);
    else if (photo) on = worldDot(quakes[i].lat, quakes[i].lon, 52, 200, &px, &py);
    if (!on) continue;
    int dx = x - px;
    int dy = y - py;
    if (dx * dx + dy * dy <= 22 * 22) return i;
  }
  int y0 = photo ? (quakeUsa ? 340 : 260) : 254;
  int maxRows = photo ? (quakeUsa ? 2 : 4) : 5;
  int shown = 0;
  for (int i = 0; i < nQuake && shown < maxRows; i++) {
    if (photo && quakeUsa) {
      int px, py;
      if (!usaDot(quakes[i].lat, quakes[i].lon, &px, &py)) continue;
    }
    int yy = y0 + shown * 42;
    shown++;
    if (yy > 430) break;
    if (inRect(x, y, photo ? 8 : X0, yy, photo ? 784 : 676, 38)) return i;
  }
  return -1;
}

void onTap(int x, int y) {
  if (otaBusy) return;
  if (x < RAIL && !(page == PG_QUAKE && quakePhoto) && page != PG_ISS) {
    int i = y / 96;
    if (i < 0) i = 0;
    if (i > 4) i = 4;
    const int dest[] = {PG_NOW, PG_WEEK, PG_AIR, PG_SKY, PG_MORE};
    page = dest[i];
    needDraw = true;
    return;
  }
  if ((page == PG_PLACE || page == PG_WIFI) && y >= 300) {
    buildKeys();
    for (int i = 0; i < nKeys; i++) {
      if (!inRect(x, y, keyX[i], keyY[i], keyW[i], keyH[i])) continue;
      if (keyCode[i] == 3) caps = !caps;
      else if (keyCode[i] == 4) {
        symbols = !symbols;
        if (symbols) caps = false;
      } else if (page == PG_PLACE) typeInto(placeQuery, sizeof(placeQuery), keyCode[i], keyCh[i]);
      else {
        if (focus != 1 && focus != 2) focus = 1;
        if (focus == 1) typeInto(wifiSsid, sizeof(wifiSsid), keyCode[i], keyCh[i]);
        else typeInto(wifiPass, sizeof(wifiPass), keyCode[i], keyCh[i]);
      }
      needDraw = true;
      return;
    }
  }
  if (page == PG_NOW) {
    if (pinAlerts && pinOn && inRect(x, y, X0 + 520, 424, 140, 36)) {
      pinOn = false;
      strlcpy(pinSeen, pinText, sizeof(pinSeen));
      savePrefs();
      needDraw = true;
      return;
    }
    if (quakePin >= 0 && quakePin < nQuake && inRect(x, y, X0, 418, 500, 52)) {
      quakeBack = PG_NOW;
      page = PG_QFACT;
      needDraw = true;
      return;
    }
    int was = page;
    if (!wifiOk && inRect(x, y, X0 + 16, 248, 200, 28)) page = PG_WIFI;
    else if (wifiOk && !placeSet && inRect(x, y, X0 + 16, 248, 200, 28)) page = PG_PLACE;
    if (page != was) needDraw = true;
    return;
  }
  if (page == PG_AIR) {
    const int ranges[] = {20, 40, 80};
    for (int i = 0; i < 3; i++) {
      if (inRect(x, y, 500 + i * 96, 8, 88, 36) && rangeNm != ranges[i]) {
        rangeNm = ranges[i];
        planesOk = false;
        nPlane = 0;
        planePick = -1;
        savePrefs();
        askNet(NET_PLANE);
        lastPlane = millis();
        needDraw = true;
      }
    }
    int shown = nPlane < 8 ? nPlane : 8;
    for (int i = 0; i < shown; i++) {
      if (!inRect(x, y, X0, 56 + i * 46, 360, 42)) continue;
      planePick = i;
      if (!planes[i].kind[0]) {
        askNet(NET_PLANE);
        lastPlane = millis();
      }
      needDraw = true;
      return;
    }
    int cx = 640, cy = 270, R = 150;
    int best = -1, bestD = 24 * 24;
    for (int i = 0; i < nPlane; i++) {
      if (!planes[i].hasPos || rangeNm <= 0) continue;
      float frac = planes[i].dist / (float)rangeNm;
      if (frac > 1) frac = 1;
      float brg = bearing(placeLat, placeLon, planes[i].lat, planes[i].lon);
      int px = cx + (int)(sinf(brg) * frac * (R - 14));
      int py = cy - (int)(cosf(brg) * frac * (R - 14));
      int dx = x - px, dy = y - py;
      int d2 = dx * dx + dy * dy;
      if (d2 < bestD) {
        bestD = d2;
        best = i;
      }
    }
    if (best >= 0) {
      planePick = best;
      if (best < nPlane && !planes[best].kind[0]) {
        askNet(NET_PLANE);
        lastPlane = millis();
      }
      needDraw = true;
    }
    return;
  }
  if (page == PG_MORE) {
    const int dest[] = {PG_PLACE, PG_WIFI, PG_QUAKE, PG_LAUNCH, PG_SETUP, PG_ABOUT};
    for (int i = 0; i < 6; i++) {
      int rx = X0 + (i % 2) * 348;
      int ry = 20 + (i / 2) * 150;
      if (inRect(x, y, rx, ry, 332, 134)) {
        page = dest[i];
        if (page == PG_WIFI) focus = 1;
        needDraw = true;
        return;
      }
    }
  }
  if (page == PG_PLACE) {
    if (inRect(x, y, X0, 8, 132, 36)) {
      page = PG_MORE;
      needDraw = true;
      return;
    }
    if (inRect(x, y, 624, 52, 160, 42) && placeQuery[0]) {
      strlcpy(placeMsg, "searching", sizeof(placeMsg));
      nHits = 0;
      askNet(NET_FIND);
      needDraw = true;
      return;
    }
    for (int i = 0; i < nHits && i < 3; i++) {
      if (!inRect(x, y, X0, 108 + i * 52, 676, 46)) continue;
      placeSet = true;
      placeLat = hits[i].lat;
      placeLon = hits[i].lon;
      strlcpy(placeName, hits[i].label, sizeof(placeName));
      placeTz[0] = 0;
      wxOk = false;
      tideId[0] = 0;
      tideHigh[0] = tideLow[0] = 0;
      tideKnown = false;
      tideDone = false;
      tideBestKm = 99999.0f;
      lastPassBuild = 0;
      savePrefs();
      page = PG_NOW;
      noteLinked();
      needDraw = true;
      return;
    }
  }
  if (page == PG_WIFI) {
    if (inRect(x, y, X0, 52, 676, 40)) focus = 1;
    else if (inRect(x, y, X0, 100, 676, 40)) focus = 2;
    else if (inRect(x, y, X0, 8, 120, 36)) {
      WiFi.mode(WIFI_STA);
      WiFi.scanNetworks(true);
      scanOn = true;
      nScan = 0;
      scanTop = 0;
    } else if (inRect(x, y, 250, 8, 120, 36)) {
      if (wifiSsid[0]) joining = true;
      else strlcpy(wifiMsg, "type a name", sizeof(wifiMsg));
    } else if (inRect(x, y, 390, 8, 160, 36)) {
      wifiSsid[0] = 0;
      wifiPass[0] = 0;
      wifiOk = false;
      WiFi.disconnect();
      savePrefs();
      strlcpy(wifiMsg, "cleared", sizeof(wifiMsg));
    } else if (inRect(x, y, 640, 188, 140, 36) && nScan > 4) {
      scanTop += 4;
      if (scanTop >= nScan) scanTop = 0;
    } else {
      int rows = nScan - scanTop;
      if (rows > 4) rows = 4;
      for (int i = 0; i < rows; i++) {
        if (!inRect(x, y, X0, 152 + i * 36, 520, 32)) continue;
        strlcpy(wifiSsid, scanName[scanTop + i], sizeof(wifiSsid));
        focus = 2;
        wifiMsg[0] = 0;
      }
    }
    needDraw = true;
    return;
  }
  if (page == PG_SKY) {
    const int dest[] = {PG_STARS, PG_TONIGHT, PG_PLANETS, PG_MOON, PG_ROCKS, PG_SATS, PG_ISS, PG_METEOR};
    for (int i = 0; i < 8; i++) {
      int rx = X0 + (i % 2) * 348;
      int ry = 48 + (i / 2) * 106;
      if (inRect(x, y, rx, ry, 336, 96)) {
        page = dest[i];
        needDraw = true;
        return;
      }
    }
  }
  if (page == PG_STARS || page == PG_TONIGHT || page == PG_PLANETS || page == PG_MOON || page == PG_ROCKS || page == PG_SATS || page == PG_METEOR) {
    if (inRect(x, y, X0, 8, 132, 36)) {
      page = PG_SKY;
      needDraw = true;
      return;
    }
  }
  if (page == PG_ISS) {
    if (inRect(x, y, 12, 8, 132, 36)) {
      page = PG_SKY;
      needDraw = true;
      return;
    }
  }
  if (page == PG_QUAKE) {
    if (inRect(x, y, X0, 8, 132, 36)) page = PG_MORE;
    else if (inRect(x, y, 500, 8, 120, 36)) quakeUsa = false;
    else if (inRect(x, y, 640, 8, 140, 36)) quakeUsa = true;
    else {
      int qi = quakeAt(x, y);
      if (qi < 0) return;
      quakePin = qi;
      quakeBack = PG_QUAKE;
      page = PG_QFACT;
    }
    needDraw = true;
    return;
  }
  if (page == PG_QFACT) {
    if (inRect(x, y, X0, 8, 132, 36)) {
      page = (quakeBack == PG_QUAKE) ? PG_QUAKE : PG_NOW;
      needDraw = true;
    }
    return;
  }
  if (page == PG_SETUP || page == PG_ABOUT || page == PG_LAUNCH) {
    if (inRect(x, y, X0, 8, 132, 36)) {
      page = PG_MORE;
      needDraw = true;
      return;
    }
  }
  if (page == PG_SETUP) {
    bool wasTide = showTide;
    if (inRect(x, y, 250, 46, 140, 36)) dayMode = false;
    else if (inRect(x, y, 404, 46, 140, 36)) dayMode = true;
    else if (inRect(x, y, 250, 90, 120, 36)) blLevel = 0;
    else if (inRect(x, y, 382, 90, 120, 36)) blLevel = 1;
    else if (inRect(x, y, 514, 90, 150, 36)) blLevel = 2;
    else if (inRect(x, y, 250, 134, 120, 36)) useC = false;
    else if (inRect(x, y, 382, 134, 120, 36)) useC = true;
    else if (inRect(x, y, 250, 178, 120, 36)) pinAlerts = true;
    else if (inRect(x, y, 382, 178, 120, 36)) {
      pinAlerts = false;
      pinOn = false;
    } else if (inRect(x, y, 250, 222, 120, 36)) showTide = true;
    else if (inRect(x, y, 382, 222, 120, 36)) showTide = false;
    else if (inRect(x, y, X0, 270, 200, 44)) page = PG_ABOUT;
    else if (inRect(x, y, 328, 270, 200, 44)) {
      page = PG_WIFI;
      focus = 1;
    } else if (inRect(x, y, 544, 270, 240, 44)) page = PG_PLACE;
    else if (inRect(x, y, X0, 326, 320, 44)) {
      factoryReset();
      return;
    }
    if (showTide && !wasTide) askNet(NET_TIDE);
    cols();
    applyBl();
    savePrefs();
    needDraw = true;
    return;
  }
  if (page == PG_ABOUT) {
    if (inRect(x, y, X0, 300, 280, 52) && !otaBusy) {
      otaLatest[0] = 0;
      otaNewer = false;
      strlcpy(otaMsg, "checking", sizeof(otaMsg));
      askNet(NET_OTA);
    } else if (otaNewer && inRect(x, y, 420, 300, 280, 52) && !otaBusy) {
      askNet(NET_OTA);
    }
    needDraw = true;
  }
}

void queueRefresh() {
  if (!wifiOk || !placeSet || quietPage() || wifiBusy) return;
  uint32_t now = millis();
  if (now - lastPack > 180000) {
    lastPack = now;
    askNet(NET_PACK);
    askNet(NET_AQI);
    askNet(NET_ALERT);
  }
  uint32_t planeWait = page == PG_AIR ? 20000 : 120000;
  if (now - lastPlane > planeWait) {
    lastPlane = now;
    askNet(NET_PLANE);
  }
  if (now - lastQuake > 180000) {
    lastQuake = now;
    askNet(NET_QUAKE);
  }
  uint32_t issWait = (page == PG_SKY || page == PG_ISS || page == PG_SATS) ? 30000 : 180000;
  if (now - lastIss > issWait) {
    lastIss = now;
    askNet(NET_ISS);
  }
  if (now - lastLaunch > 3600000) {
    lastLaunch = now;
    askNet(NET_LAUNCH);
  }
  if (now - lastKp > 1800000) {
    lastKp = now;
    askNet(NET_KP);
  }
  if (showTide && now - lastTide > 3600000) {
    lastTide = now;
    askNet(NET_TIDE);
  }
}

void pollScan() {
  if (!scanOn) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  if (n < 0) {
    scanOn = false;
    return;
  }
  nScan = n > 8 ? 8 : n;
  for (int i = 0; i < nScan; i++) strlcpy(scanName[i], WiFi.SSID(i).c_str(), 33);
  WiFi.scanDelete();
  scanOn = false;
  scanTop = 0;
  needDraw = true;
}

void connectWifi() {
  loadPrefs();
  applyBl();
  if (!wifiSsid[0]) {
    page = PG_WIFI;
    focus = 1;
    drawCurrent();
    return;
  }
  drawLinking();
  wifiBusy = true;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid, wifiPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(50);
  wifiBusy = false;
  wifiOk = WiFi.status() == WL_CONNECTED;
  if (!wifiOk) {
    page = placeSet ? PG_NOW : PG_WIFI;
    strlcpy(wifiMsg, "no join", sizeof(wifiMsg));
    drawCurrent();
    return;
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  applyPlaceTz();
  page = placeSet ? PG_NOW : PG_PLACE;
  noteLinked();
  drawCurrent();
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("meridian 1.1");
  i2cLock = xSemaphoreCreateMutex();
  dataMux = xSemaphoreCreateMutex();
  jobMux = xSemaphoreCreateMutex();
  boardIoBegin();
  rgbBus = new Arduino_ESP32RGBPanel(
      5, 3, 46, 7,
      1, 2, 42, 41, 40,
      39, 0, 45, 48, 47, 21,
      14, 38, 18, 17, 10,
      0, 8, 4, 8,
      0, 8, 4, 8,
      1, 12500000, false, 0, 0, 800 * 10);
  gfx = new Arduino_RGB_Display(PW, PH, rgbBus, 0, true);
  if (!gfx->begin()) {
    Serial.println("gfx begin failed");
    gfx = nullptr;
    return;
  }
  gfx->setTextWrap(false);
  cols();
  loadCraft(0, SATS[0].l1, SATS[0].l2);
  loadCraft(1, SATS[1].l1, SATS[1].l2);
  loadCraft(2, SATS[2].l1, SATS[2].l2);
  packBuf = (char *)heap_caps_malloc(20000, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  xTaskCreatePinnedToCore(netTask, "merid", 40960, nullptr, 1, nullptr, 0);
  connectWifi();
}

void loop() {
  if (!gfx) return;
  pollScan();
  if (joining) {
    joining = false;
    wifiMsg[0] = 0;
    drawLinking();
    wifiBusy = true;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect();
    delay(100);
    WiFi.begin(wifiSsid, wifiPass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(50);
    wifiBusy = false;
    wifiOk = WiFi.status() == WL_CONNECTED;
    savePrefs();
    if (wifiOk) {
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      applyPlaceTz();
      noteLinked();
      page = placeSet ? PG_NOW : PG_PLACE;
    } else {
      strlcpy(wifiMsg, "no join", sizeof(wifiMsg));
      page = PG_WIFI;
    }
    needDraw = true;
  }
  static bool held = false;
  static int hx = 0, hy = 0;
  static uint32_t lockUntil = 0;
  int xs[1], ys[1];
  int nTouch = 0;
  if (millis() >= lockUntil) nTouch = gt911Touches(xs, ys, 1);
  if (nTouch >= 1 && !held) {
    held = true;
    hx = xs[0];
    hy = ys[0];
  }
  if (nTouch == 0 && held) {
    held = false;
    lockUntil = millis() + 250;
    onTap(hx, hy);
  }
  static uint32_t lastWifi = 0;
  if (millis() - lastWifi > 2000) {
    lastWifi = millis();
    bool ok = WiFi.status() == WL_CONNECTED;
    if (ok != wifiOk) {
      wifiOk = ok;
      needDraw = true;
    }
  }
  time_t now;
  bool on = liveClock(&now);
  if (on) {
    struct tm t;
    localtime_r(&now, &t);
    if (!clockWas || t.tm_min != shownMin) {
      int prev = shownMin;
      shownMin = t.tm_min;
      clockWas = true;
      if (page == PG_NOW || page == PG_TONIGHT || page == PG_PLANETS || page == PG_MOON || page == PG_ROCKS) needDraw = true;
      if (page == PG_STARS && (prev < 0 || (t.tm_min / 3) != (prev / 3))) needDraw = true;
    }
  } else if (clockWas) {
    clockWas = false;
    shownMin = -99;
    needDraw = true;
  }
  if (otaBusy && page == PG_ABOUT && millis() - lastOtaDraw > 400) {
    lastOtaDraw = millis();
    needDraw = true;
  }
  queueRefresh();
  if (netPaint && !netBusy && nJob == 0) {
    netPaint = false;
    needDraw = true;
  }
  if (needDraw) {
    needDraw = false;
    drawCurrent();
  }
  delay(16);
}
