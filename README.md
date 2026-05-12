# frontop
Code for Frontop Engineering

How to build .exe installer:
1. Install required packages in requirements.txt ( install with pip install -r requirements.txt)
2. Compile build.py
3. .exe will appear in the 'dist' folder

Realtime VM ingest flow:
1. Set `vm_endpoint` in the vibration monitor config to the Windows VM URL, for example `http://<vm-ip>:8080/ingest`.
2. Run `txt-to-vm/main.py` on the Windows VM to accept POSTed events and append them to a JSONL log.
3. The vibration monitor now posts each parsed event immediately instead of waiting for the old file sync flow.

