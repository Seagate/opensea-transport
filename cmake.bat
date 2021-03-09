@echo off

set /p VS-Version= "Enter VS Version (For eg: VS-2017 or VS-2015):"
echo '%VS-Version%' 
if not exist "%VS-Version%" (mkdir "%VS-Version%")


if "%1" == "-x64" goto 64bit

if "%1" == "-x86" goto 32bit

if "%1" == "-arm" goto arm

if "%1" == "-arm64" goto arm64

:64bit
if not exist "%VS-Version%/x64" (goto handle64bit)
goto out

:32bit
if not exist "%VS-Version%/x86" (goto handle32bit)
goto out

:arm
if not exist "%VS-Version%/arm" (goto handlearm)
goto out

:arm64
if not exist "%VS-Version%/arm64" (goto handlearm64)
goto out

:handle64bit
mkdir "%VS-Version%\x64"
set pathname= %__CD__%%VS-Version%\x64
cd %pathname%
goto out

:handle32bit
mkdir "%VS-Version%\x86"
set pathname= %__CD__%%VS-Version%\x86
cd %pathname%
goto out

:handlearm
mkdir "%VS-Version%\arm"
set pathname= %__CD__%%VS-Version%\arm
cd %pathname%
goto out

:handlearm64
mkdir "%VS-Version%\arm64"
set pathname= %__CD__%%VS-Version%\arm64
cd %pathname%
goto out

:out



