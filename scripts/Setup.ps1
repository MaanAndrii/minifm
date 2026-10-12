param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $projectRoot '.tools'
$idfRoot = Join-Path $toolsRoot 'esp-idf'
New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
if (-not (Test-Path -LiteralPath $idfRoot)) {
    git clone --depth 1 --branch v5.5.5 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git $idfRoot
    if ($LASTEXITCODE) { throw 'ESP-IDF download failed' }
}
$env:IDF_TOOLS_PATH = Join-Path $toolsRoot 'idf-tools'
& $Python (Join-Path $idfRoot 'tools\idf_tools.py') install --targets esp32s3
if ($LASTEXITCODE) { throw 'Toolchain installation failed' }
& $Python (Join-Path $idfRoot 'tools\idf_tools.py') install-python-env
if ($LASTEXITCODE) { throw 'Python environment installation failed' }
Write-Output 'Ready. Run scripts/Build.ps1.'
