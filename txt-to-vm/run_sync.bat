@echo off

setlocal

:: Set paths
set LOGFILE=C:\Users\instr_data_frontop\Desktop\geosonic-data\logs\vm_ingest.log
set SCRIPT_DIR=%~dp0

if not exist "C:\Users\instr_data_frontop\Desktop\geosonic-data\logs" mkdir "C:\Users\instr_data_frontop\Desktop\geosonic-data\logs"

echo Starting vibration VM ingest server at %date% %time% >> %LOGFILE%

cd /d "%SCRIPT_DIR%"
python "%SCRIPT_DIR%main.py" >> %LOGFILE% 2>&1

echo Stopped vibration VM ingest server at %date% %time% >> %LOGFILE%