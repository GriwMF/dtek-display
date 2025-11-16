#!/bin/bash
# Script to run the DTEK schedule API server

cd "$(dirname "$0")"
source venv/bin/activate

# Optional: Set custom password via environment variable
# export API_PASSWORD="your_secure_password"

python server.py

