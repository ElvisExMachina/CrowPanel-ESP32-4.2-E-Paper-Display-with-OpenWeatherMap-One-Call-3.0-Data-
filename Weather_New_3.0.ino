#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include "EPD.h"
#include "EPD_GUI.h"
#include "pic.h"
#include <math.h>

// ────────────────────────────────────────────────
//  ⚙️  USER SETTINGS
// ────────────────────────────────────────────────
const char* ssid     = "YOURWIFISSID";
const char* password = "yourwifipassword";

String openWeatherMapApiKey = "youropenweathermapapikey";

// Memphis, Tennessee, USA
const char* CITY_NAME = "Memphis";
const char* LAT       = "35.1495";
const char* LON       = "-90.0490";

// ────────────────────────────────────────────────
//  DISPLAY BUFFERS
// ────────────────────────────────────────────────
uint8_t ImageBW[15000];

String jsonBuffer;
int     httpResponseCode;
JSONVar myObject;

String weather;
String temperature;
String humidity;
String visibility;          // renamed from sea_level
String wind_speed;
String maxtemp;
String city_js;
int    weather_flag = 0;

// ────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ────────────────────────────────────────────────
String httpGETRequest(const char* serverName);
void   js_analysis();
void   UI_weather_forecast();
void   clear_all();

// ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  Serial.print("\nConnected! IP: ");
  Serial.println(WiFi.localIP());

  pinMode(7, OUTPUT);          // E‑paper power
  digitalWrite(7, HIGH);

  EPD_GPIOInit();
}

void loop() {
  js_analysis();          // fetch & decode weather
  UI_weather_forecast();  // render to E‑paper
  delay(60UL * 60UL * 1000UL);   // update hourly
}

// ────────────────────────────────────────────────
//  FETCH & PARSE (One Call 3.0)
// ────────────────────────────────────────────────
void js_analysis() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    return;
  }

  String serverPath =
      String("https://api.openweathermap.org/data/3.0/onecall?lat=") + LAT +
      "&lon=" + LON +
      "&exclude=minutely,hourly,alerts"
      "&units=imperial"
      "&appid=" + openWeatherMapApiKey;

  // retry until HTTP 200
  do {
    jsonBuffer = httpGETRequest(serverPath.c_str());
    myObject   = JSON.parse(jsonBuffer);
    delay(2000);
  } while (httpResponseCode != 200);

  // ── pull fields from JSON ─────────────────────
  weather      = JSON.stringify(myObject["current"]["weather"][0]["main"]);
  temperature  = JSON.stringify(myObject["current"]["temp"]);
  humidity     = JSON.stringify(myObject["current"]["humidity"]);
  visibility   = JSON.stringify(myObject["current"]["visibility"]);
  maxtemp      = JSON.stringify(myObject["daily"][0]["temp"]["max"]);
  {
    // wind: m s⁻¹ → km h⁻¹ (1 dp)
    double kmh = double(myObject["current"]["wind_speed"]) * 3.6;
    wind_speed = String(kmh, 1);
  }
  {
    // visibility in miles from meters
    double vis_m  = double(myObject["current"]["visibility"]);   // meters
    double vis_mi = vis_m * 0.000621371;                         // miles
    visibility    = String(vis_mi, 1);                           // one decimal
}
// ➋ current temperature – round to nearest 1 °F
{
  double t = double(myObject["current"]["temp"]);   // 70.8 …
  int    r = int(round(t));                         // 71
  temperature = String(r);                          // "71"
}

