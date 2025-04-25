// ────────────────────────────────────────────────
//  INCLUDE LIBRARIES
// ────────────────────────────────────────────────
// Include necessary libraries for WiFi connectivity, secure HTTP requests,
// JSON data handling, E-Paper display control, GUI elements for the display,
// pre-defined image data, mathematical functions, and time synchronization.
#include <WiFi.h>           // For WiFi connection
#include <WiFiClientSecure.h> // For HTTPS requests
#include <HTTPClient.h>     // For making HTTP requests
#include <Arduino_JSON.h>   // For parsing JSON data from API
#include "EPD.h"            // E-Paper Display driver
#include "EPD_GUI.h"        // GUI library for E-Paper Display
#include "pic.h"            // Contains image data (like icons)
#include <math.h>           // For mathematical operations (like round)
#include <time.h>           // For Network Time Protocol (NTP) and local time functions

// ────────────────────────────────────────────────
//  ⚙️  USER SETTINGS
// ────────────────────────────────────────────────
// Define WiFi network credentials
const char* ssid     = "yourwifissid"; // Your WiFi network name (SSID)
const char* password = "yourwifipassword";    // Your WiFi password

// API Key for OpenWeatherMap service
String openWeatherMapApiKey = "youropenweathermaponecall3.0key";

// Define the geographical location for weather data
// Location (Memphis, Tennessee, USA) - Original Comment
const char* CITY_NAME = "Memphis"; // City name to display
const char* LAT       = "35.1495"; // Latitude of the location
const char* LON       = "-90.0490"; // Longitude of the location

// Define timezone information using the standard TZ format for NTP synchronization
// This string specifies the time zone (CST), standard time offset (-6 hours),
// daylight saving time zone (CDT), and the rules for when DST starts and ends.
// Example: "CST6CDT,M3.2.0/02:00:00,M11.1.0/02:00:00" - Original Comment
const char* TZ_INFO = "CST6CDT,M3.2.0/02:00:00,M11.1.0/02:00:00";

// Set the frequency for updating the weather data and display, in minutes.
// Update interval in minutes: choose 15, 30, or 60 - Original Comment
const uint32_t UPDATE_INTERVAL_MINUTES = 15;  // human-readable update interval

// Choose the time format for the display (true for 24-hour, false for 12-hour AM/PM)
// Display time format: true for 24h, false for 12h - Original Comment
const bool USE_24_HOUR_FORMAT = false;

// ────────────────────────────────────────────────
//  DISPLAY BUFFERS & GLOBAL VARIABLES
// ────────────────────────────────────────────────
// Buffer to hold the image data for the E-Paper display (Black/White). Size depends on display resolution.
uint8_t ImageBW[15000];

// String to store the JSON response received from the weather API
String jsonBuffer;
// Integer to store the HTTP status code from the API request (e.g., 200 for OK)
int     httpResponseCode;
// Variable to hold the parsed JSON object
JSONVar myObject;

// String variables to store extracted weather information
String weather;      // Weather condition description (e.g., "Clouds")
String temperature;  // Current temperature
String humidity;     // Current humidity percentage
String visibility;   // Current visibility distance
String wind_speed;   // Current wind speed
String maxtemp;      // Today's maximum predicted temperature
String city_js;      // City name (copied from CITY_NAME setting)
// Integer flag representing the weather condition category for icon selection
int    weather_flag = 0; // 0: Atmosphere/Fallback, 1: Clouds, 2: Thunderstorm, 3: Clear, 4: Snow, 5: Rain/Drizzle

// ────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ────────────────────────────────────────────────
// Declare functions before they are defined, so they can be called from setup() or loop()
String httpGETRequest(const char* serverName); // Function to make an HTTPS GET request
void   js_analysis();                          // Function to fetch and parse weather JSON data
void   UI_weather_forecast();                  // Function to draw the weather info on the E-Paper display
void   clear_all();                            // Function to clear the E-Paper display (optional use)

