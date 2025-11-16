# DTEK Schedule Server

Python API server that scrapes electricity shutdown schedules from the DTEK website, bypassing Incapsula protection.

## Technical Overview

The server uses Selenium with undetected-chromedriver to:
1. Load the DTEK shutdowns page
2. Wait for Incapsula protection to complete
3. Extract schedule data from JavaScript variables
4. Serve the data via REST API

## Requirements

- Python 3.7+
- Chrome/Chromium browser
- ChromeDriver (auto-downloaded by undetected-chromedriver)

## Quick Start

For deployment instructions, see the main [README.md](../README.md).

### Development Mode

```bash
./run_server.sh  # Flask development server
```

### Production Mode
```bash
./run_prod.sh    # Gunicorn production server
```

### Command Line Tool
```bash
./run.sh         # Fetch and display schedule in terminal
```

## API Documentation

### Endpoints

#### Health Check
`GET /health`
- Returns server status
- No authentication required

#### Full Schedule
`GET /schedule?password=...&queue=GPV3.1&days=2`
- Returns complete schedule data with metadata
- Includes queue info, timestamps, and detailed status

#### Simple Schedule (for ESP32)
`GET /schedule/simple?password=...&queue=GPV3.1&days=2`
- Returns compact JSON format optimized for microcontrollers
- Minimal payload size for low-memory devices

### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `password` | string | Required | API password (set via `API_PASSWORD` env var) |
| `queue` | string | `GPV3.1` | DTEK queue identifier |
| `days` | integer | `2` | Number of days (1 or 2) |

### Response Formats

#### Simple Format (ESP32)
```json
{
  "q": "GPV3.1",
  "d": [
    [1,1,0,0,2,3,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1],
    [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1]
  ]
}
```

**Status Codes:**
- `0` - No power
- `1` - Power on
- `2` - First half off
- `3` - Second half off

### Environment Variables

- `API_PASSWORD` - Set the API password (default: `dtek2024`)
- `PORT` - Server port (default: `5000`)

## Terminal Output Format

When running `main.py` or `./run.sh`:
- Header row: hours 00-23
- Status row: `+` (power on), `-` (power off), `-+` (first half off), `+-` (second half off), `?` (unknown)

## Production Deployment

For detailed production deployment with systemd service and Nginx, see [PRODUCTION.md](PRODUCTION.md).

## Troubleshooting

### Scraping Issues
1. The script saves `debug.html` if parsing fails - inspect it to see what was loaded
2. Increase wait times if Incapsula challenge takes longer
3. Try running without `--headless` mode in `main.py` to see browser behavior

### Chrome/Chromium Issues
- Ubuntu: If `chromium-browser` package not found, try `chromium`
- The script uses undetected-chromedriver which auto-downloads matching ChromeDriver

### Service Issues
- Check logs: `sudo journalctl -u dtek-schedule -f`
- Verify paths in `dtek-schedule.service` are absolute
- Ensure virtual environment is activated in service file

