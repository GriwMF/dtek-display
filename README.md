Server + Heltec Wireless Paper(ESP32) client for displaying dtek graphics

## Deploy on Ubuntu (via git clone)

This project includes a Python API server in `server/` that scrapes and serves DTEK schedules for an ESP32 client. Below are quick steps to deploy it on an Ubuntu server using git clone. For advanced/production details, see `server/PRODUCTION.md` and `server/README.md`.

### 1) Install system dependencies

```bash
sudo apt update
sudo apt install -y git python3 python3-venv python3-pip chromium-browser chromium-driver
```

Notes:
- If `chromium-browser` is unavailable, try `sudo apt install -y chromium` instead.
- The app uses undetected-chromedriver and Selenium; a Chromium/Chrome browser must be present.

### 2) Clone the repository

```bash
cd ~
git clone https://github.com/GriwMF/dtek-display
cd dtek-display/server
```

### 3) Create virtualenv and install Python deps

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

Optional but recommended:
```bash
export API_PASSWORD="$(openssl rand -base64 24)"  # set a strong API password
```

### 4) Test run (foreground)

```bash
./run_prod.sh
# Server listens on 0.0.0.0:5000
# Health check:  curl http://127.0.0.1:5000/health
```

Stop with Ctrl+C when you’re ready to install as a service.

### 5) Install as a systemd service (autostart)

Edit `server/dtek-schedule.service` and update these fields for your system:
- `User=` to your Linux username
- `WorkingDirectory=` to the absolute path of `.../dtek-display/server`
- `Environment="PATH=..."` to `.../dtek-display/server/venv/bin`
- `Environment="API_PASSWORD=..."` to your chosen password (or omit if set via environment)
- `ExecStart=` to point at `.../dtek-display/server/venv/bin/gunicorn ... server:app`

Then install and start:
```bash
./autostart.sh
```

Useful commands:
```bash
sudo systemctl status dtek-schedule
sudo journalctl -u dtek-schedule -f
sudo systemctl restart dtek-schedule
```

### 6) (Optional) Expose via Nginx

For internet exposure, put Nginx in front and proxy to `127.0.0.1:5000`. See `server/PRODUCTION.md` for a ready-to-use Nginx example and HTTPS with Let’s Encrypt.

### 7) Firewall quick-start (optional)

```bash
# If exposing directly
sudo ufw allow 5000/tcp

# If using Nginx
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
```

### API endpoints (defaults)
- `GET /health` — service health
- `GET /schedule?password=...&queue=GPV3.1&days=2`
- `GET /schedule/simple?password=...&queue=GPV3.1&days=2`

See `server/README.md` for complete usage and parameters.
