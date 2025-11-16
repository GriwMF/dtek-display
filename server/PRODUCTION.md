# Production Deployment Guide

## Option 1: Run with Gunicorn (Recommended)

### Install Gunicorn

```bash
source venv/bin/activate
pip install gunicorn
```

### Run Production Server

```bash
./run_prod.sh
```

This will start Gunicorn with:
- 2 worker processes
- 120 second timeout (for Selenium operations)
- Bound to all interfaces on port 5000
- Access and error logging

### Customize Workers

Edit `run_prod.sh` and adjust the `-w` parameter:
- For CPU-bound tasks: `workers = (2 × CPU cores) + 1`
- For this app (mostly I/O waiting): 2-4 workers is usually enough

```bash
# Example: 4 workers
gunicorn -w 4 -b 0.0.0.0:5000 --timeout 120 server:app
```

## Option 2: Run as Systemd Service (Linux)

### Install the Service

1. Install Gunicorn:
```bash
source venv/bin/activate
pip install gunicorn
```

2. Edit `dtek-schedule.service` and update:
   - `User=griwsl` (change to your username)
   - `WorkingDirectory=/home/griwsl/py-graphic` (change to your path)
   - `API_PASSWORD=dtek2024` (change to your password)

3. Run the autostart script:
```bash
./autostart.sh
```

Or manually:
```bash
sudo cp dtek-schedule.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable dtek-schedule
sudo systemctl start dtek-schedule
```

### Manage the Service

```bash
# Check status
sudo systemctl status dtek-schedule

# View logs
sudo journalctl -u dtek-schedule -f

# Restart
sudo systemctl restart dtek-schedule

# Stop
sudo systemctl stop dtek-schedule

# Disable autostart
sudo systemctl disable dtek-schedule
```

## Option 3: Run with Docker (Advanced)

### Create Dockerfile

```dockerfile
FROM python:3.11-slim

# Install Chrome and dependencies
RUN apt-get update && apt-get install -y \
    chromium \
    chromium-driver \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY server.py .

ENV API_PASSWORD=dtek2024

EXPOSE 5000

CMD ["gunicorn", "-w", "2", "-b", "0.0.0.0:5000", "--timeout", "120", "server:app"]
```

### Build and Run

```bash
docker build -t dtek-schedule .
docker run -d -p 5000:5000 -e API_PASSWORD=your_password dtek-schedule
```

## Option 4: Nginx Reverse Proxy (Recommended for Internet Exposure)

If you want to expose the API to the internet, use Nginx as a reverse proxy:

### Install Nginx

```bash
sudo apt install nginx
```

### Configure Nginx

Create `/etc/nginx/sites-available/dtek-schedule`:

```nginx
server {
    listen 80;
    server_name your-domain.com;  # or your IP

    location / {
        proxy_pass http://127.0.0.1:5000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_connect_timeout 120s;
        proxy_send_timeout 120s;
        proxy_read_timeout 120s;
    }
}
```

Enable and restart:

```bash
sudo ln -s /etc/nginx/sites-available/dtek-schedule /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl restart nginx
```

### Add SSL with Let's Encrypt (Optional)

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d your-domain.com
```

## Performance Tips

### 1. Increase Cache TTL

Edit `server.py` and increase cache time:

```python
cache = {
    'data': None,
    'timestamp': 0,
    'ttl': 600  # 10 minutes instead of 5
}
```

### 2. Use Redis for Caching (Advanced)

For multiple workers, use Redis instead of in-memory cache:

```bash
pip install redis
```

```python
import redis
r = redis.Redis(host='localhost', port=6379, db=0)
```

### 3. Limit Worker Count

Don't run too many workers - Selenium is memory-intensive:
- 2-4 workers is usually optimal
- Monitor memory usage: `htop` or `free -h`

### 4. Set Up Log Rotation

Create `/etc/logrotate.d/dtek-schedule`:

```
/var/log/dtek-schedule/*.log {
    daily
    rotate 7
    compress
    delaycompress
    notifempty
    create 0640 griwsl griwsl
    sharedscripts
    postrotate
        systemctl reload dtek-schedule
    endscript
}
```

## Security Recommendations

1. **Change the default password**:
   ```bash
   export API_PASSWORD="$(openssl rand -base64 32)"
   ```

2. **Use HTTPS** (with Nginx + Let's Encrypt)

3. **Firewall rules**:
   ```bash
   sudo ufw allow 5000/tcp  # If exposing directly
   sudo ufw allow 80/tcp    # If using Nginx
   sudo ufw allow 443/tcp   # If using Nginx with SSL
   ```

4. **Rate limiting** (with Nginx):
   ```nginx
   limit_req_zone $binary_remote_addr zone=api:10m rate=10r/m;
   
   location / {
       limit_req zone=api burst=5;
       proxy_pass http://127.0.0.1:5000;
   }
   ```

## Monitoring

### Check if service is running

```bash
curl http://localhost:5000/health
```

### Monitor logs

```bash
# Systemd service
sudo journalctl -u dtek-schedule -f

# Gunicorn direct
tail -f gunicorn.log
```

### Resource usage

```bash
htop
# Look for python/gunicorn processes
```

## Troubleshooting

### Chrome/Chromium not found

```bash
# Install Chrome
sudo pacman -S chromium  # Arch
sudo apt install chromium-browser  # Ubuntu/Debian
```

### Port already in use

```bash
# Find what's using port 5000
sudo lsof -i :5000

# Kill it or change port in run_prod.sh
```

### Workers timing out

Increase timeout in `run_prod.sh`:
```bash
gunicorn -w 2 -b 0.0.0.0:5000 --timeout 180 server:app
```

### Out of memory

Reduce worker count or increase system RAM:
```bash
gunicorn -w 1 -b 0.0.0.0:5000 --timeout 120 server:app
```

