@echo off
rem =====================================================================
rem  Jalankan suite uji lib/HeatboxCycle di PC memakai MSVC (tanpa gcc).
rem  Dari root proyek:   test\run_msvc.bat
rem  Hasil build di .pio\msvc (sudah di-gitignore). Exit code 1 kalau ada
rem  yang gagal.
rem =====================================================================
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%P in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%P"
)
if not defined VSPATH set "VSPATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" (
    echo MSVC tidak ditemukan. Pasang "Desktop development with C++" / Build Tools.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

set "UNITY=.pio\libdeps\native\Unity\src"
if not exist "%UNITY%\unity.c" (
    echo Unity tidak ditemukan di %UNITY%. Jalankan sekali: pio pkg install -e native
    exit /b 1
)

set "OUT=.pio\msvc"
if not exist "%OUT%" mkdir "%OUT%"
set FAILED=0

for %%S in (test_drying_math test_cycle_aggregation) do (
    echo === %%S
    cl /nologo /std:c++14 /W4 /WX /EHsc /D_CRT_SECURE_NO_WARNINGS ^
       /I lib\HeatboxCycle\src /I "%UNITY%" ^
       test\%%S\%%S.cpp lib\HeatboxCycle\src\*.cpp "%UNITY%\unity.c" ^
       /Fo"%OUT%\\" /Fe"%OUT%\%%S.exe" > "%OUT%\%%S.build.log"
    if errorlevel 1 (
        type "%OUT%\%%S.build.log"
        set FAILED=1
    ) else (
        "%OUT%\%%S.exe" || set FAILED=1
    )
)

if %FAILED%==0 (echo SEMUA SUITE LOLOS) else (echo ADA SUITE YANG GAGAL)
exit /b %FAILED%
