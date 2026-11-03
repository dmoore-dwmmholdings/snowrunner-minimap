# Inspect a file stream only. Never load the image as executable code.
function Get-PeArchitecture {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][System.IO.Stream]$Stream)
    if (-not $Stream.CanRead -or -not $Stream.CanSeek) { throw 'A readable, seekable stream is required.' }
    $originalPosition = $Stream.Position
    $reader = [System.IO.BinaryReader]::new($Stream, [Text.Encoding]::ASCII, $true)
    try {
        if ($Stream.Length -lt 64) { throw 'Truncated DOS header.' }
        $Stream.Position = 0
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'Missing DOS MZ signature.' }
        $Stream.Position = 0x3c
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -lt 64 -or [long]$peOffset + 26 -gt $Stream.Length) { throw 'Invalid PE header offset.' }
        $Stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw 'Missing PE signature.' }
        $machine = $reader.ReadUInt16()
        $Stream.Position = [long]$peOffset + 20
        $optionalBytes = $reader.ReadUInt16()
        $characteristics = $reader.ReadUInt16()
        if (($characteristics -band 0x0002) -eq 0 -or ($characteristics -band 0x2000) -ne 0) {
            throw 'Expected an executable image, not a DLL or object.'
        }
        if ($optionalBytes -lt 2 -or [long]$peOffset + 24 + $optionalBytes -gt $Stream.Length) {
            throw 'Truncated PE optional header.'
        }
        $magic = $reader.ReadUInt16()
        switch ($machine) {
            0x8664 { $name = 'x64'; $expectedMagic = 0x20b; $minimumBytes = 112 }
            0x014c { $name = 'x86'; $expectedMagic = 0x10b; $minimumBytes = 96 }
            0xaa64 { $name = 'arm64'; $expectedMagic = 0x20b; $minimumBytes = 112 }
            default { throw ('Unsupported PE machine: 0x{0:x4}' -f $machine) }
        }
        if ($magic -ne $expectedMagic -or $optionalBytes -lt $minimumBytes) {
            throw 'PE machine and optional header are inconsistent.'
        }
        return $name
    } finally {
        $reader.Dispose()
        $Stream.Position = $originalPosition
    }
}
Export-ModuleMember -Function Get-PeArchitecture
