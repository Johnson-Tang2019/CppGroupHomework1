param(
    [string]$OutputDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "dist"),
    [string]$InstallName = "RemoteSensingQtStarter",
    [switch]$KeepTemp
)

$ErrorActionPreference = "Stop"

$vsVars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$iexpress = Join-Path $env:WINDIR "system32\iexpress.exe"
$qtDir = "C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6"
$opencvDir = "C:/Program Files/opencv/build/x64/vc16/lib"
$vcpkgBin = "C:\vcpkg\installed\x64-windows\bin"

function Assert-File {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
}

function Invoke-Checked {
    param([Parameter(Mandatory = $true)][string]$Command)
    cmd.exe /c $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Command"
    }
}

function Compress-ArchiveWithRetry {
    param(
        [Parameter(Mandatory = $true)][string[]]$Path,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [int]$MaxAttempts = 5
    )

    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        try {
            Compress-Archive -Path $Path -DestinationPath $DestinationPath -CompressionLevel Optimal
            return
        }
        catch {
            if ($attempt -eq $MaxAttempts) {
                throw
            }

            Start-Sleep -Seconds 2
        }
    }
}

Assert-File $vsVars
Assert-File $cmake
Assert-File $iexpress

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$tempRoot = Join-Path $env:TEMP "rsqt-package-$stamp"
$srcDir = Join-Path $tempRoot "src"
$buildDir = Join-Path $tempRoot "build"
$installerSrc = Join-Path $tempRoot "installer-src"
$tempInstaller = Join-Path $tempRoot "${InstallName}-Setup.exe"
$sedPath = Join-Path $tempRoot "installer.sed"

New-Item -ItemType Directory -Force -Path $tempRoot, $OutputDir | Out-Null

