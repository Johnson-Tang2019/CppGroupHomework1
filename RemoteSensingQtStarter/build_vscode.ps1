param(
    [switch]$Run
)

$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectDir "build_vs"
$ExePath = Join-Path $BuildDir "RemoteSensingQtStarter.exe"
$QtPrefix = "D:\Qt\6.11.1\msvc2022_64"
$QtBin = Join-Path $QtPrefix "bin"
$CMake = "D:\Qt\Tools\CMake_64\bin\cmake.exe"
$Ninja = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$VcVars = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
$WindeployQt = Join-Path $QtBin "windeployqt.exe"
$VcpkgToolchain = "D:\vcpkg\scripts\buildsystems\vcpkg.cmake"
$VcpkgTriplet = "x64-windows"

if (-not (Test-Path $CMake)) {
    $CMake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}

foreach ($RequiredPath in @($CMake, $Ninja, $VcVars, $WindeployQt)) {
    if (-not (Test-Path $RequiredPath)) {
        throw "Required build tool not found: $RequiredPath"
    }
}

$env:Path = "$QtBin;$env:Path"

$CachePath = Join-Path $BuildDir "CMakeCache.txt"
$UseVcpkgToolchain = $false
if ((Test-Path $VcpkgToolchain) -and (Test-Path $CachePath)) {
    $CacheText = Get-Content -Path $CachePath -Raw
    if ($CacheText -notmatch [regex]::Escape($VcpkgToolchain.Replace("\", "/")) -and
        $CacheText -notmatch [regex]::Escape($VcpkgToolchain)) {
        Remove-Item -LiteralPath $CachePath -Force
        $CMakeFiles = Join-Path $BuildDir "CMakeFiles"
        if (Test-Path $CMakeFiles) {
            Remove-Item -LiteralPath $CMakeFiles -Recurse -Force
        }
        $UseVcpkgToolchain = $true
    }
} elseif (Test-Path $VcpkgToolchain) {
    $UseVcpkgToolchain = $true
}

$Configure = '"' + $CMake + '" -S "' + $ProjectDir + '" -B "' + $BuildDir + '" -G Ninja -DCMAKE_MAKE_PROGRAM="' + $Ninja + '" -DCMAKE_PREFIX_PATH="' + $QtPrefix + '"'
if ($UseVcpkgToolchain) {
    $Configure += ' -DCMAKE_TOOLCHAIN_FILE="' + $VcpkgToolchain + '" -DVCPKG_TARGET_TRIPLET=' + $VcpkgTriplet
}
$Build = '"' + $CMake + '" --build "' + $BuildDir + '"'
$Deploy = '"' + $WindeployQt + '" --release "' + $ExePath + '"'
$Command = 'call "' + $VcVars + '" && ' + $Configure + ' && ' + $Build + ' && ' + $Deploy

cmd.exe /d /c $Command
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path $ExePath)) {
    throw "Build completed but executable was not found: $ExePath"
}

if ($Run) {
    Start-Process -FilePath $ExePath -WorkingDirectory $BuildDir
} else {
    Write-Host "Build finished: $ExePath"
}
