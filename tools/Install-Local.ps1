param([string]$Destination = 'D:\Games\Standalone\Hulk Recompiled')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$build = 'D:\Projects\Build Cache\Hulk Recompiled\release'
if (Test-Path -LiteralPath $Destination) { throw "Destination already exists; no files overwritten: $Destination" }
if (!(Test-Path -LiteralPath "$build\HulkRecompiled.dll")) { throw 'Build the game first' }
if ((Get-PSDrive D).Free -lt 4GB) { throw 'At least 4 GB free on D: is required' }
$destinationPath = [IO.Path]::GetFullPath($Destination)
if (!$destinationPath.StartsWith('D:\Games\Standalone\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Choose a folder under D:\Games\Standalone' }
New-Item -ItemType Directory -Path $destinationPath | Out-Null
$manifest = [Collections.Generic.List[object]]::new()
function Copy-Verified([string]$Source, [string]$Relative) {
    $target = Join-Path $destinationPath $Relative
    if (Test-Path -LiteralPath $target) { throw "Refusing to overwrite $target" }
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target
    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sourceHash) { throw "Copy verification failed: $Relative" }
    (Get-Item -LiteralPath $target).LastWriteTimeUtc = (Get-Item -LiteralPath $Source).LastWriteTimeUtc
    $manifest.Add([pscustomobject]@{Path=$Relative;Bytes=(Get-Item -LiteralPath $target).Length;SHA256=$sourceHash})
}
foreach ($name in 'HulkRecompiled.exe','HulkRecompiled.dll') { Copy-Verified "$build\$name" $name }
foreach ($file in Get-ChildItem "$project\assets" -File -Recurse) {
    Copy-Verified $file.FullName ('assets\' + $file.FullName.Substring("$project\assets\".Length))
}
# Preserve the rebuild tools, without generated objects, test saves or developer logs.
foreach ($file in Get-ChildItem "$build\compiler" -File -Recurse) {
    $relative = $file.FullName.Substring("$build\compiler\".Length)
    if ($relative -notlike 'work\*') { Copy-Verified $file.FullName ("compiler\$relative") }
}
Copy-Verified "$project\sdk\THIRD_PARTY.txt" 'THIRD_PARTY.txt'
Copy-Verified "$project\sdk\LICENSE" 'SDK-LICENSE.txt'
Copy-Verified "$project\STATUS.md" 'STATUS.md'
@'
@echo off
setlocal
cd /d "%~dp0"
set HULK_TEST_INPUT=
set HULK_CAPTURE_DELAY_MS=
set HULK_CAPTURE_INTERVAL_MS=
set HULK_CAPTURE_COUNT=
set XBE_CAPTURE_DIR=
set XBE_PERF=
start "Hulk Recompiled" "%~dp0HulkRecompiled.exe"
'@ | Set-Content -LiteralPath "$destinationPath\Play Hulk.bat" -Encoding ASCII
@'
Hulk Recompiled - local experimental native Windows build

Double-click Play Hulk.bat. Click the game window before using the keyboard.
Enter: Start / pause. Tab: Back. WASD: move. IJKL: camera. Arrows: D-pad.
Space: A (jump), X: B (grab), Z: X (punch), C: Y.
Shift/Ctrl: left/right trigger. Q/R: White/Black. F/G: stick clicks.
Xbox-compatible controllers use the SDL3 controller backend.

Saves are in hdd beside the game. Test saves in hdd2 are separate.
Windowed 640x480, 30 fps defaults match the source game's target.
Use the runtime's settings menu to change display options.
See STATUS.md for measured verification and outstanding limitations.

This private local installation contains data extracted from your disc image.
The original disc image remains untouched. The port uses Xbox XBE Recomp by
TGP482; see SDK-LICENSE.txt, THIRD_PARTY.txt and compiler/LLVM-LICENSE.txt.
'@ | Set-Content -LiteralPath "$destinationPath\Read Me.txt" -Encoding ASCII
@'
display_mode = "windowed"
width = 640
height = 480
fps_cap = 30
vsync = true
render_percent = 100
'@ | Set-Content -LiteralPath "$destinationPath\settings.toml" -Encoding ASCII
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$destinationPath\install-manifest.json" -Encoding UTF8
Write-Host "Installed and SHA256 verified $($manifest.Count) files: $destinationPath"

