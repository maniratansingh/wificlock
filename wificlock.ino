#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>
#include <SPI.h>
#include <MD_MAX72xx.h>
#include <ctype.h>

// ============================================================
// USER SETTINGS
// ============================================================
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   4

#define DATA_PIN 13   // GPIO13 / D7 -> DIN
#define CLK_PIN  14   // GPIO14 / D5 -> CLK
#define CS_PIN   4    // GPIO4  / D2 -> CS / LOAD

#define DEFAULT_BRIGHTNESS_DAY   3
#define DEFAULT_BRIGHTNESS_NIGHT 1

constexpr long TZ_OFFSET_SECONDS = 19800; // IST UTC+5:30

constexpr bool FLIP_X = false;
constexpr bool FLIP_Y = true;

// Weather
const char* WEATHER_URL =
  "https://api.open-meteo.com/v1/forecast?latitude=26.793695&longitude=82.732182&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,weather_code&timezone=Asia%2FKolkata";

// NTP
const char* NTP_SERVER_1 = "in.pool.ntp.org";
const char* NTP_SERVER_2 = "pool.ntp.org";
const char* NTP_SERVER_3 = "time.nist.gov";

// Timing
constexpr time_t   MIN_VALID_EPOCH          = 1700000000;
constexpr uint32_t DISPLAY_REFRESH_MS       = 40UL;
constexpr uint32_t CLOCK_REDRAW_MS          = 200UL;
constexpr uint32_t CLOCK_SECONDS_ANIM_MS    = 200UL;
constexpr uint32_t WIFI_RETRY_MS            = 15000UL;
constexpr uint32_t NTP_RETRY_MS             = 15000UL;
constexpr uint32_t NTP_REFRESH_MS           = 3600000UL;
constexpr uint32_t SERIAL_IP_PRINT_MS       = 30000UL;
constexpr uint32_t HTTPS_TIMEOUT_MS         = 2500UL;

// State schedule
constexpr uint32_t BIRTHDAY_REPEAT_MS       = 60000UL;
constexpr uint32_t HOLIDAY_REPEAT_MS        = 30000UL;
constexpr uint32_t WEATHER_REPEAT_MS        = 2UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_RETRY_MS         = 5UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_REFRESH_MS       = 30UL * 60UL * 1000UL;
constexpr uint32_t MANUAL_MESSAGE_MS        = 20000UL;
constexpr uint32_t BIRTHDAY_SHOW_MS         = 12000UL;
constexpr uint32_t HOLIDAY_SHOW_MS          = 10000UL;
constexpr uint32_t WEATHER_SHOW_MS          = 9000UL;
constexpr uint32_t STATUS_SHOW_MS           = 8000UL;

// Queue
constexpr uint8_t MANUAL_QUEUE_SIZE = 10;

// ============================================================
// DISPLAY + WEB
// ============================================================
MD_MAX72XX mx(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
ESP8266WebServer server(80);

// ============================================================
// DATA MODELS
// ============================================================
struct ClockNow {
  bool valid;
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int wday;
};

struct BirthdayItem {
  const char* name;
  uint8_t day;
  uint8_t month;
};

struct HolidayItem {
  const char* title;
  uint8_t day;
  uint8_t month;
};

enum DisplayState : uint8_t {
  STATE_CLOCK = 0,
  STATE_MANUAL,
  STATE_BIRTHDAY,
  STATE_HOLIDAY,
  STATE_WEATHER,
  STATE_STATUS
};

struct WeatherData {
  bool valid;
  float tempC;
  float feelsLikeC;
  int humidityPct;
  float windKmh;
  int weatherCode;
  String summary;
  String advisory;
  unsigned long lastFetchMs;
  unsigned long lastAttemptMs;
};

struct ManualQueueItem {
  String text;
  uint32_t durationMs;
};

// ============================================================
// USER DATA
// ============================================================
const BirthdayItem birthdays[] = {
  {"PAPA",          24, 4},
  {"MUMMY",         14, 5},
  {"KHUSHBOO DIDI", 25, 5},
  {"SWATI",         14, 6},
  {"MANSHU",        12, 11}
};

const HolidayItem holidays[] = {
  {"HAZRAT ALI JAYANTI",            3, 1},
  {"2ND SATURDAY BANK CLOSED",     10, 1},
  {"4TH SATURDAY BANK CLOSED",     24, 1},
  {"REPUBLIC DAY",                 26, 1},
  {"2ND SATURDAY BANK CLOSED",     14, 2},
  {"MAHA SHIVRATRI",               15, 2},
  {"4TH SATURDAY BANK CLOSED",     28, 2},
  {"HOLI",                          3, 3},
  {"2ND SATURDAY BANK CLOSED",     14, 3},
  {"EID UL FITR",                  21, 3},
  {"RAM NAVAMI",                   27, 3},
  {"4TH SATURDAY BANK CLOSED",     28, 3},
  {"MAHAVIR JAYANTI",              31, 3},
  {"ANNUAL BANK CLOSING",           1, 4},
  {"GOOD FRIDAY",                   3, 4},
  {"2ND SATURDAY BANK CLOSED",     11, 4},
  {"DR AMBEDKAR JAYANTI",          14, 4},
  {"4TH SATURDAY BANK CLOSED",     25, 4},
  {"BUDDHA PURNIMA",                1, 5},
  {"2ND SATURDAY BANK CLOSED",      9, 5},
  {"4TH SATURDAY BANK CLOSED",     23, 5},
  {"EID UL ADHA BAKRID",           27, 5},
  {"2ND SATURDAY BANK CLOSED",     13, 6},
  {"MUHARRAM",                     26, 6},
  {"4TH SATURDAY BANK CLOSED",     27, 6},
  {"2ND SATURDAY BANK CLOSED",     11, 7},
  {"4TH SATURDAY BANK CLOSED",     25, 7},
  {"2ND SATURDAY BANK CLOSED",      8, 8},
  {"INDEPENDENCE DAY",             15, 8},
  {"4TH SATURDAY BANK CLOSED",     22, 8},
  {"EID E MILAD",                  25, 8},
  {"RAKSHA BANDHAN",               28, 8},
  {"JANMASHTAMI",                   4, 9},
  {"2ND SATURDAY BANK CLOSED",     12, 9},
  {"4TH SATURDAY BANK CLOSED",     26, 9},
  {"GANDHI JAYANTI",                2, 10},
  {"2ND SATURDAY BANK CLOSED",     10, 10},
  {"MAHA NAVAMI",                  20, 10},
  {"DUSSEHRA",                     21, 10},
  {"4TH SATURDAY BANK CLOSED",     24, 10},
  {"DIWALI",                        8, 11},
  {"GOVARDHAN PUJA",                9, 11},
  {"BHAI DOOJ",                    11, 11},
  {"2ND SATURDAY BANK CLOSED",     14, 11},
  {"GURU NANAK JAYANTI",           24, 11},
  {"4TH SATURDAY BANK CLOSED",     28, 11},
  {"2ND SATURDAY BANK CLOSED",     12, 12},
  {"HAZRAT ALI JAYANTI",           23, 12},
  {"CHRISTMAS",                    25, 12},
  {"4TH SATURDAY BANK CLOSED",     26, 12}
};

const size_t BIRTHDAY_COUNT = sizeof(birthdays) / sizeof(birthdays[0]);
const size_t HOLIDAY_COUNT  = sizeof(holidays) / sizeof(holidays[0]);

// ============================================================
// RUNTIME STATE
// ============================================================
bool ntpConfigured = false;
unsigned long lastWifiAttempt = 0;
unsigned long lastNtpAttempt = 0;
unsigned long lastNtpKick = 0;
unsigned long lastIpSerialPrint = 0;
unsigned long lastClockRedraw = 0;
unsigned long lastDisplayFrame = 0;

unsigned long lastBirthdayTrigger = 0;
unsigned long lastHolidayTrigger = 0;
unsigned long lastWeatherTrigger = 0;

DisplayState activeState = STATE_CLOCK;
unsigned long activeStateUntil = 0;
String activeMessage;

// Manual queue
ManualQueueItem manualQueue[MANUAL_QUEUE_SIZE];
uint8_t manualQueueHead = 0;
uint8_t manualQueueTail = 0;
uint8_t manualQueueCount = 0;
String currentManualText = "";

// Weather
WeatherData weather = { false, 0.0f, 0.0f, 0, 0.0f, -1, "", "", 0, 0 };

// Scroll engine
String scrollText = "";
int16_t scrollPixelOffset = 0;
bool scrollActive = false;

// Runtime orientation controls from the web UI
bool clockFlipX = false;
bool clockFlipY = true;
bool textFlipX = false;
bool textFlipY = false;

int lastClockSecond = -1;
bool secondsAnimActive = false;
unsigned long secondsAnimStartMs = 0;
uint8_t secondsAnimFromTens = 0;
uint8_t secondsAnimFromOnes = 0;
uint8_t secondsAnimToTens = 0;
uint8_t secondsAnimToOnes = 0;

constexpr int CLOCK_X0 = 2;
constexpr int CLOCK_Y0 = 1;
constexpr int CLOCK_WIDTH = 27;
constexpr int CLOCK_HEIGHT = 5;

// ============================================================
// SMALL 3x5 DIGIT FONT FOR CLOCK
// ============================================================
const uint8_t DIGITS[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111},
  {0b010, 0b110, 0b010, 0b010, 0b111},
  {0b111, 0b001, 0b111, 0b100, 0b111},
  {0b111, 0b001, 0b111, 0b001, 0b111},
  {0b101, 0b101, 0b111, 0b001, 0b001},
  {0b111, 0b100, 0b111, 0b001, 0b111},
  {0b111, 0b100, 0b111, 0b101, 0b111},
  {0b111, 0b001, 0b001, 0b001, 0b001},
  {0b111, 0b101, 0b111, 0b101, 0b111},
  {0b111, 0b101, 0b111, 0b001, 0b111}
};

