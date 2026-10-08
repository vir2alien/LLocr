@echo off
setlocal EnableExtensions
rem Полный сброс настроек LLocr на Windows (Qt: org "llocr", app "LLM OCR").
rem Удаляет: QSettings (HKCU\Software\llocr*), корень рантайма с моделями,
rem кэши, данные WebEngine и следы тестовых org-идентификаторов.
rem
rem Использование: reset-settings-windows.bat [-y]

set ORG=llocr

tasklist /FI "IMAGENAME eq llocr.exe" 2>nul | find /I "llocr.exe" >nul
if not errorlevel 1 (
    echo Ошибка: LLocr запущен. Закройте приложение и повторите.
    exit /b 1
)

set ASSUME_YES=0
if /I "%~1"=="-y" set ASSUME_YES=1
if /I "%~1"=="--yes" set ASSUME_YES=1
if not "%~1"=="" if not "%ASSUME_YES%"=="1" (
    echo Использование: %~nx0 [-y^|--yes]
    exit /b 2
)

echo Будут удалены (настройки, кэши, скачанный рантайм и модели):
for %%K in ("%ORG%" "%ORG%_test" "%ORG%-test" "%ORG%-tests") do (
    reg query "HKCU\Software\%%~K" >nul 2>&1 && echo   HKCU\Software\%%~K
)
for /D %%D in ("%APPDATA%\%ORG%*") do echo   %%D
for /D %%D in ("%LOCALAPPDATA%\%ORG%*") do echo   %%D

if "%ASSUME_YES%"=="1" goto :do_reset

choice /C YN /N /M "Продолжить? [Y/N] "
if errorlevel 2 (
    echo Отменено.
    exit /b 1
)

:do_reset
for %%K in ("%ORG%" "%ORG%_test" "%ORG%-test" "%ORG%-tests") do (
    reg delete "HKCU\Software\%%~K" /f >nul 2>&1 && echo Удалено: HKCU\Software\%%~K
)

for %%D in ("%APPDATA%" "%LOCALAPPDATA%") do (
    for /D %%D2 in ("%%~D\%ORG%*") do (
        rmdir /S /Q "%%~D2"
        echo Удалено: %%~D2
    )
)

echo Готово: настройки LLocr полностью удалены.
endlocal
