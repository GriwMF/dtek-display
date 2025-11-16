#!/usr/bin/env python3
"""
Flask web server to provide electricity shutdown schedule data.
Designed for ESP32 consumption with simple password protection.
"""

import json
import time
import datetime
import os
import shutil
from flask import Flask, jsonify, request
from functools import wraps

try:
    import undetected_chromedriver as uc
    USE_UNDETECTED = True
except ImportError:
    USE_UNDETECTED = False
    from selenium import webdriver
    from selenium.webdriver.chrome.options import Options
    from selenium.webdriver.common.by import By
    from selenium.webdriver.support.ui import WebDriverWait
    from selenium.common.exceptions import TimeoutException

app = Flask(__name__)

# Simple password - can be set via environment variable or changed here
API_PASSWORD = os.environ.get('API_PASSWORD', 'dtek2025')

# Cache for schedule data (to avoid hammering the DTEK website)
cache = {
    'data': None,
    'timestamp': 0,
    'ttl': 300  # Cache for 5 minutes
}


def require_password(f):
    """Decorator to require password authentication."""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        password = request.args.get('password', '')
        if password != API_PASSWORD:
            return jsonify({
                'error': 'Unauthorized',
                'message': 'Invalid or missing password'
            }), 401
        return f(*args, **kwargs)
    return decorated_function


def find_chromedriver():
    """Find ChromeDriver executable in common locations or PATH."""
    # Check PATH first (most universal)
    chromedriver_path = shutil.which('chromedriver')
    if chromedriver_path:
        return chromedriver_path
    
    # Common system locations
    common_paths = [
        '/usr/bin/chromedriver',
        '/usr/local/bin/chromedriver',
        '/snap/bin/chromium.chromedriver',
    ]
    
    for path in common_paths:
        if os.path.exists(path) and os.access(path, os.X_OK):
            return path
    
    # Return None to let undetected-chromedriver auto-download (may fail on ARM)
    return None


def find_chromium_binary():
    """Find Chromium binary in common locations or PATH."""
    # Check PATH first
    chromium_path = shutil.which('chromium-browser') or shutil.which('chromium')
    if chromium_path:
        return chromium_path
    
    # Common system locations
    common_paths = [
        '/usr/bin/chromium-browser',
        '/usr/bin/chromium',
        '/snap/bin/chromium',
    ]
    
    for path in common_paths:
        if os.path.exists(path) and os.access(path, os.X_OK):
            return path
    
    return None


def setup_driver():
    """Setup Chrome driver with options to avoid detection."""
    if USE_UNDETECTED:
        options = uc.ChromeOptions()
        options.add_argument('--headless')
        options.add_argument('--no-sandbox')
        options.add_argument('--disable-dev-shm-usage')
        
        # Find Chromium binary and ChromeDriver
        chromium_binary = find_chromium_binary()
        chromedriver_path = find_chromedriver()
        
        # Set Chromium binary location if found
        if chromium_binary:
            options.binary_location = chromium_binary
        
        # Use system ChromeDriver if found
        if chromedriver_path:
            driver = uc.Chrome(options=options, driver_executable_path=chromedriver_path, version_main=None)
        else:
            # Fallback: let it auto-download (may fail on ARM)
            driver = uc.Chrome(options=options, version_main=None)
        return driver
    else:
        chrome_options = Options()
        chrome_options.add_argument('--headless')
        chrome_options.add_argument('--no-sandbox')
        chrome_options.add_argument('--disable-dev-shm-usage')
        chrome_options.add_argument('--disable-blink-features=AutomationControlled')
        chrome_options.add_experimental_option("excludeSwitches", ["enable-automation"])
        chrome_options.add_experimental_option('useAutomationExtension', False)
        chrome_options.add_argument('user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36')
        
        driver = webdriver.Chrome(options=chrome_options)
        driver.execute_cdp_cmd('Page.addScriptToEvaluateOnNewDocument', {
            'source': 'Object.defineProperty(navigator, "webdriver", {get: () => undefined})'
        })
        return driver


def extract_json_from_browser(driver):
    """Extract preset and fact JSON directly from browser JavaScript context."""
    preset_json = None
    fact_json = None
    
    try:
        preset_json_str = driver.execute_script("""
            if (typeof DisconSchedule !== 'undefined' && DisconSchedule.preset) {
                return JSON.stringify(DisconSchedule.preset);
            }
            return null;
        """)
        if preset_json_str:
            preset_json = json.loads(preset_json_str)
    except Exception as e:
        print(f"Warning: Could not extract preset JSON: {e}")
    
    try:
        fact_json_str = driver.execute_script("""
            if (typeof DisconSchedule !== 'undefined' && DisconSchedule.fact) {
                return JSON.stringify(DisconSchedule.fact);
            }
            return null;
        """)
        if fact_json_str:
            fact_json = json.loads(fact_json_str)
    except Exception as e:
        print(f"Warning: Could not extract fact JSON: {e}")
    
    return preset_json, fact_json


def fetch_schedule_data():
    """Fetch schedule data from DTEK website."""
    url = "https://www.dtek-dnem.com.ua/ua/shutdowns"
    
    driver = None
    try:
        driver = setup_driver()
        driver.get(url)
        
        # Wait for JavaScript to execute
        max_wait = 30
        waited = 0
        while waited < max_wait:
            try:
                result = driver.execute_script("""
                    return typeof DisconSchedule !== 'undefined' && 
                           DisconSchedule.fact && 
                           DisconSchedule.preset;
                """)
                if result:
                    break
            except:
                pass
            time.sleep(2)
            waited += 2
        
        preset_json, fact_json = extract_json_from_browser(driver)
        
        if not preset_json or not fact_json:
            return None
        
        return {
            'preset': preset_json,
            'fact': fact_json
        }
        
    except Exception as e:
        print(f"Error fetching schedule: {e}")
        return None
    finally:
        if driver:
            driver.quit()