// ============================================================
// HTML PAGE
// ============================================================
const char PAGE_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP8266 Smart Clock</title>
<style>
  body{font-family:Arial,Helvetica,sans-serif;background:#0f1115;color:#e8eaed;margin:0;padding:16px}
  .wrap{max-width:1100px;margin:auto}
  .card{background:#171a21;border:1px solid #252936;border-radius:14px;padding:16px;margin-bottom:14px}
  input,button,textarea,select{
    width:100%;padding:12px;border-radius:10px;border:1px solid #333;background:#11151d;color:#fff;box-sizing:border-box
  }
  button{cursor:pointer;background:#2563eb}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px}
  .muted{color:#aeb4c0;font-size:14px}
  .value{font-size:20px;font-weight:700;margin-top:8px;word-break:break-word}
  table{width:100%;border-collapse:collapse}
  th,td{border-bottom:1px solid #2b3040;padding:8px;text-align:left;vertical-align:top}
  .pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#232938;color:#d7def0;font-size:12px}
  .mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
  .toggle{display:flex;align-items:center;gap:10px;padding:10px;border:1px solid #333;background:#11151d;border-radius:10px}
  .toggle input{width:auto;margin:0}
  .btn-inline{width:auto;padding:8px 12px;font-size:14px}
</style>
</head>
<body>
<div class="wrap">
  <div class="card">
    <h1>ESP8266 Smart Clock</h1>
    <div class="muted">Clock + birthdays + holidays + morning weather + browser text queue</div>
  </div>

  <div class="card">
    <h2>Send text to queue</h2>
    <form onsubmit="sendMsg(event)">
      <textarea id="msg" rows="3" placeholder="Type message for LED display"></textarea><br><br>
      <select id="duration">
        <option value="20">Show for 20 sec</option>
        <option value="30">Show for 30 sec</option>
        <option value="60">Show for 60 sec</option>
      </select><br><br>
      <button type="submit">Add to Queue</button>
    </form>
    <div id="sendStatus" class="muted" style="margin-top:10px;"></div>
  </div>

  <div class="card">
    <h2>Status</h2>
    <div class="grid">
      <div><div class="muted">Time</div><div class="value" id="timeText">-</div></div>
      <div><div class="muted">Date</div><div class="value" id="dateText">-</div></div>
      <div><div class="muted">IP</div><div class="value" id="ipText">-</div></div>
      <div><div class="muted">WiFi</div><div class="value" id="wifiText">-</div></div>
      <div><div class="muted">RSSI</div><div class="value" id="rssiText">-</div></div>
      <div><div class="muted">Heap</div><div class="value" id="heapText">-</div></div>
      <div><div class="muted">State</div><div class="value" id="stateText">-</div></div>
      <div><div class="muted">Weather</div><div class="value" id="weatherText">-</div></div>
    </div>
    <br>
    <button type="button" onclick="showWeatherNow()">Show Weather Now</button>
    <div id="weatherNowStatus" class="muted" style="margin-top:10px;"></div>
  </div>

  <div class="card">
    <h2>Orientation</h2>
    <div class="grid">
      <label class="toggle"><input type="checkbox" id="clockFlipX" onchange="saveOrientation()"> Clock flip X</label>
      <label class="toggle"><input type="checkbox" id="clockFlipY" onchange="saveOrientation()"> Clock flip Y</label>
      <label class="toggle"><input type="checkbox" id="textFlipX" onchange="saveOrientation()"> Text flip X</label>
      <label class="toggle"><input type="checkbox" id="textFlipY" onchange="saveOrientation()"> Text flip Y</label>
    </div>
    <div id="orientationState" class="muted" style="margin-top:12px;">-</div>
  </div>

  <div class="card">
    <h2>Manual Queue</h2>
    <div class="grid">
      <div>
        <div class="muted">Currently running text</div>
        <div class="value mono" id="currentText">-</div>
      </div>
      <div>
        <div class="muted">Queue count</div>
        <div class="value" id="queueCount">0</div>
      </div>
    </div>
    <br>
    <table>
      <thead>
        <tr><th>#</th><th>Text</th><th>Duration</th></tr>
      </thead>
      <tbody id="queueBody"></tbody>
    </table>
  </div>

  <div class="card">
    <h2>Birthdays</h2>
    <table>
      <thead><tr><th>Name</th><th>Date</th><th>Test</th></tr></thead>
      <tbody id="birthdaysBody"></tbody>
    </table>
    <div id="birthdayTestStatus" class="muted" style="margin-top:10px;"></div>
  </div>

  <div class="card">
    <h2>2026 UP Bank Holidays</h2>
    <table>
      <thead><tr><th>Holiday</th><th>Date</th><th>Test</th></tr></thead>
      <tbody id="holidaysBody"></tbody>
    </table>
    <div id="holidayTestStatus" class="muted" style="margin-top:10px;"></div>
  </div>
</div>

<script>
async function refreshAll(){
  try{
    const r = await fetch('/api/all?_=' + Date.now(), {cache:'no-store'});
    const j = await r.json();

    document.getElementById('timeText').textContent = j.status.time || '-';
    document.getElementById('dateText').textContent = j.status.date || '-';
    document.getElementById('ipText').textContent = j.status.ip || '-';
    document.getElementById('wifiText').textContent = j.status.wifi || '-';
    document.getElementById('rssiText').textContent = j.status.rssi || '-';
    document.getElementById('heapText').textContent = j.status.heap || '-';
    document.getElementById('stateText').textContent = j.status.state || '-';
    document.getElementById('weatherText').textContent = j.status.weather || '-';

    if (j.orientation) {
      document.getElementById('clockFlipX').checked = !!j.orientation.clock_flip_x;
      document.getElementById('clockFlipY').checked = !!j.orientation.clock_flip_y;
      document.getElementById('textFlipX').checked = !!j.orientation.text_flip_x;
      document.getElementById('textFlipY').checked = !!j.orientation.text_flip_y;

      document.getElementById('orientationState').textContent =
        'Panel X:' + (j.orientation.panel_flip_x ? 'ON' : 'OFF') +
        ' Y:' + (j.orientation.panel_flip_y ? 'ON' : 'OFF') +
        ' | Clock X:' + (j.orientation.clock_flip_x ? 'ON' : 'OFF') +
        ' Y:' + (j.orientation.clock_flip_y ? 'ON' : 'OFF') +
        ' | Text X:' + (j.orientation.text_flip_x ? 'ON' : 'OFF') +
        ' Y:' + (j.orientation.text_flip_y ? 'ON' : 'OFF');
    }

    document.getElementById('currentText').textContent = j.manual.current || '-';
    document.getElementById('queueCount').textContent = j.manual.queue_count || 0;
    document.getElementById('queueBody').innerHTML =
      (j.manual.queue || []).map(x => `<tr><td>${x.index}</td><td>${x.text}</td><td>${x.duration}</td></tr>`).join('')
      || '<tr><td colspan="3" class="muted">Queue empty</td></tr>';

    document.getElementById('birthdaysBody').innerHTML =
      (j.birthdays || []).map(x => `<tr><td>${x.name}</td><td>${x.date}</td><td><button type="button" class="btn-inline" onclick="testBirthday(${x.index})">Test</button></td></tr>`).join('')
      || '<tr><td colspan="3" class="muted">No birthdays</td></tr>';

    document.getElementById('holidaysBody').innerHTML =
      (j.holidays || []).map(x => `<tr><td>${x.title}</td><td>${x.date}</td><td><button type="button" class="btn-inline" onclick="testHoliday(${x.index})">Test</button></td></tr>`).join('')
      || '<tr><td colspan="3" class="muted">No holidays</td></tr>';
  } catch(e) {}
}

async function sendMsg(ev){
  ev.preventDefault();
  const msg = document.getElementById('msg').value.trim();
  const duration = document.getElementById('duration').value;
  if(!msg){
    document.getElementById('sendStatus').textContent = 'Enter message first';
    return;
  }
  try{
    const body = 'text=' + encodeURIComponent(msg) + '&duration=' + encodeURIComponent(duration);
    const r = await fetch('/api/send', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body
    });
    const t = await r.text();
    document.getElementById('sendStatus').textContent = t;
    document.getElementById('msg').value = '';
    refreshAll();
  }catch(e){
    document.getElementById('sendStatus').textContent = 'Failed to send';
  }
}

async function saveOrientation(){
  const body = [
    'clockFlipX=' + (document.getElementById('clockFlipX').checked ? '1' : '0'),
    'clockFlipY=' + (document.getElementById('clockFlipY').checked ? '1' : '0'),
    'textFlipX=' + (document.getElementById('textFlipX').checked ? '1' : '0'),
    'textFlipY=' + (document.getElementById('textFlipY').checked ? '1' : '0')
  ].join('&');

  try{
    await fetch('/api/orientation', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body
    });
    refreshAll();
  }catch(e){}
}

async function showWeatherNow(){
  const status = document.getElementById('weatherNowStatus');
  status.textContent = 'Requesting weather...';
  try{
    const r = await fetch('/api/weather-now', {method:'POST'});
    const t = await r.text();
    status.textContent = t;
    refreshAll();
  }catch(e){
    status.textContent = 'Failed to trigger weather';
  }
}

async function testBirthday(index){
  const status = document.getElementById('birthdayTestStatus');
  status.textContent = 'Triggering birthday test...';
  try{
    const body = 'index=' + encodeURIComponent(index);
    const r = await fetch('/api/test-birthday', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body
    });
    const t = await r.text();
    status.textContent = t;
    refreshAll();
  }catch(e){
    status.textContent = 'Failed to trigger birthday test';
  }
}

async function testHoliday(index){
  const status = document.getElementById('holidayTestStatus');
  status.textContent = 'Triggering holiday test...';
  try{
    const body = 'index=' + encodeURIComponent(index);
    const r = await fetch('/api/test-holiday', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body
    });
    const t = await r.text();
    status.textContent = t;
    refreshAll();
  }catch(e){
    status.textContent = 'Failed to trigger holiday test';
  }
}

refreshAll();
setInterval(refreshAll, 3000);
</script>
</body>
</html>
)HTML";

// ============================================================
// HELPERS
// ============================================================
bool timeReady() {
  return time(nullptr) > MIN_VALID_EPOCH;
}

String twoDigits(int v) {
  if (v < 10) return "0" + String(v);
  return String(v);
}

const char* weekdayName(int wday) {
  static const char* names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
  };
  if (wday < 0 || wday > 6) return "-";
  return names[wday];
}

