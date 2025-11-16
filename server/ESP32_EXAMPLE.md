# ESP32 Usage Examples

## Arduino/ESP32 Example Code

### Simple Format (Recommended for ESP32)

The `/schedule/simple` endpoint returns compact data optimized for low-memory devices:

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";
const char* serverUrl = "http://your-server-ip:5000/schedule/simple?password=dtek2024&queue=GPV3.1";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  
  getSchedule();
}

void getSchedule() {
  HTTPClient http;
  http.begin(serverUrl);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    
    // Parse JSON
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);
    
    String queue = doc["q"];
    JsonArray days = doc["d"];
    
    Serial.println("Schedule for " + queue);
    
    for (int day = 0; day < days.size(); day++) {
      Serial.print("Day ");
      Serial.print(day == 0 ? "Today: " : "Tomorrow: ");
      
      JsonArray hours = days[day];
      for (int hour = 0; hour < 24; hour++) {
        int status = hours[hour];
        // Status: 0=no power, 1=power on, 2=first half off, 3=second half off
        
        if (status == 1) Serial.print("+ ");
        else if (status == 0) Serial.print("- ");
        else if (status == 2) Serial.print("-+");
        else if (status == 3) Serial.print("+-");
        else Serial.print("? ");
      }
      Serial.println();
    }
  } else {
    Serial.printf("HTTP Error: %d\n", httpCode);
  }
  
  http.end();
}

void loop() {
  // Update every 5 minutes
  delay(300000);
  getSchedule();
}
```

### Full Format

For more detailed information, use `/schedule`:

```cpp
void getDetailedSchedule() {
  HTTPClient http;
  http.begin("http://your-server-ip:5000/schedule?password=dtek2024&queue=GPV3.1&days=2");
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    
    DynamicJsonDocument doc(8192);
    deserializeJson(doc, payload);
    
    String queue = doc["queue"];
    String updateTime = doc["update_time"];
    JsonArray days = doc["days"];
    
    for (JsonObject day : days) {
      String date = day["date"];
      String dayName = day["day_name"];
      
      Serial.println(date + " (" + dayName + ")");
      
      JsonArray schedule = day["schedule"];
      for (JsonObject hour : schedule) {
        int h = hour["hour"];
        String status = hour["status"];
        Serial.printf("Hour %02d: %s\n", h, status.c_str());
      }
    }
  }
  
  http.end();
}
```

## API Response Examples

### Simple Format Response
```json
{
  "q": "GPV3.1",
  "d": [
    [1,1,1,3,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1],
    [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]
  ]
}
```

### Full Format Response
```json
{
  "queue": "GPV3.1",
  "update_time": "16.11.2025 09:39",
  "days": [
    {
      "date": "2025-11-16",
      "day_name": "Saturday",
      "timestamp": 1763244000,
      "schedule": [
        {"hour": 0, "status": "yes"},
        {"hour": 1, "status": "yes"},
        {"hour": 2, "status": "yes"},
        {"hour": 3, "status": "second"},
        ...
      ]
    }
  ]
}
```

## Status Codes

### Simple Format
- `0` = No power
- `1` = Power on
- `2` = First half off (first 30 minutes)
- `3` = Second half off (last 30 minutes)
- `-1` = Unknown

### Full Format
- `"yes"` = Power on
- `"no"` = No power
- `"first"` = First half off
- `"second"` = Second half off
- `"maybe"` = Possible outage
- `"mfirst"` = Maybe first half off
- `"msecond"` = Maybe second half off

