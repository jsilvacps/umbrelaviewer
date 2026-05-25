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
"%CMAKE%" -B "%B%" -S "%~dp0" -G "Ninja" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 ( echo ERRO na configuracao. & pause & exit /b 1 )

echo Compilando...
"%CMAKE%" --build "%B%" --config Release
if errorlevel 1 ( echo ERRO na compilacao. & pause & exit /b 1 )

if not exist "%~dp0dist" mkdir "%~dp0dist"
copy /y "%B%\UmbrelaViewer.exe" "%~dp0dist\" >nul

echo.
echo =========================================
echo  CONCLUIDO: dist\UmbrelaViewer.exe
echo =========================================
pause
endlocal