const char* monthName(int m) {
  static const char* names[] = {
    "", "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
  };
  if (m < 1 || m > 12) return "-";
  return names[m];
}

String formatDayMonth(uint8_t day, uint8_t month) {
  return twoDigits(day) + "-" + twoDigits(month);
}

String formatUptime() {
  uint32_t sec = millis() / 1000UL;
  uint32_t days = sec / 86400UL;
  sec %= 86400UL;
  uint8_t hours = sec / 3600UL;
  sec %= 3600UL;
  uint8_t mins = sec / 60UL;
  uint8_t secs = sec % 60UL;

  String s;
  if (days > 0) s += String(days) + "d ";
  s += twoDigits(hours) + ":" + twoDigits(mins) + ":" + twoDigits(secs);
  return s;
}

ClockNow getISTNow() {
  ClockNow now {};
  now.valid = false;

  if (!timeReady()) return now;

  time_t utc = time(nullptr);
  time_t ist = utc + TZ_OFFSET_SECONDS;
  struct tm* t = gmtime(&ist);
  if (!t) return now;

  now.valid  = true;
  now.year   = t->tm_year + 1900;
  now.month  = t->tm_mon + 1;
  now.day    = t->tm_mday;
  now.hour   = t->tm_hour;
  now.minute = t->tm_min;
  now.second = t->tm_sec;
  now.wday   = t->tm_wday;
  return now;
}

