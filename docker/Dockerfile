FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    ca-certificates \
    curl \
    file \
    gawk \
    gettext \
    git \
    libc6-dev \
    libncurses-dev \
    make \
    patch \
    python3 \
    rsync \
    tar \
    unzip \
    wget \
    xz-utils \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
