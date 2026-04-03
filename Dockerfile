# Lemoniscate - Hotline Server
# Multi-stage build for Linux deployment

# --- Build stage ---
FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libssl-dev \
    libyaml-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN make clean && make lemoniscate

# --- Runtime stage ---
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libyaml-0-2 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/lemoniscate /usr/local/bin/lemoniscate

# Config and file storage mount point
VOLUME /data

# Hotline protocol + file transfer + TLS variants
EXPOSE 5500 5501 5600 5601

ENTRYPOINT ["lemoniscate"]
CMD ["-c", "/data/config"]
