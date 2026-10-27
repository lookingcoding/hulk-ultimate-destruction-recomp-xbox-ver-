param(
    [Parameter(Mandatory=$true)][ValidatePattern('^v\d+\.\d+\.\d+$')][string]$Version,
    [string]$Output = 'D:\Projects\Build Cache\Hulk Recompiled\dist'
)
# The release players download: an executable for each release of the game's disc and the compiler folder that builds
# the game's code from their own disc on first launch. Whichever executable starts hands over to the one built for
# the disc in assets (src/common/release.cpp). No game data and no code recompiled from the game is included.
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$build = 'D:\Projects\Build Cache\Hulk Recompiled\release'
$executables = 'HulkRecompiled', 'HulkRecompiledUSA'
foreach ($exe in $executables) { if (!(Test-Path -LiteralPath "$build\$exe.exe")) { throw "Build the game first: $exe.exe is missing" } }
$name = "HulkRecompiled-$Version-windows-x64"
$stage = Join-Path $Output $name
$zip = "$stage.zip"
foreach ($path in $stage, $zip) { if (Test-Path -LiteralPath $path) { throw "Already exists; nothing overwritten: $path" } }
New-Item -ItemType Directory -Path $stage | Out-Null

function Copy-Verified([string]$Source, [string]$Relative) {
    $target = Join-Path $stage $Relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target
    if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $Source).Hash) { throw "Copy verification failed: $Relative" }
}
foreach ($exe in $executables) { Copy-Verified "$build\$exe.exe" "$exe.exe" }
foreach ($file in Get-ChildItem "$build\compiler" -File -Recurse) {
    $relative = $file.FullName.Substring("$build\compiler\".Length)
    if ($relative -notlike 'work\*') { Copy-Verified $file.FullName "compiler\$relative" }
}
Copy-Verified "$project\README.md" 'README.md'
Copy-Verified "$project\STATUS.md" 'STATUS.md'
Copy-Verified "$project\LICENSE" 'LICENSE.txt'
Copy-Verified "$project\sdk\LICENSE" 'SDK-LICENSE.txt'
Copy-Verified "$project\sdk\THIRD_PARTY.txt" 'THIRD_PARTY.txt'

New-Item -ItemType Directory -Path "$stage\assets" | Out-Null
@'
Put your own Xbox disc image of The Incredible Hulk: Ultimate Destruction in this folder.

- Any .iso file name works. The game checks the image when it starts: it must be an Xbox disc with the
  game's default.xbe on it.
- Or put the extracted game files here instead, with default.xbe in this folder.

No game files are included with this download.
'@ | Set-Content -LiteralPath "$stage\assets\PUT YOUR GAME ISO HERE.txt" -Encoding ASCII
@'
@echo off
cd /d "%~dp0"
start "Hulk Recompiled" "%~dp0HulkRecompiled.exe"
'@ | Set-Content -LiteralPath "$stage\Play.bat" -Encoding ASCII

Compress-Archive -Path "$stage\*" -DestinationPath $zip
$hash = (Get-FileHash -LiteralPath $zip).Hash
Write-Host "Packaged $zip ($([math]::Round((Get-Item -LiteralPath $zip).Length / 1MB)) MB), SHA256 $hash"
