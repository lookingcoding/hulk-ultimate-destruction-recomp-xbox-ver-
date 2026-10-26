param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Z+]+$')][string]$Buttons,
    [ValidateRange(1,60000)][int]$Duration = 300,
    [ValidateRange(500,60000)][int]$Delay = 1500,
    [string]$Replay = 'D:\Projects\Build Cache\Hulk Recompiled\finish-tutorial.input'
)
$ErrorActionPreference = 'Stop'
$process = @(Get-Process HulkRecompiled -ErrorAction SilentlyContinue | Where-Object Path -eq 'D:\Projects\Build Cache\Hulk Recompiled\release\HulkRecompiled.exe')
if ($process.Count -ne 1) { throw 'Expected one running development test' }
$log = Get-ChildItem 'D:\Projects\Build Cache\Hulk Recompiled\release\logs' -Filter 'hulk-2*' -File | Sort-Object CreationTime -Descending | Select-Object -First 1
$elapsed = [int]((Get-Date) - $log.CreationTime).TotalMilliseconds
$at = $elapsed + $Delay
$lines = @(Get-Content -LiteralPath $Replay | Where-Object { $_ -and $_[0] -ne '#' }) + "$at $Duration $Buttons"
$lines | Sort-Object { [int]($_.Split(' ')[0]) } | Set-Content -LiteralPath $Replay -Encoding ASCII
Write-Host "Scheduled $Buttons at $at ms for $Duration ms (now $elapsed)."
