param([string]$Xbe = (Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\default.xbe'))
$ErrorActionPreference = 'Stop'
$bytes = [IO.File]::ReadAllBytes($Xbe)
function U32([int]$Offset) { [BitConverter]::ToUInt32($bytes,$Offset) }
function U16([int]$Offset) { [BitConverter]::ToUInt16($bytes,$Offset) }
if ($bytes.Length -lt 0x180 -or [Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'XBEH') { throw 'Not an XBE' }
$base = U32 0x104
$cert = (U32 0x118) - $base
$libraries = @()
$libOffset = (U32 0x164) - $base
for ($i=0; $i -lt (U32 0x160); $i++) {
    $o = $libOffset+$i*16
    $libraries += [pscustomobject]@{Name=[Text.Encoding]::ASCII.GetString($bytes,$o,8).Trim([char]0); Major=(U16 ($o+8)); Minor=(U16 ($o+10)); Build=(U16 ($o+12)); Flags=('0x{0:X4}' -f (U16 ($o+14)))}
}
$sections = @()
$secOffset = (U32 0x120) - $base
for ($i=0; $i -lt (U32 0x11c); $i++) {
    $o = $secOffset+$i*56
    $nameOffset = (U32 ($o+20)) - $base
    $end = $nameOffset
    while ($bytes[$end]) { $end++ }
    $sections += [pscustomobject]@{Name=[Text.Encoding]::ASCII.GetString($bytes,$nameOffset,$end-$nameOffset); Address=('0x{0:X8}' -f (U32 ($o+4))); VirtualSize=(U32 ($o+8)); RawOffset=(U32 ($o+12)); RawSize=(U32 ($o+16)); Flags=('0x{0:X}' -f (U32 $o))}
}
[pscustomobject]@{
    Title=[Text.Encoding]::Unicode.GetString($bytes,$cert+12,80).Trim([char]0)
    TitleId=('0x{0:X8}' -f (U32 ($cert+8)))
    Entry=('0x{0:X8}' -f ((U32 0x128) -bxor 0xA8FC57AB))
    ImageBase=('0x{0:X8}' -f $base)
    ImageSize=(U32 0x10c)
    SHA256=(Get-FileHash -LiteralPath $Xbe -Algorithm SHA256).Hash
    Libraries=$libraries
    Sections=$sections
} | ConvertTo-Json -Depth 5
