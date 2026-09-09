@echo off
setlocal

set "HPVR_ROOT=%~dp0"
set "HPVR_EXE=%HPVR_ROOT%tools\xr-runtime-probe\target\release\wgpu_stereo_clear.exe"
set "HPVR_LOADER=C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll"
set "HPVR_DATA=C:\Program Files\HP"
set "HPVR_MAP=%HPVR_DATA%\Maps\Lev_Tut1.unr"

if not exist "%HPVR_EXE%" (
  echo ERROR: B20 executable is missing:
  echo %HPVR_EXE%
  pause
  exit /b 1
)
if not exist "%HPVR_LOADER%" (
  echo ERROR: OpenXR loader is missing:
  echo %HPVR_LOADER%
  pause
  exit /b 1
)
if not exist "%HPVR_MAP%" (
  echo ERROR: HP1 map is missing:
  echo %HPVR_MAP%
  pause
  exit /b 1
)

echo Harry Potter VR B20
echo Loading the owned map and characters, then OpenXR will take over automatically.
echo Camera uses Harry BaseEyeHeight 40.75 UU = 0.815 m.
echo Seven principal NPCs are staged near PlayerStart for this vertical slice.
echo Harry's capsule now follows floors and stairs and blocks solid BSP walls.
echo The real HPBase.WandMesh is loaded directly from your owned HP1 package.
echo Aim at an NPC first, press and hold to lock that target, draw Flipendo, then release.
echo The cyan aim ray hides while drawing; test assist allows 75 percent more path error.
echo A green beam means a hit and moves that NPC; orange means miss or BSP obstruction.
echo Close this console or press Ctrl+C when you are finished.
echo.

pushd "%HPVR_ROOT%"
"%HPVR_EXE%" ^
  --loader "%HPVR_LOADER%" ^
  --frames 540000 ^
  --flipendo-data-root "%HPVR_DATA%" ^
  --flipendo-test-assist ^
  --hp1-map-slice "%HPVR_MAP%" 0.02 100000 ^
  --hp1-player-start 0 ^
  --hp1-eye-height 0.815 ^
  --hp1-bsp-collision ^
  --hp1-character-population "%HPVR_DATA%" 603 ^
  --hp1-character-stage-near-player ^
  --hp1-character-animation ^
  --hp1-npc-spell-interaction
set "HPVR_EXIT=%ERRORLEVEL%"
popd

echo.
echo VR process exited with code %HPVR_EXIT%.
pause
exit /b %HPVR_EXIT%
