param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Game,
    [Parameter(Mandatory = $true)][string]$Launcher,
    [Parameter(Mandatory = $true)][string]$Output
)

$ErrorActionPreference = 'Stop'
$gameHash = (Get-FileHash -LiteralPath $Game -Algorithm SHA256).Hash.ToLowerInvariant()
$launcherHash = (Get-FileHash -LiteralPath $Launcher -Algorithm SHA256).Hash.ToLowerInvariant()
$base = "https://github.com/$Repository/releases/download/$Tag"
@(
    "version=$Tag"
    "game_url=$base/cinderlift.exe"
    "game_sha256=$gameHash"
    "launcher_url=$base/cinderlift-launcher.exe"
    "launcher_sha256=$launcherHash"
) | Set-Content -LiteralPath $Output -Encoding utf8
