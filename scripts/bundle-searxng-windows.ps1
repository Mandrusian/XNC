param(
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $RepoRoot "windows\searxng-runtime"
}

$WorkTmp = Join-Path ([System.IO.Path]::GetTempPath()) ("xnc-searxng-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $WorkTmp -Force | Out-Null

try {
    $PyZipUrl = "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip"
    $PyZip = Join-Path $WorkTmp "python-embed.zip"
    Write-Host "-> Downloading Python 3.12.10 embeddable package..."
    Invoke-WebRequest -Uri $PyZipUrl -OutFile $PyZip -UseBasicParsing

    if (Test-Path $OutDir) {
        Remove-Item $OutDir -Recurse -Force
    }
    $PyDir = Join-Path $OutDir "python"
    $SiteDir = Join-Path $OutDir "site-packages"
    $SearxDir = Join-Path $OutDir "searxng"
    New-Item -ItemType Directory -Path $PyDir, $SiteDir, $SearxDir -Force | Out-Null

    Expand-Archive -Path $PyZip -DestinationPath $PyDir -Force

    # Configure python312._pth so embeddable Python resolves site-packages & searxng
    $PthFile = Join-Path $PyDir "python312._pth"
    @"
python312.zip
.
../site-packages
../searxng
import site
"@ | Set-Content -Path $PthFile -Encoding ASCII

    Write-Host "-> Cloning upstream searxng/searxng..."
    $SrcDir = Join-Path $WorkTmp "searxng-src"
    git clone --depth 1 https://github.com/searxng/searxng.git $SrcDir

    $RawReqs = Get-Content (Join-Path $SrcDir "requirements.txt")
    $FilteredReqs = $RawReqs | Where-Object {
        $_ -notmatch '^\s*(uwsgi|gunicorn|setproctitle|uvloop)\b'
    }
    $ReqsFile = Join-Path $WorkTmp "reqs.txt"
    $FilteredReqs | Set-Content -Path $ReqsFile -Encoding UTF8

    Write-Host "-> Installing SearXNG dependencies + waitress via uv..."
    uv python install 3.12
    $UvPy = (uv python find 3.12)
    uv pip install `
        --python $UvPy `
        --python-version 3.12 `
        --target $SiteDir `
        --no-cache `
        -r $ReqsFile `
        waitress

    Write-Host "-> Copying SearXNG package + launcher..."
    Copy-Item (Join-Path $SrcDir "searx") (Join-Path $SearxDir "searx") -Recurse -Force
    Copy-Item (Join-Path $RepoRoot "shared\run_searxng.py") (Join-Path $OutDir "run_searxng.py") -Force

    Write-Host "-> Pruning caches and tests..."
    Get-ChildItem $OutDir -Recurse -Directory -Force | Where-Object {
        $_.Name -in @("__pycache__", "tests", "test")
    } | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

    $PortablePy = Join-Path $PyDir "python.exe"
    $env:PYTHONNOUSERSITE = "1"
    $env:PYTHONDONTWRITEBYTECODE = "1"
    $CheckData = Join-Path $WorkTmp "check-data"
    & $PortablePy (Join-Path $OutDir "run_searxng.py") --check --data-dir $CheckData
    if ($LASTEXITCODE -ne 0) {
        throw "Bundled SearXNG verification failed."
    }

    $TotalBytes = (Get-ChildItem $OutDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host ("-> Bundled SearXNG runtime size: {0:N1} MB" -f ($TotalBytes / 1MB))
} finally {
    Remove-Item $WorkTmp -Recurse -Force -ErrorAction SilentlyContinue
}
