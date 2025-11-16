#include <heltec-eink-modules.h>

// Pick your panel  -  https://github.com/todd-herbert/heltec-eink-modules
// ---------------

    // EInkDisplay_WirelessPaperV1 display;
    // EInkDisplay_WirelessPaperV1_1 display;
    // EInkDisplay_WirelessPaperV1_1_1 display;
    EInkDisplay_WirelessPaperV1_2 display;    


// DTEK Schedule Display for ESP32
// --------------------------------
// Fetches electricity shutdown schedule from API and displays on e-ink screen
// Status codes: 0=no power, 1=power on, 2=first half off, 3=second half off


// Sketch config
// -------------

// WiFi Settings
#define WIFI_SSID ""
#define PASSWORD ""

// API Settings
#define API_URL ""

// Deep sleep duration (microseconds) - 1 hour
#define DEEP_SLEEP_DURATION 3600000000ULL  // 1 hour in microseconds

// -------------

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include <time.h>

// ---- Battery sense config (adjust to your board revision) ----
// If your schematic shows the switched divider (Q3 + R21/R26 = 10k/10k),
// the gain is 2.0. Drive ADC_CTRL low to enable the divider briefly.
#ifndef VBAT_ADC_IN_PIN
#define VBAT_ADC_IN_PIN 20
#endif
#ifndef VBAT_ADC_CTRL_PIN
#define VBAT_ADC_CTRL_PIN 19
#endif
#ifndef VBAT_DIVIDER_GAIN
#define VBAT_DIVIDER_GAIN 2.0f      // R21=10k, R26=10k => 2.0
#endif

// Function prototypes
void logToDisplay(const char* message, bool use_fastmode = true);
void fetchAndDisplaySchedule();
void displaySchedule(const char* queue, JsonArray days);
void drawHourBlock(int x, int y, int width, int height, int status, int hour);
void syncTime();
String getFormattedTime();
float readBatteryVoltage();
String getFormattedBatteryVoltage();
String getCachedBatteryVoltage();

// Cached readings captured before WiFi to avoid ADC2 + WiFi conflicts on S3
static String g_cachedBatteryStr = "";

void setup() {
    // Set ADC resolution to 12 bits
    analogReadResolution(12);

    // Configure ADC control pin (if present) and keep it off (HIGH for P-MOS off) initially
    if (VBAT_ADC_CTRL_PIN >= 0) {
        pinMode(VBAT_ADC_CTRL_PIN, OUTPUT);
        digitalWrite(VBAT_ADC_CTRL_PIN, HIGH);
    }

    // Read and cache battery voltage BEFORE WiFi (ADC2 + WiFi can conflict on Wireless Paper)
    g_cachedBatteryStr = getFormattedBatteryVoltage();
    
    // Prepare display for log messages
    display.landscape();
    display.clear();
    display.fastmodeOn();

    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, PASSWORD);
    uint32_t wifi_connect_start = millis();
    logToDisplay("[WiFi] Connecting...");
    
    bool logged_stillwaiting = false;
    while (WiFi.status() != WL_CONNECTED) {
        uint32_t now = millis();
        if (!logged_stillwaiting && now > wifi_connect_start + 15000) {
            logToDisplay("[WiFi] Still waiting...");
            logged_stillwaiting = true;
        }
        if (millis() > wifi_connect_start + 30000) {
            logToDisplay("[WiFi] Timeout.", false);
            break;  // Exit loop but continue to deep sleep
        }
        yield();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        logToDisplay("[WiFi] Connected!");
        
        // Sync time with NTP server
        syncTime();
        
        // Fetch and display schedule
        fetchAndDisplaySchedule();
    }
    
    // Disconnect WiFi to save power before deep sleep
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Configure hardware for low-power (required for 18uA deep sleep current)
    Platform::prepareToSleep();
    
    // Configure deep sleep for 1 hour
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION);
    
    // Enter deep sleep - device will restart after 1 hour and run setup() again
    // The library will automatically reverse the sleep state when the display is next used
    esp_deep_sleep_start();
}