// ────────────────────────────────────────────────
//  SETUP FUNCTION - RUNS ONCE AT STARTUP
// ────────────────────────────────────────────────
void setup() {
  // Initialize serial communication at 115200 baud rate for debugging output
  Serial.begin(115200);

  // Start connecting to the WiFi network using the defined SSID and password
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  // Wait until the WiFi connection is established
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.'); // Print dots to show connection progress
    delay(500);        // Wait 500 milliseconds before checking again
  }
  // Print confirmation and the device's IP address once connected
  Serial.print("\nConnected! IP: ");
  Serial.println(WiFi.localIP());

  // Configure GPIO pin 7 as an output to control the E-Paper display power
  pinMode(7, OUTPUT);
  // Turn on the power to the E-Paper display
  digitalWrite(7, HIGH); // E-paper power - Original Comment

  // Initialize the GPIO pins used by the E-Paper display hardware
  EPD_GPIOInit();

  // Configure the system time using NTP (Network Time Protocol)
  // Set the timezone using TZ_INFO and specify an NTP server pool
  // Initialize NTP with timezone - Original Comment
  configTzTime(TZ_INFO, "pool.ntp.org");

  // Wait until the system time has been successfully synchronized via NTP
  // Wait for time sync - Original Comment
  Serial.print("Waiting for NTP time sync");
  time_t now = time(nullptr); // Get current time (will be epoch 0 or incorrect before sync)
  // Check if the time is still very low (indicating it hasn't synchronized yet)
  while (now < 8 * 3600 * 2) { // Loop until time is reasonably greater than epoch zero
    Serial.print('.');
    delay(500);
    now = time(nullptr); // Check time again
  }
  Serial.println(); // Newline after sync message

  // Get the current synchronized time and format it for printing
  struct tm timeinfo;
  localtime_r(&now, &timeinfo); // Convert time_t to tm structure (local time)
  char timeBuf[32];
  // Format the time into a human-readable string (YYYY-MM-DD HH:MM:SS)
  strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.print("Current local time: ");
  Serial.println(timeBuf); // Print the synchronized local time
}

// ────────────────────────────────────────────────
//  MAIN LOOP FUNCTION - RUNS REPEATEDLY
// ────────────────────────────────────────────────
void loop() {
  // Fetch weather data from the API and parse the JSON response
  js_analysis();          // fetch & decode weather - Original Comment
  // Update the E-Paper display with the fetched weather information
  UI_weather_forecast();  // render to E-paper - Original Comment
  // Wait for the defined update interval before running the loop again
  // Convert minutes to milliseconds for the delay function
  delay(UPDATE_INTERVAL_MINUTES * 60UL * 1000UL);
}

// ────────────────────────────────────────────────
//  FETCH & PARSE WEATHER DATA (OpenWeatherMap One Call 3.0 API)
// ────────────────────────────────────────────────
void js_analysis() {
  // Check if WiFi is still connected before attempting API call
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    return; // Exit the function if not connected
  }

  // Construct the API request URL string with location, exclusions, units, and API key
  String serverPath =
      String("https://api.openweathermap.org/data/3.0/onecall?lat=") + LAT +
      "&lon=" + LON +
      "&exclude=minutely,hourly,alerts" // Exclude data we don't need
      "&units=imperial"                 // Request units in Imperial (Fahrenheit, mph)
      "&appid=" + openWeatherMapApiKey; // Append the API key

  // Attempt to get weather data, retrying if the HTTP request fails (not code 200)
  // retry until HTTP 200 - Original Comment
  do {
    // Make the HTTPS GET request using the helper function
    jsonBuffer = httpGETRequest(serverPath.c_str());
    // Try to parse the received JSON string
    myObject   = JSON.parse(jsonBuffer);
    // Short delay before potentially retrying
    delay(2000);
  } while (httpResponseCode != 200); // Continue looping if HTTP response code is not OK (200)

  // ── pull fields from JSON ─────────────────────
  // Extract the main weather description (e.g., "Clear", "Clouds", "Rain")
  weather = String((const char*) myObject["current"]["weather"][0]["main"]);

  // Extract the numerical weather condition ID
  // capture the weather condition ID - Original Comment
  int w_id = int((double) myObject["current"]["weather"][0]["id"]);

  // Extract the humidity percentage
  humidity = JSON.stringify(myObject["current"]["humidity"]);

  // Extract wind speed (originally in m/s), convert to mph, and format to 1 decimal place
  // Convert wind from m/s to mph and format - Original Comment
  { // Scope block to limit temporary variable 'mph'
    double mph = double(myObject["current"]["wind_speed"]) * 2.23694; // Conversion factor
    wind_speed = String(mph, 1); // Convert double to String with 1 decimal place
  }

  // Extract visibility (originally in meters), convert to miles, and format to 1 decimal place
  // visibility in miles from meters - Original Comment
  { // Scope block to limit temporary variables 'vis_m', 'vis_mi'
    double vis_m  = double(myObject["current"]["visibility"]);
    double vis_mi = vis_m * 0.000621371; // Conversion factor
    visibility    = String(vis_mi, 1); // Convert double to String with 1 decimal place
  }

  // Extract current temperature, round it to the nearest integer, and convert to String
  // round temperatures - Original Comment
  { // Scope block to limit temporary variable 't'
    double t = double(myObject["current"]["temp"]);
    temperature = String(int(round(t))); // Round and convert to integer String
  }
  // Extract today's maximum temperature, round it, and convert to String
  { // Scope block to limit temporary variable 't'
    double t = double(myObject["daily"][0]["temp"]["max"]); // Get max temp from the first day's forecast
    maxtemp = String(int(round(t))); // Round and convert to integer String
  }

  // Assign the configured city name to the display variable
  city_js = CITY_NAME;

  // Determine the weather_flag based on OpenWeatherMap condition ID ranges
  // This flag is used to select the appropriate weather icon from the 'pic.h' array.
  // map ranges to your Weather_Num indices - Original Comment
  if      (w_id >= 200 && w_id < 300) weather_flag = 2; // Thunderstorm Category
  else if (w_id >= 300 && w_id < 600) weather_flag = 5; // Drizzle + Rain Category
  else if (w_id >= 600 && w_id < 700) weather_flag = 4; // Snow Category
  else if (w_id >= 700 && w_id < 800) weather_flag = 0; // Atmosphere Category (Mist, Fog, etc.)
  else if (w_id == 800)               weather_flag = 3; // Clear Category
  else if (w_id > 800 && w_id < 900)  weather_flag = 1; // Clouds Category
  else                                weather_flag = 0; // Default/fallback category

  // ── debug out ─────────────────────────────────
  // Print the extracted and processed weather data to the Serial Monitor for debugging
  Serial.print("Weather: ");
  Serial.println(weather);

  Serial.print("w_id: ");
  Serial.println(w_id);

  Serial.print("weather_flag: ");
  Serial.println(weather_flag);

  Serial.print("Temp °F: ");
  Serial.println(temperature);

  Serial.print("High °F: ");
  Serial.println(maxtemp);

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("Visibility: ");
  Serial.print(visibility);
  Serial.println(" mi");

  Serial.print("Wind Speed: "); // Corrected label from original comment "Wind km/h"
  Serial.print(wind_speed);
  Serial.println(" mph"); // Corrected unit from original comment

  // Get and print the current time to show when the data was last processed
  time_t now2 = time(nullptr);
  struct tm t2;
  localtime_r(&now2, &t2);
  char tsBuf[32];
  strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", &t2);
  Serial.print("Last updated at: ");
  Serial.println(tsBuf);

  // Print the HTTP response code received from the last API request
  Serial.print("HTTP response code: ");
  Serial.println(httpResponseCode);

  // Print the WiFi signal strength (RSSI - Received Signal Strength Indicator)
  Serial.print("WiFi RSSI (dBm): ");
  Serial.println(WiFi.RSSI());

  Serial.println("-----------------------------"); // Separator for readability
}

