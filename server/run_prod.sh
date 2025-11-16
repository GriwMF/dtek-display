#!/bin/bash
# Production server using Gunicorn

cd "$(dirname "$0")"
source venv/bin/activate

# Optional: Set custom password via environment variable
# export API_PASSWORD="your_secure_password"

# Run with Gunicorn
# -w 2: 2 worker processes (adjust based on your CPU cores)
# -b 0.0.0.0:5000: bind to all interfaces on port 5000
# --timeout 120: 120 second timeout (needed for Selenium operations)
# --access-logfile -: log to stdout
# --error-logfile -: log errors to stdout

gunicorn -w 2 \
  -b 0.0.0.0:5000 \
  --timeout 120 \
  --access-logfile - \
  --error-logfile - \
  server:app