String formatTimeString(const ClockNow& now) {
  if (!now.valid) return "-";
  return twoDigits(now.hour) + ":" + twoDigits(now.minute) + ":" + twoDigits(now.second);
}

String formatDateString(const ClockNow& now) {
  if (!now.valid) return "-";
  return String(weekdayName(now.wday)) + ", " + String(now.day) + " " +
         String(monthName(now.month)) + " " + String(now.year);
}

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 32) out += ' ';
        else out += c;
        break;
    }
  }
  return out;
}

// Strong ASCII cleanup so weird upside-down / unicode text cannot break display
String sanitizeText(String s) {
  String out;
  out.reserve(s.length());

  for (size_t i = 0; i < s.length(); i++) {
    uint8_t b = (uint8_t)s[i];

    // keep only plain ASCII
    if (b < 32 || b > 126) {
      out += ' ';
      continue;
    }

    char c = (char)b;
    c = toupper((unsigned char)c);

    bool ok =
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == ' ' || c == '-' || c == '.' || c == ':' || c == '/' ||
      c == '(' || c == ')';

    out += ok ? c : ' ';
  }

  while (out.indexOf("  ") >= 0) out.replace("  ", " ");
  out.trim();

  if (out.length() > 180) out = out.substring(0, 180);
  if (!out.length()) out = "EMPTY";
  return out;
}

String wifiStateText() {
  wl_status_t s = WiFi.status();
  switch (s) {
    case WL_CONNECTED:       return "Connected";
    case WL_NO_SSID_AVAIL:   return "SSID not available";
    case WL_CONNECT_FAILED:  return "Connect failed";
    case WL_CONNECTION_LOST: return "Connection lost";
    case WL_DISCONNECTED:    return "Disconnected";
    case WL_IDLE_STATUS:     return "Idle";
    default:                 return "Unknown";
  }
}

String rssiText() {
  if (WiFi.status() != WL_CONNECTED) return "-";
  return String(WiFi.RSSI()) + " dBm";
}

const char* boolText(bool value) {
  return value ? "true" : "false";
}

const char* stateName(DisplayState s) {
  switch (s) {
    case STATE_CLOCK:    return "CLOCK";
    case STATE_MANUAL:   return "MANUAL";
    case STATE_BIRTHDAY: return "BIRTHDAY";
    case STATE_HOLIDAY:  return "HOLIDAY";
    case STATE_WEATHER:  return "WEATHER";
    case STATE_STATUS:   return "STATUS";
    default:             return "UNKNOWN";
  }
}

bool isMorningWeatherWindow(const ClockNow& now) {
  if (!now.valid) return false;
  return (now.hour >= 6 && now.hour < 12);
}

bool isNight(const ClockNow& now) {
  if (!now.valid) return false;
  return (now.hour >= 20 || now.hour < 6);
}

void applyBrightnessByTime(const ClockNow& now) {
  uint8_t level = isNight(now) ? DEFAULT_BRIGHTNESS_NIGHT : DEFAULT_BRIGHTNESS_DAY;
  mx.control(MD_MAX72XX::INTENSITY, level);
}

// ============================================================
// MANUAL QUEUE HELPERS
// ============================================================
bool manualQueueIsFull() {
  return manualQueueCount >= MANUAL_QUEUE_SIZE;
}

bool manualQueueIsEmpty() {
  return manualQueueCount == 0;
}

bool enqueueManualMessage(const String& text, uint32_t durationMs) {
  if (manualQueueIsFull()) return false;

  manualQueue[manualQueueTail].text = text;
  manualQueue[manualQueueTail].durationMs = durationMs;
  manualQueueTail = (manualQueueTail + 1) % MANUAL_QUEUE_SIZE;
  manualQueueCount++;
  return true;
}

bool dequeueManualMessage(String& text, uint32_t& durationMs) {
  if (manualQueueIsEmpty()) return false;

  text = manualQueue[manualQueueHead].text;
  durationMs = manualQueue[manualQueueHead].durationMs;
  manualQueueHead = (manualQueueHead + 1) % MANUAL_QUEUE_SIZE;
  manualQueueCount--;
  return true;
}

// ============================================================
// DATE MATCH HELPERS
// ============================================================
int findTodayBirthdayIndex(const ClockNow& now) {
  if (!now.valid) return -1;
  for (size_t i = 0; i < BIRTHDAY_COUNT; i++) {
    if (birthdays[i].day == now.day && birthdays[i].month == now.month) return (int)i;
  }
  return -1;
}

int findTodayHolidayIndex(const ClockNow& now) {
  if (!now.valid) return -1;
  for (size_t i = 0; i < HOLIDAY_COUNT; i++) {
    if (holidays[i].day == now.day && holidays[i].month == now.month) return (int)i;
  }
  return -1;
}

