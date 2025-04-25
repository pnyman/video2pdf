@echo off
echo === Kompilerar vip ===

gcc ^
 -o vip ^
 video2pdf.c ^
 lib/pdfgen.c

if %errorlevel% neq 0 (
    echo.
    echo !!! Kompileringen misslyckades !!!
    exit /b %errorlevel%
)

echo.
echo Kompileringen lyckades! Startar programmet...
run.bat
