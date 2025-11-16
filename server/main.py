#!/usr/bin/env python3
"""
Script to fetch electricity shutdown schedule from DTEK website.
Bypasses Incapsula protection using Selenium with undetected-chromedriver.
"""

import json
import time
import datetime

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


def setup_driver():
    """Setup Chrome driver with options to avoid detection."""
    if USE_UNDETECTED:
        # Use undetected-chromedriver for better bot detection bypass
        options = uc.ChromeOptions()
        options.add_argument('--headless')
        options.add_argument('--no-sandbox')
        options.add_argument('--disable-dev-shm-usage')
        try:
            driver = uc.Chrome(options=options, version_main=None)
            return driver
        except Exception as e:
            print(f"Error setting up undetected Chrome driver: {e}")
            raise
    else:
        # Fallback to regular Selenium
        chrome_options = Options()
        chrome_options.add_argument('--headless')
        chrome_options.add_argument('--no-sandbox')
        chrome_options.add_argument('--disable-dev-shm-usage')
        chrome_options.add_argument('--disable-blink-features=AutomationControlled')
        chrome_options.add_experimental_option("excludeSwitches", ["enable-automation"])
        chrome_options.add_experimental_option('useAutomationExtension', False)
        chrome_options.add_argument('user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36')
        
        try:
            driver = webdriver.Chrome(options=chrome_options)
            driver.execute_cdp_cmd('Page.addScriptToEvaluateOnNewDocument', {
                'source': '''
                    Object.defineProperty(navigator, 'webdriver', {
                        get: () => undefined
                    })
                '''
            })
            return driver
        except Exception as e:
            print(f"Error setting up Chrome driver: {e}")
            print("\nPlease ensure ChromeDriver is installed.")
            print("You can install it with: sudo apt-get install chromium-chromedriver")
            print("Or use: pip install undetected-chromedriver")
            raise


def extract_json_from_browser(driver):
    """Extract preset and fact JSON directly from browser JavaScript context."""
    preset_json = None
    fact_json = None
    
    try:
        # Execute JavaScript to get the DisconSchedule objects and serialize to JSON
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


def get_schedule_status(fact_json, preset_json, queue="GPV3.1", timestamp=None):
    """Extract schedule status for a given queue and timestamp."""
    if not fact_json or not preset_json:
        return None
    
    # Use provided timestamp or get today's timestamp from JSON
    if timestamp is None:
        timestamp = fact_json.get('today')
        if not timestamp:
            # Fallback: calculate today's timestamp
            today = datetime.date.today()
            timestamp = int(time.mktime(today.timetuple()))
    
    timestamp_str = str(timestamp)
    
    if timestamp_str not in fact_json.get('data', {}):
        return None
    
    queue_data = fact_json.get('data', {}).get(timestamp_str, {}).get(queue, {})
    
    if not queue_data:
        return None
    
    # Generate status line for 24 hours (00-23)
    # Note: The JSON uses hours 1-24, so we need to map them to 0-23
    status_line = []
    for hour in range(24):
        hour_str = str(hour + 1)  # JSON uses 1-24, not 0-23
        status = queue_data.get(hour_str, '')
        
        if status == "yes":
            status_line.append("+ ")
        elif status == "no":
            status_line.append("- ")
        elif status == "first":
            status_line.append("-+")
        elif status == "second":
            status_line.append("+-")
        else:
            status_line.append("? ")
    
    return status_line


def main():
    url = "https://www.dtek-dnem.com.ua/ua/shutdowns"
    queue = "GPV3.1"  # Default queue, can be made configurable
    
    print(f"Fetching today's schedule for {queue}...")
    print("-" * 50)
    
    driver = None
    try:
        driver = setup_driver()
        print("Loading page...")
        driver.get(url)
        
        # Wait for page to load and Incapsula challenge to complete
        if not USE_UNDETECTED:
            try:
                WebDriverWait(driver, 30).until(
                    lambda d: 'DisconSchedule' in d.page_source
                )
            except TimeoutException:
                print("Warning: Page load timeout. Trying to proceed anyway...")
        
        # Additional wait for JavaScript to execute and Incapsula to complete
        max_wait = 30
        waited = 0
        while waited < max_wait:
            try:
                # Try to access DisconSchedule object in JavaScript
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
            if waited % 6 == 0:
                print(f"Waiting for page to load... ({waited}s)")
        
        if waited >= max_wait:
            print("Warning: May not have fully loaded. Proceeding anyway...")
        
        # Extract JSON data directly from browser JavaScript context
        preset_json, fact_json = extract_json_from_browser(driver)
        
        if not preset_json or not fact_json:
            print("Error: Could not extract schedule data from page.")
            print("Page might still be loading or structure changed.")
            print("\nSaving page source to debug.html for inspection...")
            with open('debug.html', 'w', encoding='utf-8') as f:
                f.write(driver.page_source)
            print("Please check debug.html to see what was loaded.")
            return 1
        
        # Get today's timestamp
        today_timestamp = fact_json.get('today')
        if not today_timestamp:
            today = datetime.date.today()
            today_timestamp = int(time.mktime(today.timetuple()))
        
        # Calculate tomorrow's timestamp (add 24 hours = 86400 seconds)
        tomorrow_timestamp = today_timestamp + 86400
        
        # Generate header with proper spacing (each hour is 3 chars: "00 ")
        header_parts = []
        for i in range(24):
            header_parts.append(f"{i:02d}")
        header = " ".join(header_parts)
        
        # Get today's schedule
        today_status = get_schedule_status(fact_json, preset_json, queue, today_timestamp)
        # Get tomorrow's schedule
        tomorrow_status = get_schedule_status(fact_json, preset_json, queue, tomorrow_timestamp)
        
        if not today_status and not tomorrow_status:
            print("Error: Could not extract schedule data for", queue)
            # Show available queues from the first available date
            if fact_json.get('data'):
                first_date = list(fact_json['data'].keys())[0]
                available_queues = list(fact_json['data'][first_date].keys())
                print("Available queues:", available_queues)
            return 1
        
        # Display today's schedule
        print("\nToday:")
        print(header)
        if today_status:
            today_row = " ".join(today_status)
            print(today_row)
        else:
            print("No data available")
        
        # Display tomorrow's schedule
        print("\nTomorrow:")
        print(header)
        if tomorrow_status:
            tomorrow_row = " ".join(tomorrow_status)
            print(tomorrow_row)
        else:
            print("No data available")
        
        return 0
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return 1
        
    finally:
        if driver:
            driver.quit()


if __name__ == "__main__":
    exit(main())
