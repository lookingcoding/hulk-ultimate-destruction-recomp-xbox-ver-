param(
    [Parameter(Mandatory=$true)][string]$Image,
    [string]$Project = (Split-Path $PSScriptRoot -Parent),
    [switch]$ExtractAll
)
$ErrorActionPreference = 'Stop'
$assets = Join-Path $Project 'assets'
$reports = Join-Path $Project 'reports'
New-Item -ItemType Directory -Path $assets,$reports -Force | Out-Null
$stream = [IO.File]::Open($Image, 'Open', 'Read', 'Read')
function Read-Bytes([long]$Offset, [int]$Count) {
    if ($Offset -lt 0 -or $Count -lt 0 -or $Offset + $Count -gt $stream.Length) { throw 'Disc read outside image' }
    $null = $stream.Seek($Offset, 'Begin')
    $buffer = New-Object byte[] $Count
    $done = 0
    while ($done -lt $Count) {
        $n = $stream.Read($buffer, $done, $Count - $done)
        if (!$n) { throw 'Truncated disc read' }
        $done += $n
    }
    return ,$buffer
}
$entries = [Collections.Generic.List[object]]::new()
function Read-Directory([uint32]$Sector, [uint32]$Size, [string]$Path, [int]$Depth) {
    if ($Depth -gt 32 -or $Size -gt 16MB) { throw 'Invalid directory table' }
    if (!$Size) { return }
    $table = Read-Bytes ($script:partition + [long]$Sector * 2048) $Size
    $pending = [Collections.Generic.Stack[int]]::new()
    $seen = [Collections.Generic.HashSet[int]]::new()
    $pending.Push(0)
    while ($pending.Count) {
        $at = $pending.Pop()
        if (!$seen.Add($at)) { throw 'Directory tree cycle' }
        if ($at + 14 -gt $Size) { throw 'Invalid directory entry' }
        $left = [BitConverter]::ToUInt16($table,$at)
        if ($left -eq 65535) { continue }
        $right = [BitConverter]::ToUInt16($table,$at+2)
        $fileSector = [BitConverter]::ToUInt32($table,$at+4)
        $fileSize = [BitConverter]::ToUInt32($table,$at+8)
        $isDir = ($table[$at+12] -band 16) -ne 0
        $len = $table[$at+13]
        if (!$len -or $at+14+$len -gt $Size) { throw 'Invalid entry name' }
        $name = [Text.Encoding]::ASCII.GetString($table,$at+14,$len)
        if ($name -in '.', '..' -or $name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) { throw 'Unsafe entry name' }
        $relative = if ($Path) { "$Path/$name" } else { $name }
        $offset = $script:partition + [long]$fileSector * 2048
        if ($offset + $fileSize -gt $stream.Length) { throw 'Entry outside image' }
        $entries.Add([pscustomobject]@{Path=$relative; Sector=$fileSector; Offset=$offset; Size=$fileSize; Directory=$isDir})
        if ($left) { $pending.Push($left*4) }
        if ($right) { $pending.Push($right*4) }
        if ($isDir) { Read-Directory $fileSector $fileSize $relative ($Depth+1) }
    }
}
try {
    $found = $false
    foreach ($candidate in @(0L,0x18300000L)) {
        if ($candidate + 65536 + 2048 -gt $stream.Length) { continue }
        $volume = Read-Bytes ($candidate+65536) 2048
        if ([Text.Encoding]::ASCII.GetString($volume,0,20) -eq 'MICROSOFT*XBOX*MEDIA' -and
            [Text.Encoding]::ASCII.GetString($volume,2028,20) -eq 'MICROSOFT*XBOX*MEDIA') {
            $script:partition = $candidate
            $found = $true
            break
        }
    }
    if (!$found) { throw 'No XDVDFS volume found' }
    Read-Directory ([BitConverter]::ToUInt32($volume,20)) ([BitConverter]::ToUInt32($volume,24)) '' 0
    $entries | Sort-Object Path | Export-Csv (Join-Path $reports 'disc-files.csv') -NoTypeInformation
    foreach ($entry in $entries | Where-Object { !$_.Directory -and ($ExtractAll -or $_.Path -match '\.xbe$') }) {
        $destination = Join-Path $assets $entry.Path
        if ($entry.Path -notmatch '\.xbe$') {
            if (Test-Path -LiteralPath $destination) { throw "Refusing to overwrite $destination" }
            New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
            $output = [IO.File]::Open($destination, 'CreateNew', 'Write', 'None')
            $digest = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
            try {
                $null = $stream.Seek($entry.Offset, 'Begin')
                $remaining = [long]$entry.Size
                $buffer = New-Object byte[] 4194304
                while ($remaining -gt 0) {
                    $n = $stream.Read($buffer,0,[int][Math]::Min($remaining,$buffer.Length))
                    if (!$n) { throw 'Truncated game file' }
                    $output.Write($buffer,0,$n)
                    $digest.AppendData($buffer,0,$n)
                    $remaining -= $n
                }
                $sourceHash = ([BitConverter]::ToString($digest.GetHashAndReset())).Replace('-','')
            } finally { $output.Dispose(); $digest.Dispose() }
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $sourceHash) { throw "Hash verification failed: $destination" }
            Write-Host "Verified $($entry.Path)"
            continue
        }
        $bytes = Read-Bytes $entry.Offset $entry.Size
        if (Test-Path -LiteralPath $destination) {
            $existing = [IO.File]::ReadAllBytes($destination)
            if ([Convert]::ToBase64String($existing) -ne [Convert]::ToBase64String($bytes)) { throw "Refusing to overwrite $destination" }
        } else {
            New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
            [IO.File]::WriteAllBytes($destination,$bytes)
        }
        Get-FileHash -LiteralPath $destination -Algorithm SHA256 | Format-List
    }
    [pscustomobject]@{Image=(Resolve-Path -LiteralPath $Image).Path; Bytes=$stream.Length; Partition=$partition; Files=@($entries | Where-Object { !$_.Directory }).Count} |
        ConvertTo-Json | Set-Content (Join-Path $reports 'disc.json')
    $entries | Where-Object { $_.Path -notmatch '/' } | Sort-Object Path | Format-Table Path,Size,Directory
} finally { $stream.Dispose() }
