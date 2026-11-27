$ErrorActionPreference = "Stop"

$RepoUrl = if ($env:XNC_REPO_URL) { $env:XNC_REPO_URL } else { "https://github.com/Mandrusian/XNC.git" }
$TempRoot = $null

function Resolve-RepoRoot {
    if ($PSScriptRoot) {
        $candidate = Split-Path -Parent $PSScriptRoot
        if (Test-Path (Join-Path $candidate "windows\CMakeLists.txt")) {
            return $candidate
        }
    }
    if (Test-Path "windows\CMakeLists.txt") {
        return (Get-Location).Path
    }
    return $null
}

try {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake is required. Install CMake and Visual Studio C++ Build Tools first."
    }

    $RepoRoot = Resolve-RepoRoot
    if ($RepoRoot) {
        Write-Host "Using local repository at $RepoRoot"
    } else {
        if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
            throw "git is required when running standalone."
        }
        $TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("xnc-install-" + [Guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null
        Write-Host "Cloning XNC from $RepoUrl..."
        git clone --depth 1 $RepoUrl (Join-Path $TempRoot "XNC") | Out-Null
        $RepoRoot = Join-Path $TempRoot "XNC"
    }

    $WebView2Dir = Join-Path $RepoRoot "windows\packages\Microsoft.Web.WebView2"
    if (-not (Test-Path (Join-Path $WebView2Dir "build\native\include\WebView2.h"))) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $WebView2Dir) -Force | Out-Null
        if (Get-Command nuget -ErrorAction SilentlyContinue) {
            nuget install Microsoft.Web.WebView2 -OutputDirectory (Split-Path -Parent $WebView2Dir) -ExcludeVersion
        } else {
            Write-Host "Downloading Microsoft.Web.WebView2 SDK package..."
            $nupkgZip = Join-Path ([System.IO.Path]::GetTempPath()) ("webview2-" + [Guid]::NewGuid().ToString("N") + ".zip")
            Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2" -OutFile $nupkgZip -UseBasicParsing
            Expand-Archive -Path $nupkgZip -DestinationPath $WebView2Dir -Force
            Remove-Item $nupkgZip -Force -ErrorAction SilentlyContinue
        }
    }

    $BuildDir = Join-Path $RepoRoot "windows\build"
    cmake -S (Join-Path $RepoRoot "windows") -B $BuildDir
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }

    $ExeSrc = Join-Path $BuildDir "Release\xnc.exe"
    if (-not (Test-Path $ExeSrc)) {
        throw "Built binary not found at $ExeSrc"
    }

    $InstallDir = Join-Path $env:LOCALAPPDATA "XNC"
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $InstallDir "shared") -Force | Out-Null

    Copy-Item $ExeSrc (Join-Path $InstallDir "xnc.exe") -Force
    Copy-Item (Join-Path $RepoRoot "shared\*") (Join-Path $InstallDir "shared") -Force

    # Create Start Menu shortcut
    $StartMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\XNC.lnk"
    $WshShell = New-Object -ComObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut($StartMenu)
    $Shortcut.TargetPath = Join-Path $InstallDir "xnc.exe"
    $Shortcut.WorkingDirectory = $InstallDir
    $Shortcut.Description = "XNC (XNC is not Chrome)"
    $Shortcut.Save()

    $bin = Get-Item (Join-Path $InstallDir "xnc.exe")
    Write-Host ""
    Write-Host "Installed XNC to: $($bin.FullName) ($([Math]::Round($bin.Length / 1KB, 1)) KB)"
    Write-Host "Start Menu shortcut: $StartMenu"
} finally {
    if ($TempRoot -and (Test-Path $TempRoot)) {
        Remove-Item $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
