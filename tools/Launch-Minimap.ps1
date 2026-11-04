param(
    [Parameter(Mandatory=$true)] [string] $Manifest
)
$ErrorActionPreference = 'Stop'
$manifestPath = (Resolve-Path -LiteralPath $Manifest).Path
$launcherPath = Join-Path $PSScriptRoot 'feed_launcher.py'
$pythonPath = (Get-Command pythonw.exe -ErrorAction Stop).Source
# These are separate quoted process arguments, never a string evaluated by a shell.
$launchArguments = @(('"{0}"' -f $launcherPath), ('"{0}"' -f $manifestPath), '--start')
Start-Process -FilePath $pythonPath -ArgumentList $launchArguments -WindowStyle Hidden
