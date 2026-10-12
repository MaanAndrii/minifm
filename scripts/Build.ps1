param([string]$Port, [switch]$Flash, [switch]$Monitor)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$localIdf = Join-Path $projectRoot '.tools\esp-idf'
if (Test-Path -LiteralPath $localIdf) {
    $env:IDF_PATH = $localIdf
    $env:IDF_TOOLS_PATH = Join-Path $projectRoot '.tools\idf-tools'
    $pythonEnv = Join-Path $env:IDF_TOOLS_PATH 'python_env\idf5.5_py3.12_env\Scripts'
    if (Test-Path -LiteralPath $pythonEnv) { $env:PATH = "$pythonEnv;$env:PATH" }
    . (Join-Path $localIdf 'export.ps1')
}
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) { throw 'Activate ESP-IDF 5.5.x first, or run scripts/Setup.ps1.' }
Push-Location $projectRoot
try {
    if (-not (Test-Path -LiteralPath 'sdkconfig')) { idf.py set-target esp32s3; if ($LASTEXITCODE) { throw 'Target configuration failed' } }
    idf.py build
    if ($LASTEXITCODE) { throw 'Firmware build failed' }
    if ($Flash) {
        if (-not $Port) { throw 'Specify -Port COMx for flashing.' }
        idf.py -p $Port flash
        if ($LASTEXITCODE) { throw 'Flashing failed' }
    }
    if ($Monitor) { if (-not $Port) { throw 'Specify -Port COMx for monitoring.' }; idf.py -p $Port monitor }
} finally { Pop-Location }
