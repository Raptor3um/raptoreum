# Expand the archives produced by the build workflow into one directory per
# archive, ready for signing, and fail early if there is nothing to sign.

param(
    [Parameter(Mandatory = $true)][string]$InputDir,
    [Parameter(Mandatory = $true)][string]$StagingDir
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $StagingDir | Out-Null

$zips = Get-ChildItem $InputDir -Filter *.zip
if ($zips.Count -eq 0) {
    throw "no .zip archives found in $InputDir - check the run id and version"
}

$zips | ForEach-Object {
    $dest = Join-Path $StagingDir $_.BaseName
    Expand-Archive -Path $_.FullName -DestinationPath $dest
    Write-Host "unpacked $($_.Name) -> $dest"
}

$exes = Get-ChildItem $StagingDir -Recurse -Filter *.exe
if ($exes.Count -eq 0) {
    throw "no .exe found under $StagingDir - nothing to sign"
}
Write-Host "$($exes.Count) executables to sign:"
$exes | ForEach-Object { Write-Host "  $($_.Directory.Name)/$($_.Name)" }
