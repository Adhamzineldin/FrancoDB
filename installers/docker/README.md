# FrancoDB Docker Deployment

This folder contains Docker configuration for FrancoDB containerized deployment.

## 📦 Contents

- **Dockerfile** - Multi-stage Docker build configuration
- **docker-compose.yml** - Docker Compose orchestration
- **README.md** - This file

## 🔧 Prerequisites

- Docker 20.10+ ([Install Docker](https://docs.docker.com/get-docker/))
- Docker Compose 1.29+ ([Install Compose](https://docs.docker.com/compose/install/))
- 2 GB RAM
- 1 GB disk space

## 🚀 Quick Start

### Option 1: Docker Compose (Recommended)

```bash
cd /path/to/FrancoDB/installers/docker

# Start in detached mode
docker-compose up -d

# View logs
docker-compose logs -f francodb

# Stop container
docker-compose down
```

> **Note**: Docker context is set to project root (`../..`) in `docker-compose.yml`

### Option 2: Docker CLI

```bash
# Build image
docker build -t francodb:latest .

# Run container
docker run -d \
  --name francodb-server \
  -p 2501:2501 \
  -v francodb_data:/opt/francodb/data \
  -v francodb_logs:/opt/francodb/log \
  francodb:latest
```

## 🏗️ Building the Image

### Standard Build

```bash
cd /path/to/FrancoDB/installers/docker

# Build with correct context (project root)
docker build -f Dockerfile -t francodb:latest ../..
```

Or use docker-compose:
```bash
docker-compose build
```

### Build with Custom Tag

```bash
docker build -t francodb:1.0.0 .
docker build -t francodb:stable .
```

### Build with Build Args

```bash
docker build \
  --build-arg BUILD_TYPE=Release \
  --build-arg CMAKE_FLAGS="-DENABLE_TESTS=OFF" \
  -t francodb:latest .
```

## 📋 Image Features

✅ **Multi-stage Build**
  - Builder stage: Compiles from source
  - Runtime stage: Minimal production image
  - Final size: ~200 MB

✅ **Security**
  - Runs as non-root user (`francodb`)
  - No unnecessary packages
  - Minimal attack surface

✅ **Production Ready**
  - Health checks included
  - Proper signal handling
  - Logging to stdout/stderr
  - Graceful shutdown

## ⚙️ Configuration

### Environment Variables

Configure via `docker-compose.yml` or `-e` flags:

```yaml
environment:
  FRANCODB_PORT: "2501"
  FRANCODB_BIND_ADDRESS: "0.0.0.0"
  FRANCODB_LOG_LEVEL: "INFO"
  FRANCODB_BUFFER_POOL_SIZE: "1024"
  FRANCODB_AUTOSAVE_INTERVAL: "30"
```

### Volume Mounts

Persistent storage for data and logs:

```yaml
volumes:
  # Data directory
  - francodb_data:/opt/francodb/data
  # Logs directory  
  - francodb_logs:/opt/francodb/log
  # Config file (optional)
  - ./francodb.conf:/opt/francodb/etc/francodb.conf:ro
```

### Port Mapping

```yaml
ports:
  - "2501:2501"  # host:container
```

## 🔍 Container Management

### Start/Stop

```bash
# Start
docker-compose start

# Stop gracefully
docker-compose stop

# Force stop
docker-compose kill

# Restart
docker-compose restart
```

### View Status

```bash
# List containers
docker-compose ps

# View logs
docker-compose logs -f

# Check health
docker-compose exec francodb curl http://localhost:2501/health
```

### Shell Access

```bash
# Interactive shell
docker-compose exec francodb /bin/bash

# Run FrancoDB shell
docker-compose exec francodb francodb
```

## 📊 Resource Limits

Configure in `docker-compose.yml`:

```yaml
deploy:
  resources:
    limits:
      cpus: '2'      # Max 2 CPU cores
      memory: 2G     # Max 2GB RAM
    reservations:
      cpus: '1'      # Reserve 1 core
      memory: 512M   # Reserve 512MB
```

## 🔐 Security Configuration

### Run as Non-Root

Already configured in Dockerfile:
```dockerfile
USER francodb
```

### Security Options

```yaml
security_opt:
  - no-new-privileges:true
```

### Read-Only Filesystem (Optional)

```yaml
read_only: true
tmpfs:
  - /tmp
  - /run
```

## 💾 Data Persistence

### Backup Data

```bash
# Backup to tar.gz
docker run --rm \
  -v francodb_data:/data \
  -v $(pwd):/backup \
  alpine tar czf /backup/francodb_backup_$(date +%Y%m%d).tar.gz -C /data .
```

### Restore Data

```bash
# Stop container
docker-compose down

# Restore from backup
docker run --rm \
  -v francodb_data:/data \
  -v $(pwd):/backup \
  alpine sh -c "cd /data && tar xzf /backup/francodb_backup_20260119.tar.gz"

# Start container
docker-compose up -d
```

### Export Volume

```bash
docker run --rm \
  -v francodb_data:/data \
  alpine tar c -C /data . > francodb_data.tar
```

## 🔧 Troubleshooting

### Container Won't Start

```bash
# Check logs
docker-compose logs francodb

# Check container status
docker-compose ps

# Inspect container
docker inspect francodb-server
```

### Permission Issues

```bash
# Fix volume permissions
docker-compose exec francodb chown -R francodb:francodb /opt/francodb/data
```

### Port Already in Use

```bash
# Find process using port
netstat -tulpn | grep 2501

# Change port in docker-compose.yml
ports:
  - "2502:2501"  # Use different host port
```

### Health Check Failing

```bash
# Check health endpoint
docker-compose exec francodb curl -v http://localhost:2501/health

# View health status
docker inspect francodb-server | grep -A 10 Health
```

## 📈 Monitoring

### View Resource Usage

```bash
# Real-time stats
docker stats francodb-server

# Container details
docker-compose top
```

### Logging Configuration

Configure logging driver in `docker-compose.yml`:

```yaml
logging:
  driver: "json-file"
  options:
    max-size: "10m"
    max-file: "3"
```

### Export Logs

```bash
# Export all logs
docker-compose logs > francodb_logs.txt

# Export with timestamps
docker-compose logs -t > francodb_logs_timestamped.txt
```

## 🌐 Networking

### Connect Multiple Containers

```yaml
services:
  francodb:
    networks:
      - app-network
  
  app:
    networks:
      - app-network
    depends_on:
      - francodb

networks:
  app-network:
    driver: bridge
```

### Expose to External Network

```yaml
ports:
  - "0.0.0.0:2501:2501"  # All interfaces
  - "127.0.0.1:2501:2501"  # Localhost only
```

## 🔄 Updates and Maintenance

### Update to Latest Version

```bash
# Pull latest code
cd /path/to/FrancoDB
git pull

# Rebuild image
docker-compose build --no-cache

# Restart with new image
docker-compose up -d
```

### Clean Up

```bash
# Remove unused images
docker image prune

# Remove stopped containers
docker container prune

# Remove unused volumes (CAREFUL!)
docker volume prune
```

## 🚢 Deployment

### Production Deployment

```yaml
version: '3.8'

services:
  francodb:
    image: francodb:1.0.0
    restart: always
    environment:
      - FRANCODB_PORT=2501
    volumes:
      - /var/lib/francodb/data:/opt/francodb/data
      - /var/log/francodb:/opt/francodb/log
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:2501/health"]
      interval: 30s
      timeout: 5s
      retries: 3
    logging:
      driver: "syslog"
      options:
        tag: "francodb"
```

### Docker Swarm

```bash
# Initialize swarm
docker swarm init

# Deploy stack
docker stack deploy -c docker-compose.yml francodb

# Scale service
docker service scale francodb_francodb=3
```

### Kubernetes (k8s)

Convert compose file to Kubernetes:
```bash
kompose convert -f docker-compose.yml
kubectl apply -f francodb-deployment.yaml
```

## 📚 Additional Resources

- [Dockerfile Best Practices](https://docs.docker.com/develop/develop-images/dockerfile_best-practices/)
- [Docker Compose Reference](https://docs.docker.com/compose/compose-file/)
- [Docker Security](https://docs.docker.com/engine/security/)

## 💡 Tips

1. **Use .dockerignore**: Exclude unnecessary files from build context
2. **Layer Caching**: Order Dockerfile commands from least to most frequently changing
3. **Health Checks**: Always include health checks for production
4. **Resource Limits**: Set appropriate CPU and memory limits
5. **Logging**: Use proper logging driver for production

## 🆘 Common Commands

```bash
# View all containers
docker ps -a

# View all images
docker images

# Remove container
docker rm francodb-server

# Remove image
docker rmi francodb:latest

# View container logs
docker logs -f francodb-server

# Execute command in container
docker exec -it francodb-server bash
```

---

**Last Updated**: January 19, 2026

**Quick Links**:
- [Installation Guide](../../INSTALLATION_GUIDE.md)
- [Project README](../../README.md)
- [Docker Hub](https://hub.docker.com/)