// ============================================================
// WEATHER HELPERS
// ============================================================
String weatherCodeToText(int code) {
  switch (code) {
    case 0:  return "CLEAR";
    case 1:  return "MAINLY CLEAR";
    case 2:  return "PARTLY CLOUDY";
    case 3:  return "CLOUDY";
    case 45:
    case 48: return "FOG";
    case 51:
    case 53:
    case 55: return "DRIZZLE";
    case 61:
    case 63:
    case 65: return "RAIN";
    case 71:
    case 73:
    case 75: return "SNOW";
    case 80:
    case 81:
    case 82: return "RAIN SHOWERS";
    case 95: return "THUNDERSTORM";
    default: return "WEATHER";
  }
}

String weatherCodeToShortText(int code) {
  switch (code) {
    case 0:  return "CLEAR";
    case 1:  return "FAIR";
    case 2:  return "PARTLY";
    case 3:  return "CLOUDY";
    case 45:
    case 48: return "FOG";
    case 51:
    case 53:
    case 55: return "DRIZZLE";
    case 61:
    case 63:
    case 65: return "RAIN";
    case 71:
    case 73:
    case 75: return "SNOW";
    case 80:
    case 81:
    case 82: return "SHOWERS";
    case 95: return "STORM";
    default: return "WEATHER";
  }
}

bool extractJsonFloat(const String& src, const String& key, float& outVal, int startPos = 0, int endPos = -1) {
  if (endPos < 0 || endPos > (int)src.length()) endPos = src.length();

  int p = src.indexOf(key, startPos);
  if (p < 0 || p >= endPos) return false;
  p = src.indexOf(':', p);
  if (p < 0 || p >= endPos) return false;
  p++;

  while (p < endPos && (src[p] == ' ' || src[p] == '"')) p++;

  String num = "";
  while (p < endPos) {
    char c = src[p];
    if ((c >= '0' && c <= '9') || c == '-' || c == '.') num += c;
    else break;
    p++;
  }

  if (!num.length()) return false;
  outVal = num.toFloat();
  return true;
}

bool extractJsonInt(const String& src, const String& key, int& outVal, int startPos = 0, int endPos = -1) {
  float f = 0;
  if (!extractJsonFloat(src, key, f, startPos, endPos)) return false;
  outVal = (int)f;
  return true;
}

const char* advisoryShortText(const String& advisory) {
  if (advisory == "HOT DRINK WATER STAY HYDRATED") return "HOT";
  if (advisory == "COLD STAY WARM") return "COLD";
  if (advisory == "HUMID WEATHER") return "HUMID";
  if (advisory == "WINDY OUTSIDE") return "WINDY";
  return advisory.c_str();
}

bool fetchWeather() {
  weather.lastAttemptMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  client->setTimeout(HTTPS_TIMEOUT_MS);

  HTTPClient https;
  if (!https.begin(*client, WEATHER_URL)) {
    return false;
  }

  https.setTimeout(HTTPS_TIMEOUT_MS);
  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    https.end();
    return false;
  }

  String body = https.getString();
  https.end();

  int currentStart = body.indexOf("\"current\"");
  if (currentStart < 0) return false;
  currentStart = body.indexOf('{', currentStart);
  if (currentStart < 0) return false;
  int currentEnd = body.indexOf('}', currentStart);
  if (currentEnd < 0) return false;

  float temp = 0;
  float feelsLike = 0;
  float wind = 0;
  int humidity = 0;
  int code = -1;

  bool okTemp = extractJsonFloat(body, "\"temperature_2m\"", temp, currentStart, currentEnd);
  bool okFeelsLike = extractJsonFloat(body, "\"apparent_temperature\"", feelsLike, currentStart, currentEnd);
  bool okWind = extractJsonFloat(body, "\"wind_speed_10m\"", wind, currentStart, currentEnd);
  bool okHumidity = extractJsonInt(body, "\"relative_humidity_2m\"", humidity, currentStart, currentEnd);
  bool okCode = extractJsonInt(body, "\"weather_code\"", code, currentStart, currentEnd);

  if (!(okTemp && okFeelsLike && okWind && okHumidity && okCode)) {
    return false;
  }

  weather.valid = true;
  weather.tempC = temp;
  weather.feelsLikeC = feelsLike;
  weather.humidityPct = humidity;
  weather.windKmh = wind;
  weather.weatherCode = code;
  weather.summary = weatherCodeToText(code);
  weather.advisory = "";

  if (temp >= 35.0f) weather.advisory = "HOT DRINK WATER STAY HYDRATED";
  else if (temp <= 12.0f) weather.advisory = "COLD STAY WARM";
  else if (humidity >= 85 && temp >= 28.0f) weather.advisory = "HUMID WEATHER";
  else if (wind >= 20.0f) weather.advisory = "WINDY OUTSIDE";

  weather.lastFetchMs = weather.lastAttemptMs;
  return true;
}

String buildWeatherMessage() {
  if (!weather.valid) return "BASTI WEATHER UNAVAILABLE";
  int temp = (int)(weather.tempC + 0.5f);
  int wind = (int)(weather.windKmh + 0.5f);

  String msg = "BASTI UP ";
  msg += String(temp);
  msg += "C ";
  msg += weatherCodeToShortText(weather.weatherCode);

  if (weather.humidityPct >= 75) {
    msg += " RH ";
    msg += String(weather.humidityPct);
    msg += "%";
  } else if (wind >= 15) {
    msg += " W ";
    msg += String(wind);
    msg += "KM";
  }

  if (weather.advisory.length()) {
    msg += " ";
    msg += advisoryShortText(weather.advisory);
  }
  return sanitizeText(msg);
}

String weatherStatusText() {
  if (!weather.valid) return "UNAVAILABLE";
  String s = String((int)(weather.tempC + 0.5f)) + "C";
  s += " feels " + String((int)(weather.feelsLikeC + 0.5f)) + "C";
  s += " | " + weather.summary;
  s += " | RH " + String(weather.humidityPct) + "%";
  s += " | Wind " + String((int)(weather.windKmh + 0.5f)) + " km/h";
  if (weather.advisory.length()) s += " | " + weather.advisory;
  return s;
}

// ============================================================
// FONT / SCROLL ENGINE
// ============================================================
void drawPixelSafe(int x, int y, bool state) {
  const int WIDTH = mx.getColumnCount();
  const int HEIGHT = 8;

  if (x < 0 || x >= WIDTH) return;
  if (y < 0 || y >= HEIGHT) return;

  int tx = FLIP_X ? ((WIDTH - 1) - x) : x;
  int ty = FLIP_Y ? ((HEIGHT - 1) - y) : y;

  mx.setPoint(ty, tx, state);
}

int textPixelWidth(const String& text) {
  if (!text.length()) return 0;

  int width = 0;
  uint8_t cBuf[8];
  for (int i = 0; i < (int)text.length(); i++) {
    width += mx.getChar(text[i], sizeof(cBuf), cBuf);
    if (i < (int)text.length() - 1) width += 1;
  }
  return width;
}

