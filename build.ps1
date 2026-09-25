param(
    [switch]$debug,
    [switch]$clean
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$env:PATH = "$scriptDir\tools\w64devkit\bin;$scriptDir\tools\cmake\bin;" + $env:PATH

$debugFlag = "OFF"
$buildType = "Release"

if ($debug -or ($args -contains "--debug") -or ($args -contains "-d")) {
    $debugFlag = "ON"
    $buildType = "Debug"
    Write-Host "[BUILD] Configuring in DEBUG mode (--debug enabled)" -ForegroundColor Cyan
} else {
    Write-Host "[BUILD] Configuring in PRODUCTION mode (lean, silent, high-performance)" -ForegroundColor Green
    Write-Host "[BUILD] Pass '-debug' or '--debug' to build with telemetry, logging, and diagnostics." -ForegroundColor DarkGray
}

if ($clean) {
    if (Test-Path "$scriptDir\build_app") {
        Write-Host "[BUILD] Cleaning build_app directory..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force "$scriptDir\build_app"
    }
}

cmake -B "$scriptDir\build_app" -G Ninja "-DCMAKE_BUILD_TYPE=$buildType" "-DENABLE_DEBUG=$debugFlag"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build "$scriptDir\build_app"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "`n[BUILD] Build successful! Game executables deployed to game/`n" -ForegroundColor Green
