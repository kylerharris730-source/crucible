$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$game = Join-Path $build 'cinderlift.exe'
$launcher = Join-Path $build 'cinderlift-launcher.exe'
$temp = Join-Path $build 'launcher-test-artifacts'

if (-not (Test-Path -LiteralPath $game) -or -not (Test-Path -LiteralPath $launcher)) {
    throw 'Build the game and launcher before running launcher_test.ps1'
}

if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $manifest = Join-Path $temp 'manifest.txt'
    & (Join-Path $root 'scripts\make_release_manifest.ps1') `
        -Tag v0.0.0-test -Repository kylerharris730-source/crucible `
        -Game $game -Launcher $launcher -Output $manifest

    $ok = Start-Process -FilePath $launcher `
        -ArgumentList '--verify-manifest', "`"$manifest`"", "`"$game`"", "`"$launcher`"" `
        -WorkingDirectory $root -WindowStyle Hidden -Wait -PassThru
    if ($ok.ExitCode -ne 0) { throw "valid manifest failed with $($ok.ExitCode)" }

    $bad = Start-Process -FilePath $launcher `
        -ArgumentList '--verify-manifest', "`"$manifest`"", "`"$launcher`"", "`"$launcher`"" `
        -WorkingDirectory $root -WindowStyle Hidden -Wait -PassThru
    if ($bad.ExitCode -ne 11) { throw "mismatched game was not rejected: $($bad.ExitCode)" }

    $helper = Join-Path $temp 'helper.exe'
    $target = Join-Path $temp 'target.exe'
    Copy-Item -LiteralPath $launcher -Destination $helper
    Copy-Item -LiteralPath $game -Destination $target
    $replace = Start-Process -FilePath $helper `
        -ArgumentList '--replace-only', "`"$target`"", '0' `
        -WorkingDirectory $temp -WindowStyle Hidden -Wait -PassThru
    if ($replace.ExitCode -ne 0) { throw "self replacement failed: $($replace.ExitCode)" }
    if ((Get-FileHash $helper -Algorithm SHA256).Hash -ne
        (Get-FileHash $target -Algorithm SHA256).Hash) { throw 'replacement installed the wrong bytes' }

    Write-Output 'launcher manifest, rejection, and self-replacement checks passed'
}
finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
