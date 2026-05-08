@echo off

:: Set paths
set LOGFILE=C:\Users\instr_data_frontop\Desktop\geosonic-data\logs\process_data.log
set MARKERFILE=C:\Users\instr_data_frontop\Desktop\geosonic-data\logs\last_reset.txt

:: Get the current month and year
for /f "tokens=2 delims==" %%I in ('wmic os get LocalDateTime /value ^| find "LocalDateTime"') do set DATETIME=%%I
set CURRENT_MONTH=%DATETIME:~4,2%
set CURRENT_YEAR=%DATETIME:~0,4%

:: Check if the log was already reset this month
if exist "%MARKERFILE%" (
    for /f %%M in (%MARKERFILE%) do set LAST_RESET=%%M
) else (
    set LAST_RESET=0
)

if not "%LAST_RESET%"=="%CURRENT_YEAR%%CURRENT_MONTH%" (
    echo Resetting log file for new month >> %LOGFILE%
    echo. > %LOGFILE%
    echo %CURRENT_YEAR%%CURRENT_MONTH% > %MARKERFILE%
)

:: Echo the start time to the log file
echo Starting vibration data processing at %date% %time% >> %LOGFILE%

:: Run the Python script and append output to the log file
python C:\Users\instr_data_frontop\Desktop\geosonic-data\code\main.py >> %LOGFILE% 2>&1

:: Echo the finish time to the log file
echo Finished processing at %date% %time% >> %LOGFILE%