uint32_t messageDisplayDurationMs(const String& text, uint32_t minDurationMs) {
  int totalWidth = textPixelWidth(text);
  if (totalWidth <= (int)mx.getColumnCount()) return minDurationMs;

  uint32_t scrollMs = (uint32_t)(totalWidth + mx.getColumnCount() + 2) * DISPLAY_REFRESH_MS;
  return (scrollMs > minDurationMs) ? scrollMs : minDurationMs;
}

uint8_t getTextColumnBits(const String& text, int textCol) {
  if (textCol < 0) return 0x00;

  int cursor = 0;
  uint8_t cBuf[8];
  for (int i = 0; i < (int)text.length(); i++) {
    uint8_t charWidth = mx.getChar(text[i], sizeof(cBuf), cBuf);
    if (textCol < cursor + charWidth) return cBuf[textCol - cursor];

    cursor += charWidth;
    if (i < (int)text.length() - 1) {
      if (textCol == cursor) return 0x00;
      cursor += 1;
    }
  }

  return 0x00;
}

void drawClockPixel(int x, int y, bool state) {
  if (x < 0 || x >= CLOCK_WIDTH) return;
  if (y < 0 || y >= CLOCK_HEIGHT) return;

  int tx = x;
  int ty = y;

  if (FLIP_X ^ clockFlipX) tx = (CLOCK_WIDTH - 1) - tx;
  if (FLIP_Y ^ clockFlipY) ty = (CLOCK_HEIGHT - 1) - ty;

  drawPixelSafe(CLOCK_X0 + tx, CLOCK_Y0 + ty, state);
}

void drawTextWindow(const String& text, int16_t offsetX) {
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  mx.clear();

  const int totalWidth = textPixelWidth(text);
  const bool effectiveTextFlipX = FLIP_X ^ textFlipX;
  const bool effectiveTextFlipY = FLIP_Y ^ textFlipY;
  for (int x = 0; x < (int)mx.getColumnCount(); x++) {
    int textCol = x - offsetX;
    if (textCol < 0 || textCol >= totalWidth) continue;

    int srcCol = effectiveTextFlipX ? (totalWidth - 1 - textCol) : textCol;
    uint8_t bits = getTextColumnBits(text, srcCol);
    for (int row = 0; row < 7; row++) {
      bool on = (bits >> (6 - row)) & 0x01;
      int y = effectiveTextFlipY ? (6 - row) : row;
      drawPixelSafe(x, y, on);
    }
  }

  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void startScrollMessage(const String& msg) {
  scrollText = sanitizeText(msg);
  if (!scrollText.length()) scrollText = "EMPTY";
  int totalWidth = textPixelWidth(scrollText);
  if (totalWidth <= (int)mx.getColumnCount()) {
    scrollPixelOffset = (mx.getColumnCount() - totalWidth) / 2;
    scrollActive = false;
  } else {
    scrollPixelOffset = mx.getColumnCount();
    scrollActive = true;
  }
}

void advanceScrollFrame() {
  if (!scrollText.length()) return;

  int totalWidth = textPixelWidth(scrollText);
  if (totalWidth <= (int)mx.getColumnCount()) {
    drawTextWindow(scrollText, (mx.getColumnCount() - totalWidth) / 2);
    return;
  }

  drawTextWindow(scrollText, scrollPixelOffset);
  if (!scrollActive) return;

  scrollPixelOffset--;

  if (scrollPixelOffset < -totalWidth) {
    scrollPixelOffset = mx.getColumnCount();
  }
}

// ============================================================
// CLOCK DRAW
// ============================================================
void drawDigit3x5(uint8_t digit, int x, int y) {
  if (digit > 9) return;
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      bool on = (DIGITS[digit][row] >> (2 - col)) & 0x01;
      drawClockPixel(x + col, y + row, on);
    }
  }
}

void drawColon(int x, int y, bool visible) {
  if (!visible) return;
  drawClockPixel(x, y + 1, true);
  drawClockPixel(x, y + 3, true);
}

void drawSyncAnimation() {
  static int pos = 0;
  static unsigned long lastAnim = 0;

  if (millis() - lastAnim > 120) {
    lastAnim = millis();
    pos++;
    if (pos >= (int)mx.getColumnCount()) pos = 0;
  }

  mx.clear();
  drawPixelSafe(pos, 3, true);
  drawPixelSafe(pos, 4, true);
}

bool secondsAnimationRunning(unsigned long nowMs) {
  return secondsAnimActive && (nowMs - secondsAnimStartMs <= CLOCK_SECONDS_ANIM_MS);
}

bool updateClockSecondsAnimation(const ClockNow& now, unsigned long nowMs) {
  if (!now.valid) {
    lastClockSecond = -1;
    secondsAnimActive = false;
    return false;
  }

  if (lastClockSecond < 0) {
    lastClockSecond = now.second;
    secondsAnimActive = false;
    return false;
  }

  if (now.second == lastClockSecond) return false;

  int expectedSecond = (lastClockSecond + 1) % 60;
  if (now.second != expectedSecond) {
    lastClockSecond = now.second;
    secondsAnimActive = false;
    return true;
  }

  secondsAnimFromTens = lastClockSecond / 10;
  secondsAnimFromOnes = lastClockSecond % 10;
  secondsAnimToTens = now.second / 10;
  secondsAnimToOnes = now.second % 10;
  secondsAnimStartMs = nowMs;
  secondsAnimActive = true;
  lastClockSecond = now.second;
  return true;
}

void drawDigitSlideUp(uint8_t fromDigit, uint8_t toDigit, int x, uint8_t step) {
  if (fromDigit == toDigit) {
    drawDigit3x5(toDigit, x, 0);
    return;
  }

  drawDigit3x5(fromDigit, x, -(int)step);
  drawDigit3x5(toDigit, x, 5 - (int)step);
}

