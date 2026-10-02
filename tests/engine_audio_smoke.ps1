param([string]$Game = (Join-Path $PSScriptRoot '..\build\ucrt64-release\forzaambazon.exe'))

$preview = Join-Path $PSScriptRoot '..\build\engine-audio-smoke.png'
$output = & $Game --tuning --screenshot $preview 2>&1
if ($LASTEXITCODE -ne 0 -or -not ($output -match 'AUDIO: Car engine loop ready') -or -not ($output -match 'AUDIO: Horn loop ready')) {
    $output | Write-Output
    throw 'The engine/horn loops or audio output failed to initialize.'
}
Write-Output 'Shared engine and trimmed horn streams initialized successfully.'
