#!/bin/bash
# Script to install and enable the DTEK schedule service

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVICE_FILE="$SCRIPT_DIR/dtek-schedule.service"
SERVICE_NAME="dtek-schedule.service"

echo "Installing DTEK Schedule Service..."
echo "======================================"

# Check if service file exists
if [ ! -f "$SERVICE_FILE" ]; then
    echo "Error: Service file not found: $SERVICE_FILE"
    exit 1
fi

# Copy service file
echo "Copying service file to /etc/systemd/system/..."
sudo cp "$SERVICE_FILE" /etc/systemd/system/

# Reload systemd
echo "Reloading systemd daemon..."
sudo systemctl daemon-reload

# Enable service
echo "Enabling service to start on boot..."
sudo systemctl enable "$SERVICE_NAME"

# Start service
echo "Starting service..."
sudo systemctl start "$SERVICE_NAME"

echo ""
echo "======================================"
echo "Service installed and started!"
echo ""
echo "Useful commands:"
echo "  sudo systemctl status dtek-schedule   # Check status"
echo "  sudo systemctl restart dtek-schedule  # Restart service"
echo "  sudo systemctl stop dtek-schedule     # Stop service"
echo "  sudo journalctl -u dtek-schedule -f   # View logs"
echo ""
echo "To disable autostart:"
echo "  sudo systemctl disable dtek-schedule"
echo "======================================"