void loop() {
    // This should never be reached - device goes to deep sleep after setup()
    // If we somehow get here, go to sleep immediately
    Platform::prepareToSleep();
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION);
    esp_deep_sleep_start();
}

// Fetch schedule from API and display it
void fetchAndDisplaySchedule() {
    // Check WiFi connection first
    if (WiFi.status() != WL_CONNECTED) {
        logToDisplay("[API] WiFi not connected!", false);
        return;
    }
    
    logToDisplay("[API] Fetching schedule...");
    
    // Use static client to avoid heap fragmentation from repeated new/delete
    static WiFiClientSecure client;
    client.setInsecure();  // Skip certificate validation for simplicity
    client.setTimeout(10000);  // 10 second timeout
    
    HTTPClient http;
    http.setTimeout(20000);  // 20 second timeout
    http.begin(client, API_URL);
    
    // Add headers
    http.addHeader("User-Agent", "ESP32-DTEK-Display/1.0");
    http.addHeader("Connection", "close");
    
    int http_code = http.GET();
    
    char message[60];
    strcpy(message, "[API] HTTP: ");
    char http_code_str[6];
    itoa(http_code, http_code_str, 10);
    strcat(message, http_code_str);
    logToDisplay(message);
    
    if (http_code != HTTP_CODE_OK) {
        // Log more detailed error info
        if (http_code < 0) {
            strcpy(message, "[API] Error code: ");
            strcat(message, http_code_str);
            logToDisplay(message);
        }
        logToDisplay("[API] Failed. Aborting.", false);
        http.end();
        return;
    }
    
    String payload = http.getString();
    http.end();
    
    logToDisplay("[API] Parsing JSON...");
    
    // Parse JSON (simple format: {"q":"GPV3.1","d":[[...24 values...],[...24 values...]]})
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        logToDisplay("[API] JSON parse error.", false);
        return;
    }
    
    const char* queue = doc["q"];
    JsonArray days = doc["d"];
    
    if (!queue || days.size() == 0) {
        logToDisplay("[API] Invalid data.", false);
        return;
    }
    
    logToDisplay("[API] Displaying...");
    
    // Clear display and turn off fast mode for better rendering
    display.clear();
    display.fastmodeOff();
    
    // Display the schedule
    displaySchedule(queue, days);
    
    // Update display
    display.update();
}

// Display schedule on e-ink display
void displaySchedule(const char* queue, JsonArray days) {
    int screen_width = display.bounds.full.width();
    int screen_height = display.bounds.full.height();
    
    // Header: Queue name
    display.setCursor(5, 5);
    display.print("DTEK Schedule");
    
    // Display current time on the right side
    String timeStr = getFormattedTime();
    int timeX = screen_width - (timeStr.length() * 6) - 5;  // Approximate width, right-aligned
    if (timeX < 5) timeX = 5;  // Ensure it doesn't go off screen
    display.setCursor(timeX, 5);
    display.print(timeStr);
    
    // Display battery voltage below time, aligned with Queue
    String batteryStr = getCachedBatteryVoltage();
    int batteryX = screen_width - (batteryStr.length() * 6) - 5;  // Right-aligned
    if (batteryX < 5) batteryX = 5;
    display.setCursor(batteryX, 20);
    display.print(batteryStr);
    
    display.setCursor(5, 20);
    display.print("Queue: ");
    display.print(queue);
    
    // Calculate layout - make bars smaller to fit hour labels
    int header_height = 35;
    int hour_label_height = 10;  // Space for hour labels below bars (reduced)
    int day_height = (screen_height - header_height) / days.size();
    int hour_width = screen_width / 24;
    int bar_height = day_height - 25;  // Smaller bars, leaving room for labels
    
    // Display each day
    for (int day_idx = 0; day_idx < days.size() && day_idx < 2; day_idx++) {
        JsonArray hours = days[day_idx];
        int day_y = header_height + (day_idx * day_height);
        
        // Day label
        display.setCursor(5, day_y);
        display.print(day_idx == 0 ? "Today:" : "Tomorrow:");
        
        // Hour blocks
        int block_y = day_y + 12;
        
        for (int hour = 0; hour < 24 && hour < hours.size(); hour++) {
            int status = hours[hour];
            int block_x = hour * hour_width;
            drawHourBlock(block_x, block_y, hour_width - 1, bar_height, status, hour);
            
            // Display pivot hours (every 3 hours: 0, 3, 6, 9, 12, 15, 18, 21) below the bars
            if (hour % 3 == 0) {
                // Draw vertical line from bottom of bar to hour label (left side of bar)
                int line_start_y = block_y + bar_height;
                int line_end_y = block_y + bar_height + 3;
                int line_x = block_x + 1;  // Left side of the bar
                
                for (int py = line_start_y; py <= line_end_y; py++) {
                    display.drawPixel(line_x, py, BLACK);
                }
                
                // Display hour number below the line (moved up)
                display.setCursor(block_x + 1, block_y + bar_height + 4);
                display.print(hour);
            }
        }
    }
}

