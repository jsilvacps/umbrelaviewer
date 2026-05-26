@echo off
setlocal

set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS ( echo ERRO: VS Build Tools nao encontrado. & pause & exit /b 1 )

set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set "B=%~dp0build"
if not exist "%B%" mkdir "%B%"

echo Configurando...
"%CMAKE%" -B "%B%" -S "%~dp0." -G "Ninja" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 ( echo ERRO na configuracao. & pause & exit /b 1 )

echo Compilando...
"%CMAKE%" --build "%B%" --config Release
if errorlevel 1 ( echo ERRO na compilacao. & pause & exit /b 1 )

if not exist "%~dp0dist" mkdir "%~dp0dist"
copy /y "%B%\UmbrelaViewer.exe" "%~dp0dist\" >nul

echo.
echo  EXE pronto: dist\UmbrelaViewer.exe

:: ── NSIS Installer (opcional — só compila se NSIS estiver instalado) ──────────
set "MAKENSIS="
for %%P in (
    "C:\Program Files (x86)\NSIS\makensis.exe"
    "C:\Program Files\NSIS\makensis.exe"
) do ( if exist %%P set "MAKENSIS=%%~P" )

:: Tenta no PATH também
if not defined MAKENSIS (
    for /f "delims=" %%i in ('where makensis 2^>nul') do set "MAKENSIS=%%i"
)

if defined MAKENSIS (
    echo.
    echo Compilando instalador...
    "%MAKENSIS%" /V2 "%~dp0installer\UmbrelaViewer.nsi"
    if errorlevel 1 (
        echo AVISO: falha ao compilar o instalador.
    ) else (
        echo  Instalador pronto: dist\UmbrelaViewer_Setup.exe
    )
) else (
    echo.
    echo  NSIS nao encontrado - instalador NAO foi gerado.
    echo  Para gerar o instalador, baixe o NSIS em: https://nsis.sourceforge.io/Download
)

echo.
echo =========================================
echo  CONCLUIDO
echo =========================================
pause
endlocal