def get_cached_data():
    """Get cached data or fetch new data if cache expired."""
    current_time = time.time()
    
    if cache['data'] is None or (current_time - cache['timestamp']) > cache['ttl']:
        print("Cache expired or empty, fetching new data...")
        data = fetch_schedule_data()
        if data:
            cache['data'] = data
            cache['timestamp'] = current_time
        return data
    
    print("Returning cached data")
    return cache['data']


def parse_schedule_for_queue(fact_json, queue, timestamp):
    """Parse schedule for a specific queue and timestamp."""
    timestamp_str = str(timestamp)
    
    if timestamp_str not in fact_json.get('data', {}):
        return None
    
    queue_data = fact_json['data'][timestamp_str].get(queue, {})
    if not queue_data:
        return None
    
    schedule = []
    for hour in range(24):
        hour_str = str(hour + 1)
        status = queue_data.get(hour_str, 'unknown')
        schedule.append({
            'hour': hour,
            'status': status
        })
    
    return schedule


@app.route('/')
def index():
    """Simple info page."""
    return jsonify({
        'service': 'DTEK Schedule API',
        'version': '1.0',
        'endpoints': {
            '/schedule': 'Get schedule data (requires ?password=xxx)',
            '/health': 'Health check'
        },
        'usage': 'GET /schedule?password=YOUR_PASSWORD&queue=GPV3.1'
    })


@app.route('/health')
def health():
    """Health check endpoint."""
    return jsonify({'status': 'ok'})


@app.route('/schedule')
@require_password
def get_schedule():
    """
    Get schedule data for a specific queue.
    
    Query parameters:
    - password: API password (required)
    - queue: Queue name (default: GPV3.1)
    - days: Number of days to return (1 or 2, default: 2)
    """
    queue = request.args.get('queue', 'GPV3.1')
    days = int(request.args.get('days', '2'))
    
    # Get data from cache or fetch new
    data = get_cached_data()
    
    if not data:
        return jsonify({
            'error': 'Failed to fetch schedule data',
            'message': 'Could not retrieve data from DTEK website'
        }), 500
    
    fact_json = data['fact']
    
    # Get today's timestamp
    today_timestamp = fact_json.get('today')
    if not today_timestamp:
        today = datetime.date.today()
        today_timestamp = int(time.mktime(today.timetuple()))
    
    result = {
        'queue': queue,
        'update_time': fact_json.get('update', 'unknown'),
        'days': []
    }
    
    # Get schedule for requested number of days
    for day_offset in range(days):
        day_timestamp = today_timestamp + (day_offset * 86400)
        schedule = parse_schedule_for_queue(fact_json, queue, day_timestamp)
        
        if schedule:
            day_date = datetime.datetime.fromtimestamp(day_timestamp)
            result['days'].append({
                'date': day_date.strftime('%Y-%m-%d'),
                'day_name': day_date.strftime('%A'),
                'timestamp': day_timestamp,
                'schedule': schedule
            })
    
    if not result['days']:
        return jsonify({
            'error': 'No data available',
            'message': f'No schedule data found for queue {queue}'
        }), 404
    
    return jsonify(result)


@app.route('/schedule/simple')
@require_password
def get_schedule_simple():
    """
    Get simplified schedule data optimized for ESP32 (minimal payload).
    
    Returns compact format: array of status codes for each hour.
    Status codes: 0=no power, 1=power on, 2=first half off, 3=second half off
    
    Query parameters:
    - password: API password (required)
    - queue: Queue name (default: GPV3.1)
    - days: Number of days (1 or 2, default: 2)
    """
    queue = request.args.get('queue', 'GPV3.1')
    days = int(request.args.get('days', '2'))
    
    data = get_cached_data()
    
    if not data:
        return jsonify({'error': 'Failed to fetch data'}), 500
    
    fact_json = data['fact']
    today_timestamp = fact_json.get('today')
    if not today_timestamp:
        today = datetime.date.today()
        today_timestamp = int(time.mktime(today.timetuple()))
    
    result = {
        'q': queue,
        'd': []
    }
    
    status_map = {
        'yes': 1,
        'no': 0,
        'first': 2,
        'second': 3
    }
    
    for day_offset in range(days):
        day_timestamp = today_timestamp + (day_offset * 86400)
        timestamp_str = str(day_timestamp)
        
        if timestamp_str in fact_json.get('data', {}):
            queue_data = fact_json['data'][timestamp_str].get(queue, {})
            if queue_data:
                day_schedule = []
                for hour in range(24):
                    hour_str = str(hour + 1)
                    status = queue_data.get(hour_str, 'unknown')
                    day_schedule.append(status_map.get(status, -1))
                result['d'].append(day_schedule)
    
    return jsonify(result)


if __name__ == '__main__':
    print("=" * 60)
    print("DTEK Schedule API Server")
    print("=" * 60)
    print(f"API Password: {API_PASSWORD}")
    print("Change password by setting API_PASSWORD environment variable")
    print("\nEndpoints:")
    print("  GET /schedule?password=xxx&queue=GPV3.1")
    print("  GET /schedule/simple?password=xxx&queue=GPV3.1")
    print("\nStarting server on http://0.0.0.0:5000")
    print("=" * 60)
    
    app.run(host='0.0.0.0', port=5000, debug=False)