void drawClock(const ClockNow& now, unsigned long nowMs) {
  if (!now.valid) {
    drawSyncAnimation();
    return;
  }

  int h1 = now.hour / 10;
  int h2 = now.hour % 10;
  int m1 = now.minute / 10;
  int m2 = now.minute % 10;
  int s1 = now.second / 10;
  int s2 = now.second % 10;

  bool animateSeconds = secondsAnimationRunning(nowMs);
  if (!animateSeconds) secondsAnimActive = false;
  bool colonOn = (now.second % 2 == 0);

  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  mx.clear();

  drawDigit3x5(h1,  0, 0);
  drawDigit3x5(h2,  4, 0);
  drawColon   (8,  0, colonOn);
  drawDigit3x5(m1, 10, 0);
  drawDigit3x5(m2, 14, 0);
  drawColon   (18, 0, colonOn);
  if (animateSeconds) {
    uint8_t step = (uint8_t)((nowMs - secondsAnimStartMs) / DISPLAY_REFRESH_MS);
    if (step > 5) step = 5;
    drawDigitSlideUp(secondsAnimFromTens, secondsAnimToTens, 20, step);
    drawDigitSlideUp(secondsAnimFromOnes, secondsAnimToOnes, 24, step);
  } else {
    drawDigit3x5(s1, 20, 0);
    drawDigit3x5(s2, 24, 0);
  }

  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

// ============================================================
// STATE ENGINE
// ============================================================
void activateState(DisplayState s, const String& msg, unsigned long durationMs) {
  activeState = s;
  activeMessage = sanitizeText(msg);
  activeStateUntil = millis() + durationMs;

  if (s != STATE_CLOCK) {
    startScrollMessage(activeMessage);
  }
}

void triggerBirthdayMessage(size_t idx) {
  lastBirthdayTrigger = millis();
  activateState(STATE_BIRTHDAY, "HAPPY BIRTHDAY " + String(birthdays[idx].name), BIRTHDAY_SHOW_MS);
}

void triggerHolidayMessage(size_t idx) {
  lastHolidayTrigger = millis();
  activateState(STATE_HOLIDAY, "TODAY " + String(holidays[idx].title), HOLIDAY_SHOW_MS);
}

void triggerWeatherMessage() {
  lastWeatherTrigger = millis();
  String msg = buildWeatherMessage();
  activateState(STATE_WEATHER, msg, messageDisplayDurationMs(msg, WEATHER_SHOW_MS));
}

bool stateBusy() {
  return activeState != STATE_CLOCK && millis() < activeStateUntil;
}

void finishStateIfExpired() {
  if (activeState == STATE_CLOCK) return;

  if (millis() >= activeStateUntil) {
    if (activeState == STATE_MANUAL) currentManualText = "";
    activeState = STATE_CLOCK;
    activeMessage = "";
    scrollActive = false;
  }
}

void scheduleManualIfNeeded() {
  if (stateBusy()) return;
  if (manualQueueIsEmpty()) return;

  String msg;
  uint32_t dur;
  if (!dequeueManualMessage(msg, dur)) return;

  currentManualText = msg;
  activateState(STATE_MANUAL, msg, dur);
}

void scheduleBirthdayIfNeeded(const ClockNow& now) {
  if (stateBusy()) return;

  int idx = findTodayBirthdayIndex(now);
  if (idx < 0) return;

  if (millis() - lastBirthdayTrigger < BIRTHDAY_REPEAT_MS) return;
  triggerBirthdayMessage((size_t)idx);
}

void scheduleHolidayIfNeeded(const ClockNow& now) {
  if (stateBusy()) return;
  if (findTodayBirthdayIndex(now) >= 0) return;

  int idx = findTodayHolidayIndex(now);
  if (idx < 0) return;

  if (millis() - lastHolidayTrigger < HOLIDAY_REPEAT_MS) return;
  triggerHolidayMessage((size_t)idx);
}

void scheduleWeatherIfNeeded(const ClockNow& now) {
  if (stateBusy()) return;
  if (!isMorningWeatherWindow(now)) return;
  if (findTodayBirthdayIndex(now) >= 0) return;

  if (millis() - lastWeatherTrigger < WEATHER_REPEAT_MS) return;
  if (!weather.valid) return;
  triggerWeatherMessage();
}

void scheduleStatusIfNeeded() {
  if (stateBusy()) return;

  if (WiFi.status() != WL_CONNECTED) {
    activateState(STATE_STATUS, "NO WIFI", STATUS_SHOW_MS);
    return;
  }

  if (!timeReady()) {
    activateState(STATE_STATUS, "SYNCING TIME", STATUS_SHOW_MS);
    return;
  }
}

// ============================================================
// WIFI + NTP
// ============================================================
void beginWifiAttempt() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWifiAttempt = millis();
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiAttempt >= WIFI_RETRY_MS) beginWifiAttempt();
  }
}

void configureNTP() {
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  ntpConfigured = true;
  lastNtpAttempt = millis();
}

void maintainNTP() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!ntpConfigured) {
    configureNTP();
    return;
  }

  if (!timeReady() && millis() - lastNtpAttempt >= NTP_RETRY_MS) {
    configureNTP();
    return;
  }

  if (millis() - lastNtpKick >= NTP_REFRESH_MS) {
    lastNtpKick = millis();
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  }
}

void printIpToSerialPeriodically() {
  if (WiFi.status() == WL_CONNECTED && millis() - lastIpSerialPrint >= SERIAL_IP_PRINT_MS) {
    lastIpSerialPrint = millis();
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(rssiText());
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("Active State: ");
    Serial.println(stateName(activeState));
  }
}

void maintainWeather(const ClockNow& now) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!isMorningWeatherWindow(now)) return;

  unsigned long ageMs = millis() - weather.lastAttemptMs;
  unsigned long intervalMs = weather.valid ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS;

  if (weather.lastAttemptMs == 0 || ageMs >= intervalMs) {
    fetchWeather();
  }
}

// ============================================================
// WEB + JSON
// ============================================================
String buildStatusJson() {
  ClockNow now = getISTNow();

  String json;
  json.reserve(2200);
  json += "{";
  json += "\"time\":\"" + jsonEscape(formatTimeString(now)) + "\",";
  json += "\"date\":\"" + jsonEscape(formatDateString(now)) + "\",";
  json += "\"ip\":\"" + jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "-") + "\",";
  json += "\"wifi\":\"" + jsonEscape(wifiStateText()) + "\",";
  json += "\"rssi\":\"" + jsonEscape(rssiText()) + "\",";
  json += "\"heap\":\"" + jsonEscape(String(ESP.getFreeHeap()) + " bytes") + "\",";
  json += "\"uptime\":\"" + jsonEscape(formatUptime()) + "\",";
  json += "\"state\":\"" + jsonEscape(String(stateName(activeState))) + "\",";
  json += "\"weather\":\"" + jsonEscape(weatherStatusText()) + "\"";
  json += "}";
  return json;
}

String buildOrientationJson() {
  String json = "{";
  json += "\"panel_flip_x\":";
  json += boolText(FLIP_X);
  json += ",\"panel_flip_y\":";
  json += boolText(FLIP_Y);
  json += ",\"clock_flip_x\":";
  json += boolText(clockFlipX);
  json += ",\"clock_flip_y\":";
  json += boolText(clockFlipY);
  json += ",\"text_flip_x\":";
  json += boolText(textFlipX);
  json += ",\"text_flip_y\":";
  json += boolText(textFlipY);
  json += "}";
  return json;
}

