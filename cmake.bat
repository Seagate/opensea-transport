@echo off

set /p VS-Version= "Enter VS Version (For eg: VS-2017 or VS-2015):"
echo '%VS-Version%' 
if not exist "%VS-Version%" (mkdir "%VS-Version%")


if "%1" == "-x64" goto 64bit

if "%1" == "-x86" goto 32bit

if "%1" == "-arm" goto arm

if "%1" == "-arm64" goto arm64

:64bit
if not exist "%VS-Version%/x64" (mkdir "%VS-Version%/x64")
goto out

:32bit
if not exist "%VS-Version%/x86" (mkdir "%VS-Version%/x86")
goto out

:arm
if not exist "%VS-Version%/arm" (mkdir "%VS-Version%/arm")
goto out

:arm64
if not exist "%VS-Version%/arm64" (mkdir "%VS-Version%/arm64")
goto out

:out



