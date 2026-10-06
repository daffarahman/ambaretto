param([string]$Game = (Join-Path $PSScriptRoot '..\build\ucrt64-release\Ambaretto.exe'))

$preview = Join-Path $PSScriptRoot '..\build\engine-audio-smoke.png'
$output = & $Game --tuning --screenshot $preview 2>&1
if ($LASTEXITCODE -ne 0 -or -not ($output -match 'AUDIO: Car engine loop ready') -or -not ($output -match 'AUDIO: Horn loop ready')) {
    $output | Write-Output
    throw 'The engine/horn loops or audio output failed to initialize.'
}
foreach ($clip in @('9mm.wav', 'smg.wav', 'ak47.wav', 'explosion.wav')) {
    if (-not ($output -match [regex]::Escape("AUDIO: $clip effect ready"))) { throw "Effect audio failed to initialize: $clip" }
}
if (-not ($output -match 'AUDIO: Police siren ready')) { throw 'The supplied police siren failed to initialize.' }
Write-Output 'Engine, horn, supplied police siren, gunfire, and explosion audio initialized successfully.'