// ────────────────────────────────────────────────
//  HTTP GET REQUEST HELPER FUNCTION (HTTPS, Insecure)
// ────────────────────────────────────────────────
// Performs an HTTPS GET request to the specified server URL.
// NOTE: Uses setInsecure(), which disables SSL certificate validation.
// This is common for ESP32 projects but less secure than proper validation.
String httpGETRequest(const char* serverName) {
  // Create a WiFiClientSecure object for HTTPS communication
  WiFiClientSecure client;
  // Disable SSL/TLS certificate validation (USE WITH CAUTION)
  client.setInsecure();

  // Create an HTTPClient object
  HTTPClient http;
  // Initialize the HTTP request, linking it with the secure client and server URL
  http.begin(client, serverName);
  // Send the GET request and store the HTTP status code received
  httpResponseCode = http.GET();

  // Initialize payload string to an empty JSON object (default in case of error)
  String payload = "{}";
  // Check if the request was successful (HTTP code > 0)
  if (httpResponseCode > 0) {
    // Get the response body (payload) as a String
    payload = http.getString();
  } else {
    // Print an error message if the HTTP request failed
    Serial.print("HTTP error: ");
    Serial.println(httpResponseCode);
  }
  // End the HTTP connection and release resources
  http.end();
  // Return the received payload (JSON data or "{}" on error)
  return payload;
}

