# Geosonic SSU 3000LC Code

The following repoistory contains code for the Geosonic SSU 3000LC vibration monitor, including a Python script to parse the device's log files and post events to a Windows VM, as well as a Docker-based build system for creating an OpenWrt IPK package to run the monitor directly on compatible devices.

## Features
1. .exe installer for Windows VM deployment - allows for easy setup of the vibration monitor on a Windows machine
2. Realtime event posting to a Windows VM - eliminates the need for file syncing by posting events immediately as they are parsed
3. Docker-based build system for OpenWrt IPK - simplifies the process of building and installing the monitor on OpenWrt devices, with support for multiple target SDKs

How to build .exe installer:
1. Install required packages in requirements.txt ( install with pip install -r requirements.txt)
2. Compile build.py
3. .exe will appear in the 'dist' folder

Realtime VM ingest flow:
1. Set `vm_endpoint` in the vibration monitor config to the Windows VM URL, for example `http://<vm-ip>:8080/ingest`.
2. Run `txt-to-vm/main.py` on the Windows VM to accept POSTed events and append them to a JSONL log.
3. The vibration monitor now posts each parsed event immediately instead of waiting for the old file sync flow.

Build an IPK with Docker and Ubuntu:
1. Build the Ubuntu image: `docker build -t frontop-openwrt-build -f docker/Dockerfile docker`
2. Start a shell in the container and mount the repo: `docker run --rm -it -v "$(pwd)":/workspace -w /workspace frontop-openwrt-build bash`
3. Run the build helper inside the container with the correct OpenWrt SDK URL for your target: `bash ./docker/build-ipk.sh <OPENWRT_SDK_URL>`
4. The resulting `.ipk` is copied to `out/` in the repo root.
5. Install it on the OpenWrt device with `opkg install /tmp/vibration-monitor_*.ipk` after copying the file over.
6. For GL-XE300 running OpenWrt 19.07.8, start with the 19.07.8 `ar71xx/generic` SDK URL shown in `docker/build-ipk.sh`; if your device reports a different target family, swap in that matching SDK instead.

