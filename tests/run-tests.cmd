@echo off
setlocal
set "TEST_ARCH=%~1"
if "%TEST_ARCH%"=="" set "TEST_ARCH=x64"
set "TEST_PLATFORM=x64"
if "%TEST_ARCH%"=="x86" set "TEST_PLATFORM=Win32"
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TEST_VS=%%i"
call "%TEST_VS%\VC\Auxiliary\Build\vcvarsall.bat" %TEST_ARCH% >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if not exist "build\tests\%TEST_ARCH%" mkdir "build\tests\%TEST_ARCH%"
cl /nologo /std:c++17 /EHsc /W4 /utf-8 tests\ButtonStateTests.cpp /Fo"build\tests\%TEST_ARCH%\ButtonStateTests.obj" /Fe"build\tests\%TEST_ARCH%\ButtonStateTests.exe"
if errorlevel 1 exit /b 1
"build\tests\%TEST_ARCH%\ButtonStateTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE tests\PluginSmokeTests.cpp /Fo"build\tests\%TEST_ARCH%\PluginSmokeTests.obj" /Fe"build\tests\%TEST_ARCH%\PluginSmokeTests.exe" /link user32.lib
if errorlevel 1 exit /b 1
"build\tests\%TEST_ARCH%\PluginSmokeTests.exe" "bin\%TEST_PLATFORM%\Release\HIDRemocon.tvtp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE tests\HidParserTests.cpp /Fo"build\tests\%TEST_ARCH%\HidParserTests.obj" /Fe"build\tests\%TEST_ARCH%\HidParserTests.exe" /link user32.lib hid.lib
if errorlevel 1 exit /b 1
"build\tests\%TEST_ARCH%\HidParserTests.exe"
if errorlevel 1 exit /b 1
popd
endlocal
