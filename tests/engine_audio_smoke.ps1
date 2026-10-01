param([string]$Game = (Join-Path $PSScriptRoot '..\build\ucrt64-release\forzaambazon.exe'))

$preview = Join-Path $PSScriptRoot '..\build\engine-audio-smoke.png'
$output = & $Game --tuning --screenshot $preview 2>&1
if ($LASTEXITCODE -ne 0 -or -not ($output -match 'AUDIO: Car engine loop ready')) {
    $output | Write-Output
    throw 'The engine WAV or audio output failed to initialize.'
}
Write-Output 'Engine WAV decoding and streaming smoke check passed.'
