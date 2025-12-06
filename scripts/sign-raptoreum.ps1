# Raptoreum Windows Code Signing Script
# Usage: .\sign-raptoreum.ps1 -InputPath "path\to\unsigned" -CertThumbprint "YOUR_THUMBPRINT"
#
# This script signs all .exe files in the specified directory using your Comodo USB token certificate

param(
    [Parameter(Mandatory=$true, HelpMessage="Path to directory containing .exe files to sign")]
    [string]$InputPath,

    [Parameter(Mandatory=$true, HelpMessage="Certificate thumbprint (SHA1 hash)")]
    [string]$CertThumbprint,

    [Parameter(HelpMessage="Path to signtool.exe")]
    [string]$SignToolPath = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe",

    [Parameter(HelpMessage="Timestamp server URL")]
    [string]$TimestampServer = "http://timestamp.comodoca.com/authenticode",

    [Parameter(HelpMessage="Description embedded in signature")]
    [string]$Description = "Raptoreum Cryptocurrency Wallet",

    [Parameter(HelpMessage="URL embedded in signature")]
    [string]$DescriptionUrl = "https://raptoreum.com",

    [Parameter(HelpMessage="Automatically find signtool.exe")]
    [switch]$AutoFindSignTool
)

# Script version
$ScriptVersion = "1.0.0"

# Color functions
function Write-Header {
    param([string]$Text)
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host $Text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Text)
    Write-Host $Text -ForegroundColor Green
}

function Write-Warning {
    param([string]$Text)
    Write-Host $Text -ForegroundColor Yellow
}

function Write-Failure {
    param([string]$Text)
    Write-Host $Text -ForegroundColor Red
}

function Write-Info {
    param([string]$Text)
    Write-Host $Text -ForegroundColor Cyan
}

# Auto-find signtool.exe if requested
if ($AutoFindSignTool) {
    Write-Info "Searching for signtool.exe..."
    $signtools = Get-ChildItem -Path "C:\Program Files (x86)\Windows Kits" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
                 Where-Object { $_.FullName -match "\\x64\\" } |
                 Sort-Object -Property FullName -Descending |
                 Select-Object -First 1

    if ($signtools) {
        $SignToolPath = $signtools.FullName
        Write-Success "Found signtool.exe at: $SignToolPath"
    } else {
        Write-Failure "Could not find signtool.exe automatically"
        exit 1
    }
}

# Display header
Write-Header "Raptoreum Code Signing Tool v$ScriptVersion"

# Validate signtool.exe exists
if (-not (Test-Path $SignToolPath)) {
    Write-Failure "ERROR: signtool.exe not found at: $SignToolPath"
    Write-Host ""
    Write-Host "Please update the SignToolPath parameter or use -AutoFindSignTool" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Common locations:" -ForegroundColor Yellow
    Write-Host "  - C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
    Write-Host "  - C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"
    Write-Host "  - C:\Program Files (x86)\Windows Kits\10\bin\10.0.18362.0\x64\signtool.exe"
    Write-Host ""
    Write-Host "Or search manually with:" -ForegroundColor Yellow
    Write-Host '  Get-ChildItem "C:\Program Files*" -Recurse -Filter "signtool.exe"' -ForegroundColor Gray
    exit 1
}

# Validate input path exists
if (-not (Test-Path $InputPath)) {
    Write-Failure "ERROR: Input path not found: $InputPath"
    exit 1
}

# Validate certificate thumbprint format
if ($CertThumbprint -notmatch '^[A-Fa-f0-9]{40}$') {
    Write-Failure "ERROR: Invalid certificate thumbprint format"
    Write-Host "Thumbprint should be a 40-character hexadecimal string" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To find your thumbprint, run:" -ForegroundColor Yellow
    Write-Host "  Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-List Thumbprint" -ForegroundColor Gray
    exit 1
}

# Remove any spaces or dashes from thumbprint
$CertThumbprint = $CertThumbprint -replace '[\s-]', ''

Write-Host ""
Write-Host "Configuration:" -ForegroundColor Cyan
Write-Host "  Input Path:        $InputPath"
Write-Host "  Certificate:       $CertThumbprint"
Write-Host "  Timestamp Server:  $TimestampServer"
Write-Host "  Description:       $Description"
Write-Host "  URL:               $DescriptionUrl"
Write-Host ""

# Find all .exe files recursively
Write-Info "Scanning for executable files..."
$exeFiles = Get-ChildItem -Path $InputPath -Filter "*.exe" -Recurse

if ($exeFiles.Count -eq 0) {
    Write-Warning "No .exe files found in: $InputPath"
    Write-Host "Nothing to sign." -ForegroundColor Yellow
    exit 0
}

Write-Success "Found $($exeFiles.Count) executable(s) to sign:"
$exeFiles | ForEach-Object {
    $relativePath = $_.FullName.Replace("$InputPath\", "")
    Write-Host "  → $relativePath" -ForegroundColor Yellow
}
Write-Host ""

# Check for USB token
Write-Info "Checking for code signing certificate..."
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $CertThumbprint }

