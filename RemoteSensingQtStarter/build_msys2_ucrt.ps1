$ErrorActionPreference = "Stop"

$ucrtBin = "C:\msys64\ucrt64\bin"
$buildDir = "C:\rsqt-starter-build"

function Invoke-Checked {
    param([Parameter(Mandatory = $true)][string]$FilePath, [string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

Get-Process RemoteSensingQtStarter -ErrorAction SilentlyContinue | Stop-Process -Force
$env:Path = "$ucrtBin;$env:Path"

Invoke-Checked "cmake" @("-S", $PSScriptRoot, "-B", $buildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
Invoke-Checked "cmake" @("--build", $buildDir)
Invoke-Checked "windeployqt" @("--release", "$buildDir\RemoteSensingQtStarter.exe")

foreach ($dll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $source = Join-Path $ucrtBin $dll
    if (Test-Path $source) {
        Copy-Item -Force $source $buildDir
    }
}

Write-Host "Build finished: $buildDir\RemoteSensingQtStarter.exe"
