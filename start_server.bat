@echo off
setlocal
rem ==========================================================================
rem  escape_from_blockov 서버 실행
rem    start_server.bat           게임 서버 + WS 게이트웨이 + WebGL 웹서버(8090, 게임 WS /ws 중계) 실행
rem    start_server.bat web       + 브라우저로 http://localhost:8090/ 열기
rem                               외부 접속: 공유기 TCP 8090 포트포워딩 + 방화벽 허용 (GAME_SPEC.md 18.4)
rem    start_server.bat build     서버를 Release x64로 다시 빌드한 뒤 실행
rem    (web, build 는 함께 쓸 수 있음)   종료: stop_server.bat
rem  필요: Node.js 18+, (빌드 시) Visual Studio 2022
rem ==========================================================================
set "ROOT=%~dp0"
set "SERVER_DIR=%ROOT%server\GameServer"
set "SERVER_EXE=%SERVER_DIR%\x64\Release\GameServer.exe"
set "GATEWAY_DIR=%ROOT%server\gateway"
set "CLIENT_DIR=%ROOT%client\escape_from_blockov"

set DO_BUILD=0
set DO_WEB=0
for %%A in (%*) do (
  if /I "%%A"=="build" set DO_BUILD=1
  if /I "%%A"=="web" set DO_WEB=1
)

where node >nul 2>nul
if errorlevel 1 (
  echo [오류] Node.js가 필요합니다: https://nodejs.org
  pause
  exit /b 1
)

if not exist "%SERVER_EXE%" set DO_BUILD=1
if "%DO_BUILD%"=="1" (
  call :build
  if errorlevel 1 (
    echo [오류] 서버 빌드 실패
    pause
    exit /b 1
  )
)

if not exist "%ROOT%map\obstacles.bmp" echo [경고] map\obstacles.bmp 가 없습니다. 엄폐물 없이 실행됩니다.

rem 게이트웨이는 이 PC에서만 받고(웹서버 /ws 가 중계), 웹서버가 넘긴 실제 접속 IP를 사용
set "GW_HOST=127.0.0.1"
set "TRUST_XFF=1"

start "Blockov GameServer" /D "%SERVER_DIR%" "%SERVER_EXE%"
start "Blockov Gateway" /D "%GATEWAY_DIR%" cmd /k node index.js

if not exist "%CLIENT_DIR%\Builds\WebGL\index.html" echo [경고] WebGL 빌드가 없습니다: client\escape_from_blockov\Builds\WebGL  (Unity에서 WebGL 빌드 후 사용)
start "Blockov WebGL" /D "%CLIENT_DIR%" cmd /k node Tools\serve_webgl.js 8090

if "%DO_WEB%"=="1" (
  timeout /t 2 /nobreak >nul
  start "" "http://localhost:8090/"
)

echo.
echo  GameServer : TCP 10301   (server\GameServer\game_config.txt)
echo  Gateway    : ws://127.0.0.1:8080/   (이 PC 전용, Unity 에디터 접속용)
echo  WebGL      : http://localhost:8090/   (LAN/외부 주소는 Blockov WebGL 창 참고)
echo  종료       : stop_server.bat
exit /b 0

:build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [오류] Visual Studio 2022가 설치되어 있지 않습니다.
  exit /b 1
)
set "MSBUILD="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
  echo [오류] MSBuild를 찾을 수 없습니다.
  exit /b 1
)
echo 서버 빌드 중 (Release x64)...
"%MSBUILD%" "%SERVER_DIR%\GameServer.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
exit /b %errorlevel%