if (-not $cert) {
    Write-Failure "ERROR: Certificate not found in certificate store"
    Write-Host ""
    Write-Host "Please ensure:" -ForegroundColor Yellow
    Write-Host "  1. Your USB token is inserted"
    Write-Host "  2. SafeNet Authentication Client is installed"
    Write-Host "  3. The certificate thumbprint is correct"
    Write-Host ""
    Write-Host "Available certificates:" -ForegroundColor Cyan
    Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-Table Subject, Thumbprint, NotAfter -AutoSize
    exit 1
}

Write-Success "Certificate found: $($cert.Subject)"
Write-Host "  Valid until: $($cert.NotAfter)" -ForegroundColor Gray
Write-Host ""

# Prompt for confirmation
Write-Warning "⚠ Please ensure your USB token is unlocked"
Write-Host "You may be prompted for your PIN by the SafeNet client" -ForegroundColor Gray
Write-Host ""
$confirm = Read-Host "Ready to sign? (Y/N)"
if ($confirm -ne 'Y' -and $confirm -ne 'y') {
    Write-Host "Signing cancelled by user" -ForegroundColor Yellow
    exit 0
}

Write-Host ""
Write-Header "Signing Files"

$signedCount = 0
$failedCount = 0
$failedFiles = @()
$totalFiles = $exeFiles.Count
$currentFile = 0

foreach ($file in $exeFiles) {
    $currentFile++
    $fileName = $file.Name
    $progress = [math]::Round(($currentFile / $totalFiles) * 100)

    Write-Host "[$currentFile/$totalFiles] Signing: $fileName..." -NoNewline

    # Build signtool arguments
    $arguments = @(
        "sign",
        "/sha1", $CertThumbprint,
        "/fd", "SHA256",                    # File digest algorithm
        "/tr", $TimestampServer,            # RFC 3161 timestamp server
        "/td", "SHA256",                    # Timestamp digest algorithm
        "/d", "`"$Description`"",           # Description
        "/du", "`"$DescriptionUrl`"",       # Description URL
        "/v",                               # Verbose
        "`"$($file.FullName)`""             # File to sign
    )

    # Execute signtool
    try {
        $output = & $SignToolPath $arguments 2>&1
        $exitCode = $LASTEXITCODE

        if ($exitCode -eq 0) {
            Write-Success " ✓ Success"
            $signedCount++
        } else {
            Write-Failure " ✗ Failed (Exit code: $exitCode)"
            $failedCount++
            $failedFiles += @{
                Name = $fileName
                Error = $output
            }

            # Show error details
            if ($output) {
                $errorMsg = ($output | Out-String) -replace '\r?\n', ' '
                Write-Host "    Error: $($errorMsg.Substring(0, [Math]::Min(100, $errorMsg.Length)))..." -ForegroundColor Red
            }
        }
    } catch {
        Write-Failure " ✗ Exception"
        $failedCount++
        $failedFiles += @{
            Name = $fileName
            Error = $_.Exception.Message
        }
        Write-Host "    Exception: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# Summary
Write-Header "Signing Summary"
Write-Host "Total files:    $totalFiles"
Write-Success "Signed:         $signedCount"
if ($failedCount -gt 0) {
    Write-Failure "Failed:         $failedCount"
} else {
    Write-Host "Failed:         $failedCount"
}

if ($failedCount -gt 0) {
    Write-Host ""
    Write-Failure "Failed files:"
    $failedFiles | ForEach-Object {
        Write-Host "  × $($_.Name)" -ForegroundColor Yellow
        if ($_.Error) {
            Write-Host "    $($_.Error)" -ForegroundColor Gray
        }
    }
    Write-Host ""
    Write-Failure "⚠ Some files failed to sign!"
    exit 1
}

Write-Host ""
Write-Success "✓ All files signed successfully!"

# Verify signatures
Write-Host ""
Write-Header "Verifying Signatures"

$verifyFailedCount = 0

foreach ($file in $exeFiles) {
    Write-Host "Verifying: $($file.Name)..." -NoNewline

    $arguments = @(
        "verify",
        "/pa",                              # Use default authentication policy
        "/v",                               # Verbose
        "`"$($file.FullName)`""
    )

    try {
        $output = & $SignToolPath $arguments 2>&1
        $exitCode = $LASTEXITCODE

        if ($exitCode -eq 0) {
            Write-Success " ✓ Valid"
        } else {
            Write-Failure " ✗ Invalid"
            $verifyFailedCount++
        }
    } catch {
        Write-Failure " ✗ Error"
        $verifyFailedCount++
    }
}

Write-Host ""

if ($verifyFailedCount -eq 0) {
    Write-Header "✓ Complete - All Signatures Valid!"
    Write-Success "Your binaries are now digitally signed and ready for distribution."
    Write-Host ""
    Write-Host "Signed files location: $InputPath" -ForegroundColor Cyan
    exit 0
} else {
    Write-Failure "⚠ Warning: $verifyFailedCount signature(s) failed verification"
    exit 1
}

