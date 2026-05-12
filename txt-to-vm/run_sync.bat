@echo off

setlocal

:: Set paths
set LOGFILE=C:\Users\instr_data_frontop\Desktop\geosonic-data\logs\vm_ingest.log
set SCRIPT_DIR=%~dp0

echo Starting vibration VM ingest server at %date% %time% >> %LOGFILE%

python "%SCRIPT_DIR%main.py" >> %LOGFILE% 2>&1

echo Stopped vibration VM ingest server at %date% %time% >> %LOGFILE%