# FrancoDB Server Dockerfile - Production Ready
# Location: installers/docker/Dockerfile

# ============================================================================
# Stage 1: Build Stage
# ============================================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    curl \
    wget \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY src ./src
COPY test ./test
COPY CMakeLists.txt .
COPY examples ./examples
COPY libaries ./libaries

# Build with testing DISABLED to skip Windows-only tests
RUN mkdir -p build && \
    cd build && \
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_TESTING=OFF \
          -DCMAKE_INSTALL_PREFIX=/opt/francodb .. && \
    cmake --build . --config Release -j$(nproc) && \
    cmake --install . && \
    cd / && \
    rm -rf /build

# ============================================================================
# Stage 2: Runtime Stage
# ============================================================================
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -r francodb && useradd -r -g francodb francodb

COPY --from=builder /opt/francodb /opt/francodb

RUN mkdir -p /opt/francodb/data /opt/francodb/log /opt/francodb/etc && \
    chown -R francodb:francodb /opt/francodb && \
    chmod 755 /opt/francodb && \
    chmod 700 /opt/francodb/data /opt/francodb/log

# --- ADDED: Generate Default Config ---
RUN echo "port = 2501" > /opt/francodb/etc/francodb.conf && \
    echo "bind_address = \"0.0.0.0\"" >> /opt/francodb/etc/francodb.conf && \
    echo "data_directory = \"/opt/francodb/data\"" >> /opt/francodb/etc/francodb.conf && \
    echo "log_directory = \"/opt/francodb/log\"" >> /opt/francodb/etc/francodb.conf && \
    chown francodb:francodb /opt/francodb/etc/francodb.conf
# ---------------------------------------

USER francodb
WORKDIR /opt/francodb
EXPOSE 2501

# REMOVED HEALTHCHECK to prevent protocol mismatch errors
# Re-enable if you implement an HTTP /health endpoint

ENTRYPOINT ["/opt/francodb/bin/francodb_server"]
CMD ["--config", "/opt/francodb/etc/francodb.conf"]