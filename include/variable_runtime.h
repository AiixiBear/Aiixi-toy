// Run-Time Variables //
JsonDocument jsondata;

// m5::rtc_time_t TimeStruct;
// m5::rtc_date_t DateStruct;

m5::rtc_datetime_t localTime;

uint16_t screen_x;
uint16_t screen_y;

uint16_t bg_color = TFT_BLACK;
uint16_t txt_color = TFT_WHITE;

enum screen {
  STICKC,
  STICKCPLUS2,
};

enum screen current_screen;

unsigned long previousMillis = 0;
int8_t lcd_brightness = 100;
int timeout_ScreenOn = 180000; // 3 minutes

// Name of the app currently on screen, shown in the toolbar. The home
// screen sets it to "Home"; entering an app sets it to the app's name.
String currentAppName = "Home";

// timezone offset (hours and minutes for local time zone from UTC)
int8_t timezone_h = 0; 
int8_t timezone_m = 0;

String pin_UNLOCK = "";
int passwordMode = 0; // 0 random, 1 fixed
String Wifi_Mode = "AP"; // Default mode
String Wifi_SSID = "";
String Wifi_PASSWORD = "";

bool _mdnsStarted = false;