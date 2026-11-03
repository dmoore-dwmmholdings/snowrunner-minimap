# Not yet run on Windows. Reads file metadata and computes a hash; never launches
# or attaches to SnowRunner. Writes only inside this repository's ignored build/.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameExe,
    [Parameter(Mandatory=$true)][ValidateSet('Steam','Epic','MicrosoftStore','Other')][string]$Storefront,
    [Parameter(Mandatory=$true)][string]$DisplayedVersion,
    [Parameter(Mandatory=$true)][ValidateSet('DX11','DX12','Unknown')][string]$GraphicsApi,
    [Parameter(Mandatory=$true)][ValidateSet('Fullscreen','Borderless','Windowed','Unknown')][string]$DisplayMode
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($DisplayedVersion)) { throw 'DisplayedVersion must not be blank.' }
$file = Get-Item -LiteralPath $GameExe
if ($file.PSIsContainer -or $file.Name -ine 'SnowRunner.exe') { throw 'Provide the actual SnowRunner.exe file.' }
$identity = [ordered]@{
    schema = 'snowrunner-minimap-preflight-v1'
    verification = 'metadata-only; no runtime compatibility verified'
    storefront = $Storefront
    displayedVersion = $DisplayedVersion
    fileVersion = $file.VersionInfo.FileVersion
    executableSha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    executableBytes = $file.Length
    architecture = 'unverified; inspect the executable on Windows'
    graphicsApi = $GraphicsApi
    displayMode = $DisplayMode
}
$buildDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'build'
$null = New-Item -ItemType Directory -Path $buildDir -Force
$output = Join-Path $buildDir 'windows-identity.local.json'
$json = $identity | ConvertTo-Json
[System.IO.File]::WriteAllText($output, $json, [System.Text.UTF8Encoding]::new($false))
Write-Output $output
