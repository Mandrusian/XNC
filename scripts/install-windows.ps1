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

    $running = Get-Process -Name "xnc" -ErrorAction SilentlyContinue
    if ($running) {
        Write-Host "Stopping running XNC process..."
        $running | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }

    $ExePath = Join-Path $InstallDir "xnc.exe"
    if (Test-Path $InstallDir) {
        if (Test-Path $ExePath) {
            Remove-Item $ExePath -Force -ErrorAction Stop
            if (Test-Path $ExePath) {
                throw "Failed to remove existing executable at $ExePath. Close XNC and try again."
            }
        }
        Remove-Item (Join-Path $InstallDir "shared") -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

    Expand-Archive -Path $TempZip -DestinationPath $InstallDir -Force

    if (-not (Test-Path $ExePath)) {
        throw "Expected executable not found at $ExePath after extraction."
    }

    $StartMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\XNC.lnk"
    $WshShell = New-Object -ComObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut($StartMenu)
    $Shortcut.TargetPath = $ExePath
    $Shortcut.WorkingDirectory = $InstallDir
    $Shortcut.IconLocation = "$ExePath,0"
    $Shortcut.Description = "XNC (XNC is not Chrome)"
    $Shortcut.Save()

    $DesktopLink = Join-Path ([Environment]::GetFolderPath("Desktop")) "XNC.lnk"
    if (Test-Path $DesktopLink) {
        $DesktopShortcut = $WshShell.CreateShortcut($DesktopLink)
        $DesktopShortcut.TargetPath = $ExePath
        $DesktopShortcut.WorkingDirectory = $InstallDir
        $DesktopShortcut.IconLocation = "$ExePath,0"
        $DesktopShortcut.Description = "XNC (XNC is not Chrome)"
        $DesktopShortcut.Save()
    }

    $bin = Get-Item $ExePath
    Write-Host ""
    Write-Host "Installed XNC (zero-admin, zero-UAC) to: $($bin.FullName) ($([Math]::Round($bin.Length / 1KB, 1)) KB)"
    Write-Host "Start Menu shortcut: $StartMenu"
} finally {
    if (Test-Path $TempZip) {
        Remove-Item $TempZip -Force -ErrorAction SilentlyContinue
    }
}
