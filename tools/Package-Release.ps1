param(
    [Parameter(Mandatory=$true)][ValidatePattern('^v\d+\.\d+\.\d+$')][string]$Version,
    [string]$Output = 'D:\Projects\Build Cache\Hulk Recompiled\dist',
    [string]$Build = 'D:\Projects\Build Cache\Hulk Recompiled\release'
)
# The release players download: the loader, Hulk Recompiled.exe (tools/loader.cpp), beside assets\ and game\, which
# holds an executable for each release of the game's disc and the compiler folder that builds the game's code from
# their own disc on first launch. The loader starts the first executable, which hands over to the one built for the
# disc in assets (src/common/release.cpp). No game data and no code recompiled from the game is included.
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$build = $Build
$loader = 'Hulk Recompiled'
$executables = 'HulkRecompiled', 'HulkRecompiledUSA'
$modtool = 'Hulk Mod Tool'
$modstudio = 'Hulk Mod Studio'
foreach ($exe in @($loader, $modtool, $modstudio) + $executables) { if (!(Test-Path -LiteralPath "$build\$exe.exe")) { throw "Build the game first: $exe.exe is missing" } }
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
Copy-Verified "$build\$loader.exe" "$loader.exe"
Copy-Verified "$build\$modtool.exe" "$modtool.exe"  # copies the game's files out for modding (tools/modtool.cpp)
Copy-Verified "$build\$modstudio.exe" "$modstudio.exe"  # OpenIV-style archive browser & mod manager (tools/modstudio.cpp)
foreach ($exe in $executables) { Copy-Verified "$build\$exe.exe" "game\$exe.exe" }
foreach ($file in Get-ChildItem "$build\compiler" -File -Recurse) {
    $relative = $file.FullName.Substring("$build\compiler\".Length)
    if ($relative -notlike 'work\*') { Copy-Verified $file.FullName "game\compiler\$relative" }
}
Copy-Verified "$project\README.md" 'README.md'
Copy-Verified "$project\STATUS.md" 'STATUS.md'
Copy-Verified "$project\recomplauncher.json" 'recomplauncher.json'  # RecompLauncher reads it from the top of the zip
Copy-Verified "$project\LICENSE" 'LICENSE.txt'
Copy-Verified "$project\sdk\LICENSE" 'SDK-LICENSE.txt'
Copy-Verified "$project\sdk\THIRD_PARTY.txt" 'THIRD_PARTY.txt'
Copy-Verified "$project\plugins\hulk_plugin.h" 'plugins\hulk_plugin.h'
Copy-Verified "$project\plugins\example_trainer.cpp" 'plugins\example_trainer.cpp'
Copy-Verified "$project\docs\plugin-api.md" 'docs\plugin-api.md'

New-Item -ItemType Directory -Path "$stage\assets" | Out-Null
@'
Put your own Xbox disc image of The Incredible Hulk: Ultimate Destruction in this folder, then start
Hulk Recompiled.exe.

- Any .iso file name works. The game checks the image when it starts: it must be an Xbox disc with the
  game's default.xbe on it.
- Or put the extracted game files here instead, with default.xbe in this folder.

No game files are included with this download.
'@ | Set-Content -LiteralPath "$stage\assets\PUT YOUR GAME ISO HERE.txt" -Encoding ASCII

$items = @(Get-ChildItem -LiteralPath $stage | Select-Object -ExpandProperty Name)
& tar.exe -a -c -f $zip -C $stage @items
if ($LASTEXITCODE -ne 0) { throw "tar.exe failed to create $zip (exit $LASTEXITCODE)" }
$hash = (Get-FileHash -LiteralPath $zip).Hash
Write-Host "Packaged $zip ($([math]::Round((Get-Item -LiteralPath $zip).Length / 1MB)) MB), SHA256 $hash"