// ➌ today’s max temperature – round to nearest 1 °F
{
  double t = double(myObject["daily"][0]["temp"]["max"]);
  int    r = int(round(t));
  maxtemp = String(r);                               // "74"
}

  city_js = CITY_NAME;

  // ── pick icon set ─────────────────────────────
  if      (weather.indexOf("Clouds")      != -1) weather_flag = 1;
  else if (weather.indexOf("Clear")       != -1) weather_flag = 3;
  else if (weather.indexOf("Rain")        != -1) weather_flag = 5;
  else if (weather.indexOf("Thunderstorm")!= -1) weather_flag = 2;
  else if (weather.indexOf("Snow")        != -1) weather_flag = 4;
  else if (weather.indexOf("Mist")        != -1 ||
           weather.indexOf("Fog")         != -1 ||
           weather.indexOf("Haze")        != -1) weather_flag = 0;

  // ── debug out ─────────────────────────────────
  Serial.println("Weather:  " + weather);
  Serial.println("Temp °F:  " + temperature);
  Serial.println("High Temp °F:  " + maxtemp);
  Serial.println("Humidity: " + humidity + " %");
  Serial.println("Visibility: " + visibility  + " mi");
  Serial.println("Wind km/h:" + wind_speed);
}

// ────────────────────────────────────────────────
//  HTTP helper (HTTPS, insecure)
// ────────────────────────────────────────────────
String httpGETRequest(const char* serverName) {
  WiFiClientSecure client;
  client.setInsecure();          // skip TLS cert validation

  HTTPClient http;
  http.begin(client, serverName);
  httpResponseCode = http.GET();

  String payload = "{}";
  if (httpResponseCode > 0) {
    payload = http.getString();
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpResponseCode);
  }
  http.end();
  return payload;
}

// ────────────────────────────────────────────────
//  E‑paper UI (unchanged except unit labels)
// ────────────────────────────────────────────────
void UI_weather_forecast() {
  char buffer[40];
  EPD_GPIOInit();
  EPD_Clear();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  EPD_Full(WHITE);
  EPD_Display_Part(0, 0, EPD_W, EPD_H, ImageBW);
  EPD_Init_Fast(Fast_Seconds_1_5s);

  // icons & separators … (same as before)
  EPD_ShowPicture(7,   0,   184, 208, Weather_Num[weather_flag], WHITE);
  EPD_ShowPicture(205, 20,  184, 88,  gImage_city,  WHITE);
  EPD_ShowPicture(6,   238, 96,  40,  gImage_wind,  WHITE);
  EPD_ShowPicture(205, 120, 184, 88,  gImage_hum,   WHITE);
  EPD_ShowPicture(112, 238, 144, 40,  gImage_tem,   WHITE);
  EPD_ShowPicture(265, 238, 128, 40,  gImage_visi,  WHITE);

  EPD_DrawLine(0,   230, 400, 230, BLACK);
  EPD_DrawLine(200, 0,   200, 230, BLACK);
  EPD_DrawLine(200, 115, 400, 115, BLACK);

  // city
  snprintf(buffer, sizeof(buffer), "%s ", city_js.c_str());
  EPD_ShowString(290, 74, buffer, 24, BLACK);

  // temp
  snprintf(buffer, sizeof(buffer), "%s F", temperature.c_str());
  EPD_ShowString(53,  171, buffer, 48, BLACK);

  // High Temp
  snprintf(buffer, sizeof(buffer), "High %s F", maxtemp.c_str());
  EPD_ShowString(170, 273, buffer, 16, BLACK);

  // humidity
  snprintf(buffer, sizeof(buffer), "%s%%", humidity.c_str());
  EPD_ShowString(290, 171, buffer, 48, BLACK);

  // wind
  snprintf(buffer, sizeof(buffer), "%s mph", wind_speed.c_str());
  EPD_ShowString(54, 273, buffer, 16, BLACK);

  // visibility
  snprintf(buffer, sizeof(buffer), "%s miles", visibility.c_str());
  EPD_ShowString(320, 273, buffer, 16, BLACK);

  EPD_Display_Part(0, 0, EPD_W, EPD_H, ImageBW);
  EPD_Sleep();
}

void clear_all() {
  EPD_Clear();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  EPD_Full(WHITE);
  EPD_Display_Part(0, 0, EPD_W, EPD_H, ImageBW);
}
