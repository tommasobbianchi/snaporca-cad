@REM Snapmaker_Orca build script for Windows
@echo off
set WP=%CD%

@REM Pack deps
if "%1"=="pack" (
    setlocal ENABLEDELAYEDEXPANSION 
    cd %WP%/deps/build
    for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set build_date=%%c%%b%%a
    echo packing deps: OrcaSlicer_dep_win64_!build_date!_vs2022.zip

    %WP%/tools/7z.exe a OrcaSlicer_dep_win64_!build_date!_vs2022.zip OrcaSlicer_dep
    exit /b 0
)

set debug=OFF
set debuginfo=OFF
if "%1"=="debug" set debug=ON
if "%2"=="debug" set debug=ON
if "%1"=="debuginfo" set debuginfo=ON
if "%2"=="debuginfo" set debuginfo=ON
if "%debug%"=="ON" (
    set build_type=Debug
    set build_dir=build-dbg
) else (
    if "%debuginfo%"=="ON" (
        set build_type=RelWithDebInfo
        set build_dir=build-dbginfo
    ) else (
        set build_type=Release
        set build_dir=build
    )
)
echo build type set to %build_type%

@REM Detect the installed Visual Studio so the CMake generator matches the
@REM runner image (VS2022=17, VS2026=18). Hardcoding "Visual Studio 17 2022"
@REM broke once the GitHub windows-latest runner migrated to VS 18.
echo Detecting Visual Studio version using msbuild...
set VS_MAJOR=
for /f "tokens=*" %%i in ('msbuild -version 2^>^&1 ^| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*"') do (
    for /f "tokens=1 delims=." %%a in ("%%i") do set VS_MAJOR=%%a
    goto :vs_found
)
:vs_found
if "%VS_MAJOR%"=="16" (
    set CMAKE_GENERATOR="Visual Studio 16 2019"
) else if "%VS_MAJOR%"=="17" (
    set CMAKE_GENERATOR="Visual Studio 17 2022"
) else if "%VS_MAJOR%"=="18" (
    set CMAKE_GENERATOR="Visual Studio 18 2026"
) else (
    echo Error: unsupported or undetected Visual Studio version "%VS_MAJOR%"
    exit /b 1
)
echo Using CMake generator: %CMAKE_GENERATOR%

setlocal DISABLEDELAYEDEXPANSION
cd deps
mkdir %build_dir%
cd %build_dir%
set DEPS=%CD%/OrcaSlicer_dep
set "SIG_FLAG="
if defined ORCA_UPDATER_SIG_KEY set "SIG_FLAG=-DORCA_UPDATER_SIG_KEY=%ORCA_UPDATER_SIG_KEY%"

if "%1"=="slicer" (
    GOTO :slicer
)
echo "building deps.."

echo on
REM Set minimum CMake policy to avoid <3.5 errors (CMake 4.x removed pre-3.5 compat)
set CMAKE_POLICY_VERSION_MINIMUM=3.5
cmake ../ -G %CMAKE_GENERATOR% -A x64 -DDESTDIR="%DEPS%" -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo% || exit /b 1
cmake --build . --config %build_type% --target deps -- -m || exit /b 1
@echo off

if "%1"=="deps" exit /b 0

:slicer
echo "building Snapmaker Orca..."
cd %WP%
mkdir %build_dir%
cd %build_dir%

echo on
set CMAKE_POLICY_VERSION_MINIMUM=3.5
cmake .. -G %CMAKE_GENERATOR% -A x64 -DBBL_RELEASE_TO_PUBLIC=1 -DORCA_TOOLS=ON %SIG_FLAG% -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./Snapmaker_Orca" -DCMAKE_BUILD_TYPE=%build_type% -DWIN10SDK_PATH="%WindowsSdkDir%Include\%WindowsSDKVersion%\" || exit /b 1
cmake --build . --config %build_type% --target ALL_BUILD -- -m || exit /b 1
@echo off
cd ..
call scripts/run_gettext.bat
cd %build_dir%
cmake --build . --target install --config %build_type%
