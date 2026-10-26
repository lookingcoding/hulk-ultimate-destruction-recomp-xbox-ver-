param(
    [string]$Replay = (Join-Path (Split-Path $PSScriptRoot -Parent) 'tests\tutorial.input'),
    [ValidateRange(10,600)][int]$Seconds = 195,
    [string]$Run = 'D:\Projects\Build Cache\Hulk Recompiled\release'
)
$ErrorActionPreference = 'Stop'
$run = (Resolve-Path -LiteralPath $Run).Path
$exe = Join-Path $run 'HulkRecompiled.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Build the game first' }
if (Get-Process HulkRecompiled -ErrorAction SilentlyContinue) { throw 'Close the existing game before running the smoke test' }
$Replay = (Resolve-Path -LiteralPath $Replay).Path
$env:XBE_PERF = '1'
$env:XBE_CAPTURE_DIR = 'captures'
$env:HULK_CAPTURE_DELAY_MS = '5000'
$env:HULK_CAPTURE_INTERVAL_MS = '10000'
$env:HULK_CAPTURE_COUNT = '20'
$env:HULK_TEST_INPUT = $Replay
# SDK instance 2 uses hdd2 and separate logs, keeping test saves isolated.
$started = Get-Date
$process = Start-Process -FilePath $exe -WorkingDirectory $run -ArgumentList '--instance','2' -WindowStyle Hidden -PassThru
Write-Host "Smoke test PID $($process.Id); replay $Replay; duration $Seconds seconds; saves in hdd2."
$ended = $process.WaitForExit($Seconds * 1000)
if ($ended) { Write-Host "Game exited early: $($process.ExitCode)" }
else { Stop-Process -Id $process.Id; Write-Host 'Timed test ended.' }
$log = Get-ChildItem "$run\logs" -Filter 'hulk-2*' -File | Where-Object LastWriteTime -ge $started | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$log) { throw 'No runtime log found' }
$errors = @(Select-String -LiteralPath $log.FullName -Pattern '\[error\]')
Write-Host "Runtime log: $($log.FullName); error lines: $($errors.Count)"
Get-ChildItem "$run\captures" -Recurse -Filter final.bmp | Where-Object LastWriteTime -ge $started | Sort-Object LastWriteTime | Select-Object FullName
if ($errors.Count -or $ended) { throw 'Smoke test requires investigation; inspect the log and renderer captures' }
Write-Host 'Timed run had no logged runtime errors. Inspect captures to assess rendering and progression.'
