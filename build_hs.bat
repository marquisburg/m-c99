@echo off
setlocal
cd /d "%~dp0"

rem The Haskell frontend. Built with `ghc --make` rather than cabal: every
rem dependency is a GHC boot library, so no package index is needed.
rem GHC comes from GHCup (https://www.haskell.org/ghcup/).

set GHC=ghc
where %GHC% >nul 2>nul
if errorlevel 1 set GHC=%USERPROFILE%\.ghcup\bin\ghc.exe
if not exist "%GHC%" if not "%GHC%"=="ghc" (
  echo GHC not found. Install it with ghcup, or put ghc on PATH.
  exit /b 1
)

if not exist bin mkdir bin
if not exist build\hs mkdir build\hs

echo Building bin\c99mtlc-hs.exe ...
%GHC% --make -O2 -Wall ^
  -ihs\src -ihs\app ^
  -outputdir build\hs ^
  -optc-Ilibmtlc\include ^
  hs\app\Main.hs hs\cbits\blob.c ^
  libmtlc\lib\mtlc.lib -ldbghelp ^
  -o bin\c99mtlc-hs.exe
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)
echo Built bin\c99mtlc-hs.exe
endlocal
