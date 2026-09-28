# Builds BOUNCE on Windows (VST3 + Standalone, x64), runs the test suite and zips dist/.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
#
# Needs: Visual Studio 2022 (Desktop C++), CMake >= 3.22, JUCE 9.0.0 at %USERPROFILE%\JUCE.
# The editor is a WebView2 page. JUCE 9 links WebView2 statically and finds the SDK in the
# NuGet package folder; this script downloads that package once if it is missing.
# The end user needs the Evergreen WebView2 Runtime (preinstalled on Windows 10/11).
#
# NOT YET RUN: this script was written on macOS and has never executed on Windows.

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$wv2Version = "1.0.3485.44"
$wv2Root = Join-Path $env:USERPROFILE "AppData\Local\PackageManagement\NuGet\Packages"
$wv2Dir = Join-Path $wv2Root "Microsoft.Web.WebView2.$wv2Version"
if (-not (Test-Path (Join-Path $wv2Dir "build\native\include\WebView2.h"))) {
    Write-Host "== downloading the WebView2 SDK $wv2Version"
    New-Item -ItemType Directory -Force -Path $wv2Dir | Out-Null
    $zip = Join-Path $env:TEMP "webview2.zip"
    Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$wv2Version" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $wv2Dir -Force
}

cmake -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_WEBVIEW2_PACKAGE_LOCATION="$wv2Root"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build build --config Release --target Bounce_VST3 Bounce_Standalone BounceTests --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "== tests"
& "build\BounceTests_artefacts\Release\BounceTests.exe"
if ($LASTEXITCODE -ne 0) { throw "Tests failed" }

$art = "build\Bounce_artefacts\Release"
$version = (Select-String -Path CMakeLists.txt -Pattern "^project\(Bounce VERSION ([0-9.]+)").Matches[0].Groups[1].Value
New-Item -ItemType Directory -Force -Path dist | Out-Null
$zipOut = "dist\BOUNCE-$version-Windows.zip"
if (Test-Path $zipOut) { Remove-Item $zipOut }
Compress-Archive -Path "$art\VST3\BOUNCE.vst3", "$art\Standalone\BOUNCE.exe" -DestinationPath $zipOut
Write-Host "== $zipOut  (install: copy BOUNCE.vst3 to C:\Program Files\Common Files\VST3)"