try {
    Write-Host "Copying source to ASCII temp path: $srcDir"
    Copy-Item -LiteralPath $PSScriptRoot -Destination $srcDir -Recurse

    Write-Host "Configuring Release build..."
    Invoke-Checked "call `"$vsVars`" && `"$cmake`" -S `"$srcDir`" -B `"$buildDir`" -G `"NMake Makefiles`" -DCMAKE_BUILD_TYPE=Release -DQt6_DIR=`"$qtDir`" -DOpenCV_DIR=`"$opencvDir`""

    Write-Host "Building Release executable and deploying Qt runtime..."
    Invoke-Checked "call `"$vsVars`" && `"$cmake`" --build `"$buildDir`" --config Release --target RemoteSensingQtStarter"

    $stage = Join-Path $OutputDir $InstallName
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    Write-Host "Staging runtime files..."
    $files = @(
        "RemoteSensingQtStarter.exe",
        "Qt6Concurrent.dll",
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Network.dll",
        "Qt6OpenGL.dll",
        "Qt6OpenGLWidgets.dll",
        "Qt6Svg.dll",
        "Qt6Widgets.dll",
        "opencv_world4120.dll",
        "opengl32sw.dll",
        "d3dcompiler_47.dll",
        "dxcompiler.dll",
        "dxil.dll",
        "icuuc.dll",
        "vc_redist.x64.exe",
        "app.ico"
    )

    foreach ($file in $files) {
        $source = Join-Path $buildDir $file
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination $stage -Force
        }
    }

    if (Test-Path -LiteralPath $vcpkgBin) {
        Write-Host "Copying vcpkg runtime DLLs, including GDAL dependencies..."
        Get-ChildItem -LiteralPath $vcpkgBin -Filter "*.dll" -File |
            Copy-Item -Destination $stage -Force
    }

    foreach ($dir in @("generic", "iconengines", "imageformats", "networkinformation", "platforms", "styles", "tls")) {
        $source = Join-Path $buildDir $dir
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination $stage -Recurse -Force
        }
    }

    $payload = Join-Path $OutputDir "${InstallName}-payload.zip"
    if (Test-Path -LiteralPath $payload) {
        Remove-Item -LiteralPath $payload -Force
    }

    Write-Host "Creating payload zip..."
    $items = Get-ChildItem -LiteralPath $stage -Force
    Compress-ArchiveWithRetry -Path $items.FullName -DestinationPath $payload

    New-Item -ItemType Directory -Force -Path $installerSrc | Out-Null
    Copy-Item -LiteralPath $payload -Destination (Join-Path $installerSrc "AppPayload.zip") -Force

    @'
@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
exit /b %ERRORLEVEL%
'@ | Set-Content -LiteralPath (Join-Path $installerSrc "install.cmd") -Encoding ASCII

    @'
$ErrorActionPreference = "Stop"
$installDir = Join-Path $env:LOCALAPPDATA "RemoteSensingQtStarter"
$payload = Join-Path $PSScriptRoot "AppPayload.zip"
if (!(Test-Path -LiteralPath $payload)) { throw "Missing AppPayload.zip" }
if (Test-Path -LiteralPath $installDir) { Remove-Item -LiteralPath $installDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $installDir | Out-Null
Expand-Archive -LiteralPath $payload -DestinationPath $installDir -Force
$exe = Join-Path $installDir "RemoteSensingQtStarter.exe"
$icon = Join-Path $installDir "app.ico"
$wsh = New-Object -ComObject WScript.Shell
$desktopShortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "RemoteSensingQtStarter.lnk"
$shortcut = $wsh.CreateShortcut($desktopShortcut)
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = $installDir
if (Test-Path -LiteralPath $icon) { $shortcut.IconLocation = $icon }
$shortcut.Save()
$startDir = Join-Path ([Environment]::GetFolderPath("Programs")) "RemoteSensingQtStarter"
New-Item -ItemType Directory -Force -Path $startDir | Out-Null
$startShortcut = Join-Path $startDir "RemoteSensingQtStarter.lnk"
$shortcut = $wsh.CreateShortcut($startShortcut)
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = $installDir
if (Test-Path -LiteralPath $icon) { $shortcut.IconLocation = $icon }
$shortcut.Save()
$redist = Join-Path $installDir "vc_redist.x64.exe"
if (Test-Path -LiteralPath $redist) {
    try { Start-Process -FilePath $redist -ArgumentList "/install", "/quiet", "/norestart" -Wait -WindowStyle Hidden } catch {}
}
Write-Host "Installed RemoteSensingQtStarter to $installDir"
'@ | Set-Content -LiteralPath (Join-Path $installerSrc "install.ps1") -Encoding UTF8

    @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=RemoteSensingQtStarter installed successfully.
TargetName=$tempInstaller
FriendlyName=RemoteSensingQtStarter Installer
AppLaunched=install.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=install.cmd
UserQuietInstCmd=install.cmd
FILE0="install.cmd"
FILE1="install.ps1"
FILE2="AppPayload.zip"
[SourceFiles]
SourceFiles0=$installerSrc\
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
"@ | Set-Content -LiteralPath $sedPath -Encoding ASCII

    Write-Host "Creating installer..."
    & $iexpress /N /Q $sedPath
    if ($LASTEXITCODE -ne 0) {
        throw "IExpress failed with exit code $LASTEXITCODE"
    }

    $deadline = (Get-Date).AddSeconds(60)
    while (!(Test-Path -LiteralPath $tempInstaller) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }

    if (!(Test-Path -LiteralPath $tempInstaller)) {
        throw "Installer was not created: $tempInstaller"
    }

    $installer = Join-Path $OutputDir "${InstallName}-Setup.exe"
    Copy-Item -LiteralPath $tempInstaller -Destination $installer -Force

    $hash = Get-FileHash -LiteralPath $installer -Algorithm SHA256
    Write-Host ""
    Write-Host "Done."
    Write-Host "Installer: $installer"
    Write-Host "Portable app: $stage"
    Write-Host "SHA256: $($hash.Hash)"
}
finally {
    if (!$KeepTemp -and (Test-Path -LiteralPath $tempRoot)) {
        try {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
        catch {
            Write-Warning "Could not remove temporary directory: $tempRoot"
            Write-Warning $_.Exception.Message
        }
    }
}