// ────────────────────────────────────────────────
//  UPDATE E-PAPER DISPLAY USER INTERFACE
// ────────────────────────────────────────────────
// Renders the weather information onto the E-Paper display.
void UI_weather_forecast() {
  // Buffer for formatting strings before displaying them
  char buffer[40];

  // Re-initialize GPIO pins for the E-Paper display (might be needed after sleep)
  EPD_GPIOInit();
  // Clear the E-Paper's internal memory (optional, depending on display type/driver)
  EPD_Clear();
  // Create a new image buffer in memory, setting width, height, rotation (0), and background color (WHITE)
  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  // Set the entire E-Paper screen to white initially (full refresh)
  EPD_Full(WHITE);
   // Display the blank white image buffer (clears the screen visually)
  EPD_Display_Part(0, 0, EPD_W, EPD_H, ImageBW);
  // Initialize the E-Paper display for fast partial updates (e.g., 1.5 seconds refresh time)
  EPD_Init_Fast(Fast_Seconds_1_5s);

  // --- Draw static UI elements ---
  // Draw the main weather icon based on the calculated weather_flag
  EPD_ShowPicture(7,   0,   184, 208, Weather_Num[weather_flag], WHITE); // Weather icon array from pic.h
  // Draw decorative background images/icons for different sections
  EPD_ShowPicture(205, 20,  184, 88,  gImage_city,  WHITE); // City section background
  EPD_ShowPicture(6,   238, 96,  40,  gImage_wind,  WHITE); // Wind section background
  EPD_ShowPicture(205, 120, 184, 88,  gImage_hum,   WHITE); // Humidity section background
  EPD_ShowPicture(112, 238, 144, 40,  gImage_tem,   WHITE); // Temperature section background
  EPD_ShowPicture(265, 238, 128, 40,  gImage_visi,  WHITE); // Visibility section background

  // Draw dividing lines on the display
  EPD_DrawLine(0,   230, 400, 230, BLACK); // Horizontal line near bottom
  EPD_DrawLine(200, 0,   200, 230, BLACK); // Vertical line in middle
  EPD_DrawLine(200, 115, 400, 115, BLACK); // Horizontal line in top-right quadrant

  // --- Draw dynamic weather data ---
  // Display City Name
  snprintf(buffer, sizeof(buffer), "%s ", city_js.c_str()); // Format city name string
  EPD_ShowString(290, 74, buffer, 24, BLACK); // Draw string at specified coordinates with font size 24

  // Display Current Temperature
  snprintf(buffer, sizeof(buffer), "%s F", temperature.c_str()); // Format temperature string with unit
  EPD_ShowString(53,  171, buffer, 48, BLACK); // Draw string with font size 48

  // Display Today's High Temperature
  snprintf(buffer, sizeof(buffer), "High %s F", maxtemp.c_str()); // Format high temp string
  EPD_ShowString(170, 273, buffer, 16, BLACK); // Draw string with font size 16

  // Display Humidity
  snprintf(buffer, sizeof(buffer), "%s%%", humidity.c_str()); // Format humidity string with '%'
  EPD_ShowString(290, 171, buffer, 48, BLACK); // Draw string with font size 48

  // Display Wind Speed
  snprintf(buffer, sizeof(buffer), "%s mph", wind_speed.c_str()); // Format wind speed string with unit
  EPD_ShowString(54, 273, buffer, 16, BLACK); // Draw string with font size 16

  // Display Visibility
  snprintf(buffer, sizeof(buffer), "%s miles", visibility.c_str()); // Format visibility string with unit
  EPD_ShowString(320, 273, buffer, 16, BLACK); // Draw string with font size 16

  // --- Draw Last Updated Time ---
  // draw last update time (smallest font) - Original Comment
  { // Scope block for time variables
    time_t now3 = time(nullptr); // Get current time
    struct tm timeinfo3;
    localtime_r(&now3, &timeinfo3); // Convert to local time structure
    char timeBuf3[16]; // Buffer for formatted time string
    // Format time based on the USE_24_HOUR_FORMAT setting
    if (USE_24_HOUR_FORMAT) {
      // Format as HH:MM (24-hour)
      snprintf(timeBuf3, sizeof(timeBuf3), "Updated: %02d:%02d",
               timeinfo3.tm_hour, timeinfo3.tm_min);
    } else {
      // Format as HH:MM AM/PM (12-hour)
      int hr = timeinfo3.tm_hour % 12; // Convert 24h hour to 12h format
      if (hr == 0) hr = 12; // Handle midnight (0 hour) as 12 AM
      const char* ampm = timeinfo3.tm_hour < 12 ? "AM" : "PM"; // Determine AM or PM
      snprintf(timeBuf3, sizeof(timeBuf3), "Updated: %02d:%02d %s",
               hr, timeinfo3.tm_min, ampm);
    }
    // Draw the formatted time string at the top-left with font size 16
    EPD_ShowString(10, 10, timeBuf3, 16, BLACK);
  }

  // Send the completed image buffer (ImageBW) to the E-Paper display for partial update
  EPD_Display_Part(0, 0, EPD_W, EPD_H, ImageBW);
  // Put the E-Paper display into a low-power sleep mode until the next update
  EPD_Sleep();
}

// ────────────────────────────────────────────────
//  CLEAR DISPLAY FUNCTION (Optional)
// ────────────────────────────────────────────────
// Function to completely clear the E-Paper display to white.
// Note: This function is defined but not called in the main loop.
void clear_all() {
  // Clear the E-Paper's internal memory
  EPD_Clear();
  // Create a blank white image buffer
  Paint_NewImage(ImageBW, EPD_W, EPD_H, 0, WHITE);
  // Set the entire E-Paper screen to white (full refresh)
  EPD_Full(WHITE);
  // Display the blank white image buffer
  EPD_Display_Part(0, 0, EPD_W, EPD_H, ImageBW);
}
