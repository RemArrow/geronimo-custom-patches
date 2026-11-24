@echo off
rem Builds the Custom Patches GML plugin and the patch array container it ships.
rem   build.bat            build into build\
rem   build.bat install    also copy it into the game (refuses while the game runs)
rem
rem Input: Assets\*.png - one picture per patch, any size (fitted into the patch's 468x664 area, aspect kept,
rem on black). The file name is the patch's name; patches are added in file-name order.
rem Steps: the game's PatchesTattoos slice 0 (the atlas around the patch) -> one slice per picture
rem (Utils\UAssetParser patchprep); the game's patch array package (extracted with retoc) -> the same package
rem with our slices appended (texarray add) -> a Zen container in build\Paks (retoc to-zen) -> the plugin,
rem with each picture built in. Needs the .NET 10 SDK, Visual Studio 2022 and the GML SDK 2.2+.
setlocal enabledelayedexpansion
set ROOT=%~dp0
set OUT=%ROOT%build
set TOOLS=%ROOT%..\Utils
set PARSER=%TOOLS%\UAssetParser\bin\Debug\net10.0\UAssetParser.dll
set RETOC=%TOOLS%\retoc\retoc.exe
set SDK=%ROOT%external\gml\include
if not exist "%SDK%\GML\GML.hpp" set SDK=%ROOT%..\ModLoader\include
if defined GML_SDK set SDK=%GML_SDK%
set GAME=C:\Program Files (x86)\Steam\steamapps\common\GERONIMO\Geronimo\Binaries\Win64
if defined GML_GAME_DIR set GAME=%GML_GAME_DIR%
set PAKS=%GAME%\..\..\Content\Paks
set ARRAY=%OUT%\legacy\Geronimo\Content\Characters\Common\PatchesTattoos.uasset
set BASE=%OUT%\base\Characters\Common\PatchesTattoos_slice00.png

if not exist "%PARSER%" (
  echo === UAssetParser
  dotnet build -v q "%TOOLS%\UAssetParser" || exit /b 1
)
for %%D in ("%OUT%\obj" "%OUT%\gen" "%OUT%\Paks" "%OUT%\Patches" "%OUT%\tests\PatchesDev") do if not exist "%%~D" mkdir "%%~D"
if not exist "%ARRAY%" (
  echo === the game's patch array
  "%RETOC%" to-legacy "%PAKS%" "%OUT%\legacy" --version UE5_7 -f "Content/Characters/Common/PatchesTattoos." || exit /b 1
)
if not exist "%BASE%" (
  echo === the game's patch slices, as pictures
  dotnet "%PARSER%" textures "%OUT%\base" "Geronimo/Content/Characters/Common/PatchesTattoos" || exit /b 1
)

echo === patch pictures -^> slices
del /q "%OUT%\gen\*" 2>nul
set SLICES=
set N=0
(echo // written by build.bat) > "%OUT%\gen\patches.rc"
for /f "delims=" %%F in ('dir /b /on "%ROOT%Assets\*.png"') do (
  dotnet "%PARSER%" patchprep "%BASE%" "%ROOT%Assets\%%F" 28 328 496 992 "%OUT%\gen" "%%~nF" || exit /b 1
  set SLICES=!SLICES! "%OUT%\gen\%%~nF_slice.png"
  >> "%OUT%\gen\patches.rc" echo PATCH!N! RCDATA "build\\gen\\%%~nF_icon.png"
  >> "%OUT%\gen\names.txt" echo %%~nF
  set /a N+=1
)
if %N%==0 (
  echo Put the patch pictures in Assets\*.png.
  exit /b 1
)
>> "%OUT%\gen\patches.rc" echo NAMES RCDATA "build\\gen\\names.txt"

echo === patch array + %N% slices
if exist "%OUT%\stage" rmdir /s /q "%OUT%\stage"
dotnet "%PARSER%" texarray add "%ARRAY%" PF_DXT1 39 "%OUT%\stage" %SLICES% || exit /b 1
del /q "%OUT%\Paks\*" 2>nul
"%RETOC%" to-zen --version UE5_7 "%OUT%\stage" "%OUT%\Paks\zzz_Patches_P.utoc" || exit /b 1

if not defined VCToolsInstallDir (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
echo === plugin
rem rc resolves the build\gen\ paths from the current directory: run it from this plugin's folder
pushd "%ROOT%" || exit /b 1
rc /nologo /I "%ROOT%." /fo"%OUT%\obj\Patches.res" "%ROOT%src\Patches.rc" || (popd & exit /b 1)
popd
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%" /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%src\Patches.cpp" "%OUT%\obj\Patches.res" /Fe"%OUT%\Patches\Patches.dll" /link /DEBUG /INCREMENTAL:NO || exit /b 1
echo === test harness (never installed)
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%" /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%tests\PatchesDev\PatchesDev.cpp" /Fe"%OUT%\tests\PatchesDev\PatchesDev.dll" /link /DEBUG /INCREMENTAL:NO || exit /b 1
for /r "%OUT%" %%F in (*.exp *.lib *.ilk) do del /q "%%F"

if /i "%1"=="install" (
  tasklist /FI "IMAGENAME eq Geronimo-Win64-Shipping.exe" | find /I "Geronimo-Win64" >nul && (
    echo Geronimo is running - close it first.
    exit /b 1
  )
  echo === install to "%GAME%\GML\plugins\Patches"
  if not exist "%GAME%\GML\plugins\Patches\Paks" mkdir "%GAME%\GML\plugins\Patches\Paks"
  copy /y "%OUT%\Patches\Patches.dll" "%GAME%\GML\plugins\Patches\" >nul || exit /b 1
  copy /y "%OUT%\Paks\*" "%GAME%\GML\plugins\Patches\Paks\" >nul || exit /b 1
)
echo === done
exit /b 0
