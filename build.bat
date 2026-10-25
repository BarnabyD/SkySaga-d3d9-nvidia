@echo off
rem Builds the 32-bit d3d9.dll proxy with the floating-point crash fix.
rem Needs Visual Studio 2022 with the C++ x86 tools.
rem copy d3d9.dll to the SkySaga/Client directory to apply the fix.
setlocal
set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat
if not exist "%VCVARS%" (
  echo vcvars32.bat not found at "%VCVARS%"
  exit /b 1
)
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /LD /O2 /MT /W3 d3d9_fpu.c /link /DEF:d3d9.def /OUT:d3d9.dll || exit /b 1
echo Built d3d9.dll
