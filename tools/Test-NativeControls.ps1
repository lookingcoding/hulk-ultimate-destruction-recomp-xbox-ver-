param(
 [string]$Build='D:\Projects\Build Cache\Hulk Recompiled\release',
 [string]$GameAssets=(Join-Path (Split-Path $PSScriptRoot -Parent) 'assets'),
 [string]$Work='D:\Projects\Build Cache\Hulk Recompiled\Native Controls Proof',
 [ValidateSet('HulkRecompiled','HulkRecompiledUSA')][string]$Image='HulkRecompiled',
 [string]$Replay=(Join-Path (Split-Path $PSScriptRoot -Parent) 'tests/fixtures/native-controls.input'),
 [ValidateRange(164,300)][int]$Seconds=166
)
$ErrorActionPreference='Stop'
$env:TEMP='D:\Projects\Build Cache\Temp'
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
$env:TMP=$env:TEMP
foreach($path in @($Build,$GameAssets,$Work,$Replay)) {
 if(![IO.Path]::GetFullPath($path).StartsWith('D:\',[StringComparison]::OrdinalIgnoreCase)){throw 'All fixture paths must be on D:'}
}
$runDir=Join-Path $Work (Get-Date -Format 'yyyyMMdd-HHmmss')
$buildDir=$Build
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
foreach($name in @("$Image.exe","$Image.dll")) {
 Copy-Item -LiteralPath (Join-Path $buildDir $name) -Destination $runDir
 if((Get-FileHash (Join-Path $buildDir $name)).Hash -ne (Get-FileHash (Join-Path $runDir $name)).Hash){throw 'Copy mismatch'}
}
if(!(Test-Path -LiteralPath "$runDir\assets")){New-Item -ItemType Junction -Path "$runDir\assets" -Target $GameAssets | Out-Null}
@'
display_mode = "windowed"
width = 1280
height = 720
fps_cap = 60
vsync = false
aspect = "16:9"
button_prompts = "keyboard"
'@ | Set-Content -LiteralPath "$runDir\settings.toml" -Encoding ASCII
$fixtureVariables=@('HULK_BACKGROUND_TEST','HULK_TEST_INPUT','XBE_RENDERER','XBE_CAPTURE_DIR',
                    'HULK_CAPTURE_DELAY_MS','HULK_CAPTURE_INTERVAL_MS','HULK_CAPTURE_COUNT')
$previousEnvironment=@{}
foreach($name in $fixtureVariables){$previousEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$env:HULK_BACKGROUND_TEST='1'
Copy-Item -LiteralPath $Replay -Destination "$runDir\controls.input"
$env:HULK_TEST_INPUT="$runDir\controls.input"
$env:XBE_RENDERER='d3d11'
$env:XBE_CAPTURE_DIR="$runDir\captures"
$env:HULK_CAPTURE_DELAY_MS=$null
$env:HULK_CAPTURE_INTERVAL_MS=$null
$env:HULK_CAPTURE_COUNT=$null
if (!('NativeFixtureWindows' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NativeFixtureWindows {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
 public delegate bool Callback(IntPtr hwnd, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb, IntPtr l);
}
'@
}
$start=[DateTime]::UtcNow
try {
 $p=Start-Process -FilePath "$runDir\$Image.exe" -WorkingDirectory $runDir -WindowStyle Hidden -PassThru
} finally {
 foreach($name in $fixtureVariables){[Environment]::SetEnvironmentVariable($name,$previousEnvironment[$name],'Process')}
}
$p.Id | Set-Content "$runDir\pid.txt"
$observations=0
$violations=0
try {
 while (!$p.HasExited -and ([DateTime]::UtcNow-$start).TotalSeconds -lt $Seconds) {
  $observations++
  [uint32]$fgPid=0
  [void][NativeFixtureWindows]::GetWindowThreadProcessId([NativeFixtureWindows]::GetForegroundWindow(),[ref]$fgPid)
  if($fgPid -eq $p.Id){$violations++;throw 'Fixture acquired foreground'}
  $script:visible=$false
  [void][NativeFixtureWindows]::EnumWindows({param($w,$l) [uint32]$wpid=0; [void][NativeFixtureWindows]::GetWindowThreadProcessId($w,[ref]$wpid); if($wpid -eq $p.Id -and [NativeFixtureWindows]::IsWindowVisible($w)){$script:visible=$true};return $true},[IntPtr]::Zero)
  if($script:visible){$violations++;throw 'Fixture showed a window'}
  Start-Sleep -Milliseconds 100
 }
} finally {
 if(!$p.HasExited){ Stop-Process -Id $p.Id; $p.WaitForExit() }
 [pscustomobject]@{Pid=$p.Id;Observations=$observations;VisibleOrForegroundViolations=$violations;Seconds=([DateTime]::UtcNow-$start).TotalSeconds;Audio='samples discarded; no XAudio2 device';Input='internal replay only'} | ConvertTo-Json | Set-Content "$runDir\safety.json"
}
Get-Content "$runDir\safety.json"

$logs=Get-ChildItem -LiteralPath "$runDir\logs" -Filter '*.log'
if(!(Select-String -Path $logs.FullName -SimpleMatch 'silent mixer clock active' -Quiet)){throw 'Silent mixer evidence missing'}
Write-Host "Native game frames and evidence: $runDir"
