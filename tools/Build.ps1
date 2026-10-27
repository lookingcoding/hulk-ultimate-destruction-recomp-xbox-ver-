param(
    [ValidateSet('Configure','Tools','Symbols','Generate','Build')][string]$Stage = 'Build',
    [int]$Jobs = 4,
    [string]$Llvm = 'D:\Projects\Build Cache\Tools\clang+llvm-23.1.4-x86_64-pc-windows-msvc\bin'
)
$ErrorActionPreference = 'Stop'
function Invoke-BuildTool([string]$File, [string[]]$Arguments) {
    # Windows PowerShell otherwise treats native stderr warnings as terminating errors when redirected.
    $ErrorActionPreference = 'Continue'
    & $File @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE) { throw "$File failed with exit code $LASTEXITCODE" }
}
function Test-AppliedPatch([string]$Patch) {
    $ErrorActionPreference = 'Continue'
    & git -C sdk apply --reverse --check $Patch 2>$null | Out-Null
    return $LASTEXITCODE -eq 0
}
$project = Split-Path $PSScriptRoot -Parent
$build = 'D:\Projects\Build Cache\Hulk Recompiled\release'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ build tools not found' }
$setup = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$env:PATH = "$(Split-Path $vswhere -Parent);$env:PATH"
$environment = & $env:ComSpec /d /s /c "`"`"$setup`" -arch=amd64 -host_arch=amd64 >nul && set`""
if ($LASTEXITCODE) { throw 'Visual Studio environment setup failed' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$cmakeTools = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$env:PATH = "$Llvm;$cmakeTools\CMake\bin;$cmakeTools\Ninja;$env:PATH"
foreach ($tool in 'clang','clang++','lld-link','cmake','ninja','git') {
    if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing build tool: $tool" }
}
New-Item -ItemType Directory -Path $build -Force | Out-Null
$generatedCache = Join-Path (Split-Path $build -Parent) 'generated'
New-Item -ItemType Directory -Path $generatedCache -Force | Out-Null
if (!(Test-Path -LiteralPath "$project\generated")) {
    New-Item -ItemType Junction -Path "$project\generated" -Target $generatedCache | Out-Null
}
if (!(Test-Path -LiteralPath "$build\assets") -and (Test-Path -LiteralPath "$project\assets")) {
    New-Item -ItemType Junction -Path "$build\assets" -Target "$project\assets" | Out-Null
}
$env:TEMP = $build
$env:TMP = $build
Push-Location $project
try {
    foreach ($patchFile in Get-ChildItem (Join-Path $project 'patches') -Filter '*.patch' -File) {
        $patch = $patchFile.FullName
        if (!(Test-AppliedPatch $patch)) {
            Invoke-BuildTool git @('-C','sdk','apply','--check',$patch)
            Invoke-BuildTool git @('-C','sdk','apply',$patch)
        }
    }
    Invoke-BuildTool cmake @('--preset','release')
    if ($Stage -eq 'Configure') { return }
    Invoke-BuildTool cmake @('--build',$build,'--target','xbe_symbols','xbe_recomp','hulk_disasm','hulk_analysis_tests','hulk_input_tests','hulk_kernel_io_tests','hulk_display_menu_tests','--parallel',"$Jobs")
    Invoke-BuildTool ctest @('--test-dir',$build,'--output-on-failure')
    if ($Stage -eq 'Tools') { return }
    if (!(Test-Path -LiteralPath 'config\game.toml') -or !(Select-String -LiteralPath 'config\game.toml' -Pattern '^\[symbols\]' -Quiet)) {
        New-Item -ItemType Directory -Path config -Force | Out-Null
        Invoke-BuildTool "$build\xbe_symbols.exe" @('assets/default.xbe','sdk/xdk','--library','HulkRecompiled.dll','--out','config/game.toml')
    }
    if ($Stage -eq 'Symbols') { return }
    if ($Stage -eq 'Generate') {
        Invoke-BuildTool "$build\compiler\xbe_recomp.exe" @('config/game.toml','--xdk','sdk/xdk')
        return
    }
    Invoke-BuildTool cmake @('--build',$build,'--parallel',"$Jobs")
    Write-Host "Built: $build\HulkRecompiled.exe"
} finally { Pop-Location }
