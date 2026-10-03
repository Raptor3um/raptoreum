# Repackage signed binaries and regenerate the checksum files.
#
# Called by .github/workflows/sign-windows.yaml after the executables in
# -StagingDir have been signed. Each subdirectory of the staging directory
# corresponds to one of the archives that came out of the build workflow and is
# zipped back up under the same name.

param(
    [Parameter(Mandatory = $true)][string]$StagingDir,
    [Parameter(Mandatory = $true)][string]$OutputDir
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Get-ChildItem $StagingDir -Directory | ForEach-Object {
    $name = $_.Name
    $dir = $_.FullName

    # The checksums shipped inside the archive describe the unsigned files, so
    # they have to be recomputed: signing changes every executable.
    $checksums = Join-Path $dir "checksums.txt"
    if (Test-Path $checksums) { Remove-Item $checksums }

    $lines = @("sha256:", "------------------------------------")
    Get-ChildItem $dir -File | Sort-Object Name | ForEach-Object {
        $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
        $lines += "$hash  $($_.Name)"
    }
    $lines += "------------------------------------"
    Set-Content -Path $checksums -Value $lines -Encoding utf8

    $zip = Join-Path $OutputDir "$name.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip
    Write-Host "packed $zip"
}

# A single checksum file covering the archives themselves.
$manifest = Join-Path $OutputDir "checksums.txt"
$lines = @()
Get-ChildItem $OutputDir -Filter *.zip | Sort-Object Name | ForEach-Object {
    $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    $lines += "$hash  $($_.Name)"
}
Set-Content -Path $manifest -Value $lines -Encoding utf8
Get-Content $manifest | Write-Host
