@echo off
rem escape_from_blockov 서버 종료 (start_server.bat 로 연 창들을 닫는다)
taskkill /F /T /FI "WINDOWTITLE eq Blockov*" >nul 2>nul
taskkill /F /IM GameServer.exe >nul 2>nul
echo 서버를 종료했습니다.
