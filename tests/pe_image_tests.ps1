$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/PeImage.psm1') -Force
$checks = 0
function New-Fixture([uint16]$machine = 0x8664, [uint16]$magic = 0x20b) {
    $bytes = [byte[]]::new(320)
    $stream = [IO.MemoryStream]::new($bytes, $true)
    $writer = [IO.BinaryWriter]::new($stream, [Text.Encoding]::ASCII, $true)
    $writer.Write([uint16]0x5a4d)
    $stream.Position = 0x3c; $writer.Write([uint32]0x80)
    $stream.Position = 0x80; $writer.Write([uint32]0x00004550); $writer.Write($machine)
    $stream.Position = 0x94; $writer.Write([uint16]112); $writer.Write([uint16]2); $writer.Write($magic)
    $writer.Dispose()
    $stream.Position = 7
    return ,$stream
}
function Expect-Architecture($stream, $expected) {
    try {
        if ((Get-PeArchitecture $stream) -ne $expected) { throw 'Wrong architecture.' }
        if ($stream.Position -ne 7 -or -not $stream.CanRead) { throw 'Caller stream was changed or closed.' }
        $script:checks++
    } finally { $stream.Dispose() }
}
function Expect-Rejected($stream) {
    $position = $stream.Position
    $rejected = $false
    try { $null = Get-PeArchitecture $stream } catch { $rejected = $true }
    try {
        if (-not $rejected) { throw 'Malformed image was accepted.' }
        if ($stream.Position -ne $position -or -not $stream.CanRead) { throw 'Caller stream was changed or closed.' }
        $script:checks++
    } finally { $stream.Dispose() }
}
Expect-Architecture (New-Fixture) 'x64'
Expect-Architecture (New-Fixture 0x014c 0x10b) 'x86'
Expect-Architecture (New-Fixture 0xaa64 0x20b) 'arm64'
Expect-Rejected ([IO.MemoryStream]::new([byte[]]::new(12)))
$s = New-Fixture; $s.Position = 0; $s.WriteByte(0); Expect-Rejected $s
$s = New-Fixture; $s.Position = 0x3c; $s.Write([byte[]](255,255,255,255),0,4); Expect-Rejected $s
$s = New-Fixture; $s.Position = 0x80; $s.WriteByte(0); Expect-Rejected $s
$s = New-Fixture; $s.Position = 0x94; $s.WriteByte(255); Expect-Rejected $s
$s = New-Fixture; $s.Position = 0x97; $s.WriteByte(0x20); Expect-Rejected $s
Expect-Rejected (New-Fixture 0x8664 0x10b)
Expect-Rejected (New-Fixture 0x1234 0x20b)
Write-Output "$checks PE inspection checks passed"
