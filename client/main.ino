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
int getCurrentHour();
uint64_t calculateSleepUntilNextHour();
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

    // Connect to WiFi with retry logic
    WiFi.mode(WIFI_STA);
    int wifi_attempts = 0;
    const int MAX_WIFI_ATTEMPTS = 3;
    bool wifi_connected = false;
    
    while (wifi_attempts < MAX_WIFI_ATTEMPTS && !wifi_connected) {
        wifi_attempts++;
        
        if (wifi_attempts > 1) {
            logToDisplay("[WiFi] Retrying...");
            WiFi.disconnect();
            delay(1000);
        }
        
        WiFi.begin(WIFI_SSID, PASSWORD);
        uint32_t wifi_connect_start = millis();
        
        char attempt_msg[30];
        sprintf(attempt_msg, "[WiFi] Connecting (%d/%d)...", wifi_attempts, MAX_WIFI_ATTEMPTS);
        logToDisplay(attempt_msg);
        
        while (WiFi.status() != WL_CONNECTED) {
            uint32_t now = millis();
            
            // After 15 seconds, retry from scratch
            if (now > wifi_connect_start + 15000) {
                logToDisplay("[WiFi] Still waiting...");
                break;  // Break to retry from scratch
            }
            
            yield();
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            wifi_connected = true;
        }
    }
    
    if (wifi_connected) {
        logToDisplay("[WiFi] Connected!");
        
        // Sync time with NTP server
        syncTime();
        
        // Fetch and display schedule
        fetchAndDisplaySchedule();
    } else {
        logToDisplay("[WiFi] Failed after 3 attempts.", false);
    }
    
    // Disconnect WiFi to save power before deep sleep
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Configure hardware for low-power (required for 18uA deep sleep current)
    Platform::prepareToSleep();
    
    // Calculate dynamic sleep duration until next hour boundary (00:00:30)
    uint64_t sleep_duration = calculateSleepUntilNextHour();
    
    // Configure deep sleep to wake up at next hour boundary
    esp_sleep_enable_timer_wakeup(sleep_duration);
    
    // Enter deep sleep - device will restart at next hour boundary and run setup() again
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
    
    // HTTP retry logic
    int http_attempts = 0;
    const int MAX_HTTP_ATTEMPTS = 3;
    bool http_success = false;
    String payload = "";
    
    while (http_attempts < MAX_HTTP_ATTEMPTS && !http_success) {
        http_attempts++;
        
        if (http_attempts > 1) {
            logToDisplay("[API] Retrying...");
            delay(1000);
        }
        
        HTTPClient http;
        http.setTimeout(20000);  // 20 second timeout
        http.begin(client, API_URL);
        
        // Add headers
        http.addHeader("User-Agent", "ESP32-DTEK-Display/1.0");
        http.addHeader("Connection", "close");
        
        char attempt_msg[30];
        sprintf(attempt_msg, "[API] Attempt (%d/%d)...", http_attempts, MAX_HTTP_ATTEMPTS);
        logToDisplay(attempt_msg);
        
        int http_code = http.GET();
        
        char message[60];
        strcpy(message, "[API] HTTP: ");
        char http_code_str[6];
        itoa(http_code, http_code_str, 10);
        strcat(message, http_code_str);
        logToDisplay(message);
        
        if (http_code == HTTP_CODE_OK) {
            payload = http.getString();
            http.end();
            http_success = true;
        } else {
            // Log more detailed error info
            if (http_code < 0) {
                strcpy(message, "[API] Error code: ");
                strcat(message, http_code_str);
                logToDisplay(message);
            }
            http.end();
            
            if (http_attempts >= MAX_HTTP_ATTEMPTS) {
                logToDisplay("[API] Failed after 3 attempts.", false);
                return;
            }
        }
    }
    
    if (!http_success) {
        logToDisplay("[API] Failed. Aborting.", false);
        return;
    }
    
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
    int bar_height = day_height - 28;  // Smaller bars (reduced by 3px), leaving room for labels
    
    // Get current hour for highlighting
    int current_hour = getCurrentHour();
    
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
            
            // Make current hour bar taller (+2px) and adjust y position to keep alignment
            int this_bar_height = bar_height;
            int this_block_y = block_y;
            bool is_current_hour = (day_idx == 0 && hour == current_hour);
            
            if (is_current_hour) {
                this_bar_height = bar_height + 2;  // Make current hour bar 2px taller
                this_block_y = block_y - 2;  // Move up 2px to keep bottom aligned with other bars
            }
            
            drawHourBlock(block_x, this_block_y, hour_width - 1, this_bar_height, status, hour);
            
            // Display pivot hours (every 3 hours: 0, 3, 6, 9, 12, 15, 18, 21) below the bars
            if (hour % 3 == 0) {
                // Draw vertical line from bottom of bar to hour label (left side of bar)
                int line_start_y = this_block_y + this_bar_height;
                int line_end_y = this_block_y + this_bar_height + 3;
                int line_x = block_x + 1;  // Left side of the bar
                
                for (int py = line_start_y; py <= line_end_y; py++) {
                    display.drawPixel(line_x, py, BLACK);
                }
                
                // Display hour number below the line (moved up)
                display.setCursor(block_x + 1, this_block_y + this_bar_height + 4);
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
            // Fill with white and border, then fill left half with black
            for (int py = y; py < y + height; py++) {
                for (int px = x; px < x + width; px++) {
                    if (px == x || px == x + width - 1 || py == y || py == y + height - 1) {
                        display.drawPixel(px, py, BLACK);  // Border
                    } else {
                        // Calculate diagonal line y position for this x
                        int diag_y = y + ((px - x) * height / width);
                        // Fill left half (below the / line) with black
                        // For "/" line, pixels below the line (py > diag_y) are in the left/bottom area
                        if (py > diag_y) {
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
        case 3:  // Second half off - fill bottom-left with black, draw \ line (bottom-left to top-right)
            // Fill with white and border, then fill bottom-left part with black
            for (int py = y; py < y + height; py++) {
                for (int px = x; px < x + width; px++) {
                    if (px == x || px == x + width - 1 || py == y || py == y + height - 1) {
                        display.drawPixel(px, py, BLACK);  // Border
                    } else {
                        // Calculate diagonal line y position for this x
                        int diag_y = y + height - 1 - ((px - x) * height / width);
                        // Fill bottom-left part (below/on the \ line) with black
                        // For "\" line, pixels at or below the line (py >= diag_y) are in the bottom-left area
                        if (py >= diag_y) {
                            display.drawPixel(px, py, BLACK);
                        } else {
                            display.drawPixel(px, py, WHITE);  // Top-right part is white
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

// Get current hour (0-23)
int getCurrentHour() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return -1;  // Return -1 if time not available
    }
    return timeinfo.tm_hour;
}

// Calculate sleep duration until next hour boundary (00:00:30)
// Returns duration in microseconds
uint64_t calculateSleepUntilNextHour() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        // If time not available, fall back to 1 hour
        return DEEP_SLEEP_DURATION;
    }
    
    // Calculate seconds until next hour
    int current_minute = timeinfo.tm_min;
    int current_second = timeinfo.tm_sec;
    
    // Seconds remaining in current hour
    int seconds_until_hour = (60 - current_minute) * 60 - current_second;
    
    // Add 30 seconds to wake up at :00:30 to account for timing accuracy
    int sleep_seconds = seconds_until_hour + 30;
    
    // Convert to microseconds
    return (uint64_t)sleep_seconds * 1000000ULL;
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

