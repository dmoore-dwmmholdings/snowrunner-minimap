# Reads file metadata, PE architecture and a hash; never launches
# or attaches to SnowRunner. Writes only inside this repository's ignored build/.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameExe,
    [Parameter(Mandatory=$true)][ValidateSet('Steam','Epic','MicrosoftStore','Other')][string]$Storefront,
    [string]$DisplayedVersion = 'Unknown',
    [ValidateSet('DX11','DX12','Unknown')][string]$GraphicsApi = 'Unknown',
    [ValidateSet('Fullscreen','Borderless','Windowed','Unknown')][string]$DisplayMode = 'Unknown',
    [ValidatePattern('^(Unknown|[0-9]+)$')][string]$SteamBuildId = 'Unknown'
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($DisplayedVersion)) { throw 'DisplayedVersion must not be blank.' }
$file = Get-Item -LiteralPath $GameExe
if ($file.PSIsContainer -or $file.Name -ine 'SnowRunner.exe') { throw 'Provide the actual SnowRunner.exe file.' }
Import-Module (Join-Path $PSScriptRoot 'PeImage.psm1') -Force
# Keep one read handle for architecture and hash; deny concurrent writes while it is open.
$stream = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    $architecture = Get-PeArchitecture -Stream $stream
    $stream.Position = 0
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $fileVersion = $file.VersionInfo.FileVersion
    $productVersion = $file.VersionInfo.ProductVersion
    $length = $stream.Length
} finally { $stream.Dispose() }
$identity = [ordered]@{
    schema = 'snowrunner-minimap-preflight-v2'
    verification = 'metadata-only; no runtime compatibility verified'
    storefront = $Storefront
    displayedVersion = $DisplayedVersion
    fileVersion = $fileVersion
    productVersion = $productVersion
    steamBuildId = $SteamBuildId
    executableSha256 = $digest
    executableBytes = $length
    architecture = $architecture
    architectureEvidence = 'PE machine and optional header; not runtime execution'
    graphicsApi = $GraphicsApi
    displayMode = $DisplayMode
    runtimeFieldsEvidence = 'Caller reported; Unknown by default. Not observed by this collector.'
}
$buildDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'build'
$null = New-Item -ItemType Directory -Path $buildDir -Force
$output = Join-Path $buildDir 'windows-identity.local.json'
$json = $identity | ConvertTo-Json
[System.IO.File]::WriteAllText($output, $json, [System.Text.UTF8Encoding]::new($false))
Write-Output $output