String buildBirthdaysJson() {
  String json = "[";
  for (size_t i = 0; i < BIRTHDAY_COUNT; i++) {
    if (i) json += ",";
    json += "{";
    json += "\"index\":" + String(i) + ",";
    json += "\"name\":\"" + jsonEscape(String(birthdays[i].name)) + "\",";
    json += "\"date\":\"" + jsonEscape(formatDayMonth(birthdays[i].day, birthdays[i].month)) + "\"";
    json += "}";
  }
  json += "]";
  return json;
}

String buildHolidaysJson() {
  String json = "[";
  for (size_t i = 0; i < HOLIDAY_COUNT; i++) {
    if (i) json += ",";
    json += "{";
    json += "\"index\":" + String(i) + ",";
    json += "\"title\":\"" + jsonEscape(String(holidays[i].title)) + "\",";
    json += "\"date\":\"" + jsonEscape(formatDayMonth(holidays[i].day, holidays[i].month)) + "\"";
    json += "}";
  }
  json += "]";
  return json;
}

String buildManualJson() {
  String json = "{";
  json += "\"current\":\"" + jsonEscape(currentManualText.length() ? currentManualText : "-") + "\",";
  json += "\"queue_count\":" + String(manualQueueCount) + ",";
  json += "\"queue\":[";

  for (uint8_t i = 0; i < manualQueueCount; i++) {
    uint8_t idx = (manualQueueHead + i) % MANUAL_QUEUE_SIZE;
    if (i) json += ",";
    json += "{";
    json += "\"index\":" + String(i + 1) + ",";
    json += "\"text\":\"" + jsonEscape(manualQueue[idx].text) + "\",";
    json += "\"duration\":\"" + jsonEscape(String(manualQueue[idx].durationMs / 1000UL) + " sec") + "\"";
    json += "}";
  }

  json += "]";
  json += "}";
  return json;
}

String buildAllJson() {
  String json;
  json.reserve(12000);
  json += "{";
  json += "\"status\":";
  json += buildStatusJson();
  json += ",\"orientation\":";
  json += buildOrientationJson();
  json += ",\"manual\":";
  json += buildManualJson();
  json += ",\"birthdays\":";
  json += buildBirthdaysJson();
  json += ",\"holidays\":";
  json += buildHolidaysJson();
  json += "}";
  return json;
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

void handleApiAll() {
  server.send(200, "application/json; charset=utf-8", buildAllJson());
}

void handleOrientation() {
  clockFlipX = server.arg("clockFlipX") == "1";
  clockFlipY = server.arg("clockFlipY") == "1";
  textFlipX = server.arg("textFlipX") == "1";
  textFlipY = server.arg("textFlipY") == "1";
  server.send(200, "application/json; charset=utf-8", buildOrientationJson());
}

void handleWeatherNow() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/plain", "WiFi not connected");
    return;
  }

  fetchWeather();
  if (!weather.valid) {
    server.send(503, "text/plain", "Weather unavailable");
    return;
  }

  triggerWeatherMessage();
  server.send(200, "text/plain", "Showing weather now");
}

void handleTestBirthday() {
  if (!server.hasArg("index")) {
    server.send(400, "text/plain", "Missing birthday index");
    return;
  }

  int idx = server.arg("index").toInt();
  if (idx < 0 || idx >= (int)BIRTHDAY_COUNT) {
    server.send(400, "text/plain", "Invalid birthday index");
    return;
  }

  triggerBirthdayMessage((size_t)idx);
  server.send(200, "text/plain", "Testing birthday: " + String(birthdays[idx].name));
}

void handleTestHoliday() {
  if (!server.hasArg("index")) {
    server.send(400, "text/plain", "Missing holiday index");
    return;
  }

  int idx = server.arg("index").toInt();
  if (idx < 0 || idx >= (int)HOLIDAY_COUNT) {
    server.send(400, "text/plain", "Invalid holiday index");
    return;
  }

  triggerHolidayMessage((size_t)idx);
  server.send(200, "text/plain", "Testing holiday: " + String(holidays[idx].title));
}

void handleSend() {
  if (!server.hasArg("text")) {
    server.send(400, "text/plain", "Missing text");
    return;
  }

  String msg = sanitizeText(server.arg("text"));
  if (!msg.length()) {
    server.send(400, "text/plain", "Empty message");
    return;
  }

  uint32_t dur = MANUAL_MESSAGE_MS;
  if (server.hasArg("duration")) {
    int sec = server.arg("duration").toInt();
    if (sec > 0) dur = (uint32_t)sec * 1000UL;
  }

  if (!enqueueManualMessage(msg, dur)) {
    server.send(503, "text/plain", "Queue full");
    return;
  }

  server.send(200, "text/plain", "Added to queue");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ============================================================
// DISPLAY UPDATE
// ============================================================
void updateDisplayEngine() {
  ClockNow now = getISTNow();
  unsigned long nowMs = millis();
  bool clockSecondChanged = updateClockSecondsAnimation(now, nowMs);
  applyBrightnessByTime(now);

  finishStateIfExpired();

  // Manual queue gets highest priority
  scheduleManualIfNeeded();
  scheduleBirthdayIfNeeded(now);
  scheduleHolidayIfNeeded(now);
  scheduleWeatherIfNeeded(now);
  scheduleStatusIfNeeded();

  bool forceClockFrame = (activeState == STATE_CLOCK) && clockSecondChanged;
  if (!forceClockFrame && (nowMs - lastDisplayFrame < DISPLAY_REFRESH_MS)) return;
  lastDisplayFrame = nowMs;

  if (activeState == STATE_CLOCK) {
    uint32_t redrawMs = secondsAnimActive ? DISPLAY_REFRESH_MS : CLOCK_REDRAW_MS;
    if (forceClockFrame || (nowMs - lastClockRedraw >= redrawMs)) {
      lastClockRedraw = nowMs;
      drawClock(now, nowMs);
    }
  } else {
    advanceScrollFrame();
  }
}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, DEFAULT_BRIGHTNESS_DAY);
  mx.clear();

  beginWifiAttempt();
  configureNTP();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/all", HTTP_GET, handleApiAll);
  server.on("/api/orientation", HTTP_POST, handleOrientation);
  server.on("/api/weather-now", HTTP_POST, handleWeatherNow);
  server.on("/api/test-birthday", HTTP_POST, handleTestBirthday);
  server.on("/api/test-holiday", HTTP_POST, handleTestHoliday);
  server.on("/api/send", HTTP_POST, handleSend);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Web server started.");
}

void loop() {
  updateDisplayEngine();

  server.handleClient();

  maintainWiFi();
  maintainNTP();

  ClockNow now = getISTNow();
  maintainWeather(now);
}
