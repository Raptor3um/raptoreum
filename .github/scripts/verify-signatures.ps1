# Fail the run unless every executable carries a valid signature.
#
# Signing tools can report success while leaving a file untouched, so the
# signatures are read back rather than trusted.

param(
    [Parameter(Mandatory = $true)][string]$StagingDir
)

$ErrorActionPreference = "Stop"

$exes = Get-ChildItem $StagingDir -Recurse -Filter *.exe
if ($exes.Count -eq 0) {
    throw "no .exe found under $StagingDir - nothing was signed"
}

$bad = @()
foreach ($exe in $exes) {
    $sig = Get-AuthenticodeSignature $exe.FullName
    $subject = if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { "<none>" }
    Write-Host "$($exe.Name): $($sig.Status) $subject"
    if ($sig.Status -ne "Valid") {
        $bad += "$($exe.Name) [$($sig.Status)]"
    }
    elseif (-not $sig.TimeStamperCertificate) {
        # An un-timestamped signature stops being valid the day the certificate
        # expires, so treat it as a failure rather than shipping it.
        $bad += "$($exe.Name) [not timestamped]"
    }
}

if ($bad.Count -gt 0) {
    throw "unsigned, invalid or un-timestamped: $($bad -join ', ')"
}

Write-Host "all $($exes.Count) executables are signed and timestamped"