// Draw a single hour block with status indicator using drawPixel
void drawHourBlock(int x, int y, int width, int height, int status, int hour) {
    Color block_color;
    
    // Determine color based on status
    // 0=no power (black), 1=power on (white), 2=first half off, 3=second half off
    switch (status) {
        case 0:  // No power - fill with black
            block_color = BLACK;
            // Fill entire block
            for (int py = y; py < y + height; py++) {
                for (int px = x; px < x + width; px++) {
                    display.drawPixel(px, py, block_color);
                }
            }
            break;
        case 1:  // Power on - white background with border
            // Fill with white (already white, but draw border)
            for (int py = y; py < y + height; py++) {
                for (int px = x; px < x + width; px++) {
                    if (px == x || px == x + width - 1 || py == y || py == y + height - 1) {
                        display.drawPixel(px, py, BLACK);  // Border
                    } else {
                        display.drawPixel(px, py, WHITE);  // Background
                    }
                }
            }
            break;
        case 2:  // First half off - fill left half with black, draw / line (top-left to bottom-right)
            // Fill with white and border, then fill first half with black
            for (int py = y; py < y + height; py++) {
                for (int px = x; px < x + width; px++) {
                    if (px == x || px == x + width - 1 || py == y || py == y + height - 1) {
                        display.drawPixel(px, py, BLACK);  // Border
                    } else {
                        // Calculate diagonal line y position for this x
                        int diag_y = y + ((px - x) * height / width);
                        // Fill left half (below the / line) with black
                        if (py < diag_y) {
                            display.drawPixel(px, py, BLACK);
                        } else {
                            display.drawPixel(px, py, WHITE);  // Background
                        }
                    }
                }
            }
            // Draw diagonal line / (top-left to bottom-right)
            for (int px = x; px < x + width; px++) {
                int py = y + ((px - x) * height / width);
                if (py >= y && py < y + height) {
                    display.drawPixel(px, py, BLACK);
                }
            }
            break;
        case 3:  // Second half off - fill right half with black, draw \ line (bottom-left to top-right)
            // Fill with white and border, then fill second half with black
            for (int py = y; py < y + height; py++) {
                for (int px = x; px < x + width; px++) {
                    if (px == x || px == x + width - 1 || py == y || py == y + height - 1) {
                        display.drawPixel(px, py, BLACK);  // Border
                    } else {
                        // Calculate diagonal line y position for this x
                        int diag_y = y + height - 1 - ((px - x) * height / width);
                        // Fill right half (below the \ line) with black
                        if (py > diag_y) {
                            display.drawPixel(px, py, BLACK);
                        } else {
                            display.drawPixel(px, py, WHITE);  // Background
                        }
                    }
                }
            }
            // Draw diagonal line \ (bottom-left to top-right)
            for (int px = x; px < x + width; px++) {
                int py = y + height - 1 - ((px - x) * height / width);
                if (py >= y && py < y + height) {
                    display.drawPixel(px, py, BLACK);
                }
            }
            break;
        default:  // Unknown - gray border
            for (int py = y; py < y + height; py++) {
                for (int px = x; px < x + width; px++) {
                    if (px == x || px == x + width - 1 || py == y || py == y + height - 1) {
                        display.drawPixel(px, py, BLACK);  // Border
                    } else {
                        display.drawPixel(px, py, WHITE);  // Background
                    }
                }
            }
            break;
    }
}

