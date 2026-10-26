param([Parameter(Mandatory=$true)][string]$Dump)
$ErrorActionPreference = 'Stop'
$bytes = [IO.File]::ReadAllBytes($Dump)
function D32([int]$o) { [BitConverter]::ToUInt32($bytes,$o) }
function D64([int]$o) { [BitConverter]::ToUInt64($bytes,$o) }
$streams = @{}
$directory = D32 12
for($i=0;$i -lt (D32 8);$i++) { $o=$directory+$i*12; $streams[(D32 $o)]=(D32 ($o+8)) }
if (!$streams.ContainsKey([uint32]6) -or !$streams.ContainsKey([uint32]4)) { throw 'Dump has no exception/module stream' }
$exception = $streams[[uint32]6]
$pc = D64 ($exception+24)
$modules = $streams[[uint32]4]
for($i=0;$i -lt (D32 $modules);$i++) {
    $o=$modules+4+$i*108
    $base=D64 $o
    $size=D32 ($o+8)
    if ($pc -lt $base -or $pc -ge $base+$size) { continue }
    $nameRva=D32 ($o+20)
    $name=[Text.Encoding]::Unicode.GetString($bytes,$nameRva+4,(D32 $nameRva))
    $rva=$pc-$base
    "Exception: 0x{0:X8}; PC: 0x{1:X}; Module: {2}; RVA: 0x{3:X}" -f (D32 ($exception+8)),$pc,$name,$rva
    if (!(Test-Path -LiteralPath $name)) { return }
    $pe=[IO.File]::ReadAllBytes($name)
    function P32([int]$x) { [BitConverter]::ToUInt32($pe,$x) }
    function P16([int]$x) { [BitConverter]::ToUInt16($pe,$x) }
    $nt=P32 0x3c
    $optional=$nt+24
    $sections=$optional+(P16 ($nt+20))
    function File-Offset([uint32]$Rva) {
        for($j=0;$j -lt (P16 ($nt+6));$j++) {
            $s=$sections+$j*40
            $va=P32 ($s+12)
            if($Rva -ge $va -and $Rva -lt $va+[Math]::Max((P32 ($s+8)),(P32 ($s+16)))) { return (P32 ($s+20))+$Rva-$va }
        }
        throw ('Unmapped PE RVA 0x{0:X}' -f $Rva)
    }
    $directoryOffset=if((P16 $optional) -eq 0x20b){112}else{96}
    $export=File-Offset (P32 ($optional+$directoryOffset))
    $functions=File-Offset (P32 ($export+28))
    $names=File-Offset (P32 ($export+32))
    $ordinals=File-Offset (P32 ($export+36))
    $best=0
    $bestName=''
    for($j=0;$j -lt (P32 ($export+24));$j++) {
        $ordinal=P16 ($ordinals+$j*2)
        $address=P32 ($functions+$ordinal*4)
        if($address -le $rva -and $address -ge $best) {
            $no=File-Offset (P32 ($names+$j*4))
            $end=$no
            while($pe[$end]){$end++}
            $bestName=[Text.Encoding]::ASCII.GetString($pe,$no,$end-$no)
            $best=$address
        }
    }
    'Nearest export: {0}+0x{1:X} (RVA 0x{2:X})' -f $bestName,($rva-$best),$best
    return
}
throw 'Exception PC is not inside a recorded module'
