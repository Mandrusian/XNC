$ErrorActionPreference = "Stop"

# Zero-admin, zero-UAC, zero-build-tools installer for Windows.
# Downloads prebuilt XNC-windows-x64.zip from GitHub Releases into %LOCALAPPDATA%\Programs\XNC.

$ReleaseZipUrl = if ($env:XNC_RELEASE_URL) {
    $env:XNC_RELEASE_URL
} else {
    "https://github.com/Mandrusian/XNC/releases/latest/download/XNC-windows-x64.zip"
}

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\XNC"
$TempZip = Join-Path ([System.IO.Path]::GetTempPath()) ("XNC-windows-x64-" + [Guid]::NewGuid().ToString("N") + ".zip")

try {
    Write-Host "Downloading prebuilt XNC from $ReleaseZipUrl..."
    Invoke-WebRequest -Uri $ReleaseZipUrl -OutFile $TempZip -UseBasicParsing

    if (Test-Path $InstallDir) {
        Remove-Item (Join-Path $InstallDir "shared") -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

    Expand-Archive -Path $TempZip -DestinationPath $InstallDir -Force

    $ExePath = Join-Path $InstallDir "xnc.exe"
    if (-not (Test-Path $ExePath)) {
        throw "Expected executable not found at $ExePath after extraction."
    }

    $StartMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\XNC.lnk"
    $WshShell = New-Object -ComObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut($StartMenu)
    $Shortcut.TargetPath = $ExePath
    $Shortcut.WorkingDirectory = $InstallDir
    $Shortcut.Description = "XNC (XNC is not Chrome)"
    $Shortcut.Save()

    $bin = Get-Item $ExePath
    Write-Host ""
    Write-Host "Installed XNC (zero-admin, zero-UAC) to: $($bin.FullName) ($([Math]::Round($bin.Length / 1KB, 1)) KB)"
    Write-Host "Start Menu shortcut: $StartMenu"
} finally {
    if (Test-Path $TempZip) {
        Remove-Item $TempZip -Force -ErrorAction SilentlyContinue
    }
}
