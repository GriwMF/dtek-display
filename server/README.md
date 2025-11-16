# DTEK Electricity Schedule Scraper

Python script to fetch electricity shutdown schedules from the DTEK website, bypassing Incapsula protection.

## Requirements

- Python 3.7+
- Chrome/Chromium browser
- ChromeDriver

## Installation

1. Create a virtual environment (required on Arch Linux and other externally-managed Python environments):
```bash
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
```

2. Install Python dependencies:
```bash
pip install -r requirements.txt
```

3. Install Chrome/Chromium browser:
   - Arch Linux: `sudo pacman -S chromium`
   - Ubuntu/Debian: `sudo apt-get install chromium-browser` or `chromium`
   - The script uses `undetected-chromedriver` which automatically downloads ChromeDriver if needed

## Usage

### Command Line Script

Make sure the virtual environment is activated, then run:
```bash
source venv/bin/activate  # On Windows: venv\Scripts\activate
python main.py
```

Or run directly with the venv Python:
```bash
venv/bin/python main.py  # On Windows: venv\Scripts\python main.py
```

Or use the convenience script:
```bash
./run.sh
```

### Web Server (API for ESP32)

**Development server:**
```bash
./run_server.sh
```

**Production server (recommended):**
```bash
./run_prod.sh
```

The server will start on `http://0.0.0.0:5000` with the following endpoints:

**Endpoints:**
- `GET /schedule?password=dtek2024&queue=GPV3.1&days=2` - Full schedule data
- `GET /schedule/simple?password=dtek2024&queue=GPV3.1&days=2` - Compact format for ESP32

**Parameters:**
- `password` - API password (default: `dtek2024`, change via `API_PASSWORD` env var)
- `queue` - Queue name (default: `GPV3.1`)
- `days` - Number of days to return: 1 or 2 (default: 2)

**Change Password:**
```bash
export API_PASSWORD="your_secure_password"
./run_server.sh
```

**Install as system service (autostart on boot):**
```bash
./autostart.sh
```

See `ESP32_EXAMPLE.md` for Arduino/ESP32 code examples.

The script will:
- Load the DTEK shutdowns page using Selenium
- Wait for Incapsula protection to complete
- Extract schedule data from JavaScript variables
- Display the schedule for GPV3.1 queue in the same format as the original bash script

## Output Format

The script outputs:
- Header row: hours 00-23
- Status row: + (power on), - (power off), -+ (first half off), +- (second half off), ? (unknown)

## Troubleshooting

If the script fails:
1. Check that ChromeDriver is installed and in PATH
2. The script saves `debug.html` if parsing fails - inspect it to see what was loaded
3. Increase wait times if Incapsula challenge takes longer
4. Try running without `--headless` mode to see what's happening