// Log sketch info to display, instead of serial monitor
void logToDisplay(const char* message, bool use_fastmode) {

    // If not a blank message, insert a bullet point
    if (strlen(message) > 0)
        display.print("- ");

    display.println(message);
    display.setCursor(0, display.getCursorY() + 3); // Extra line spacing 
    
    // Draw without fastmode, for display health
    if (!use_fastmode) {
        display.fastmodeOff();
        display.update();
        display.fastmodeOn();
    }
    
    // Not cleaning. Use fastmode
    else
        display.update();
}

// Sync time with NTP server
void syncTime() {
    // Configure NTP - UTC+2 (Ukraine timezone)
    configTime(7200, 0, "pool.ntp.org", "time.nist.gov");  // UTC+2 (7200 seconds), no DST offset
    
    // Wait for time to be set (up to 10 seconds)
    logToDisplay("[NTP] Syncing time...");
    time_t now = time(nullptr);
    int retries = 0;
    while (now < 1000000000 && retries < 20) {  // Check if time is valid (after year 2001)
        delay(500);
        now = time(nullptr);
        retries++;
    }
    
    if (now >= 1000000000) {
        logToDisplay("[NTP] Time synced");
    } else {
        logToDisplay("[NTP] Time sync failed");
    }
}

// Get formatted time string (HH:MM)
String getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--:--";
    }
    
    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    return String(timeStr);
}

// Read battery voltage from ADC
float readBatteryVoltage() {
    // Battery sense via switched divider: drive ADC Ctrl LOW (turn on P-MOS), read ADC_IN, then turn it off
    const int batteryAdcPin = VBAT_ADC_IN_PIN;
    const float voltageDividerRatio = VBAT_DIVIDER_GAIN;
    const float adcReferenceVoltage = 3.3;
    const int adcResolution = 4095;
    
    // Enable the divider if we have a control pin
    if (VBAT_ADC_CTRL_PIN >= 0) {
        digitalWrite(VBAT_ADC_CTRL_PIN, LOW);   // P-MOS on
        delay(5);                               // allow to settle
    }
    // Ensure sufficient headroom for ~0..3.3V mapping
    analogSetPinAttenuation(batteryAdcPin, ADC_11db);
    
    // Read ADC value (average of 10 readings for stability)
    int adcValue = 0;
    for (int i = 0; i < 10; i++) {
        adcValue += analogRead(batteryAdcPin);
        delay(1);
    }
    adcValue = adcValue / 10;
    
    // Turn off the divider to save leakage
    if (VBAT_ADC_CTRL_PIN >= 0) {
        digitalWrite(VBAT_ADC_CTRL_PIN, HIGH);  // P-MOS off
    }
    
    // Calculate battery voltage
    float v_adc = adcValue * (adcReferenceVoltage / adcResolution);
    float voltage = v_adc * voltageDividerRatio;
    return voltage;
}

// Get formatted battery voltage string (X.XXV)
String getFormattedBatteryVoltage() {
    float voltage = readBatteryVoltage();
    char voltageStr[8];
    dtostrf(voltage, 4, 2, voltageStr);  // Format: 4 total width, 2 decimal places
    String result = String(voltageStr);
    result += "V";
    return result;
}

// Return cached battery string (measured before WiFi)
String getCachedBatteryVoltage() {
    if (g_cachedBatteryStr.length() == 0) {
        g_cachedBatteryStr = getFormattedBatteryVoltage();
    }
    return g_cachedBatteryStr;
}

