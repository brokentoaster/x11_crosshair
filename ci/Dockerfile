# CI image for x11_crosshair: toolchain + X11 dev packages only.
# Build: docker build -f ci/Dockerfile -t ghcr.io/plops/x11-crosshair-ci:1 .
FROM debian:12-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       g++ \
       cmake \
       make \
       pkg-config \
       git \
       ca-certificates \
       libx11-dev \
       libxfixes-dev \
       libxi-dev \
    && rm -rf /var/lib/apt/lists/*