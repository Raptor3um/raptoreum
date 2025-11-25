# Windows Code Signing Guide - Comodo Certificate with USB Token

This guide explains how to sign Windows binaries built by GitHub Actions using your Comodo Code Signing Certificate on a USB stick.

## Overview

Since your Comodo certificate is on a physical USB token, signing **cannot** be done in GitHub Actions. Instead, you'll:

1. GitHub Actions builds the unsigned binaries
2. Download artifacts from GitHub Actions
3. Sign them locally on your Windows machine with the USB token
4. Re-upload the signed binaries (manually or via release)

## Prerequisites

### Hardware
- ✅ Comodo Code Signing Certificate USB token
- 🖥️ Windows machine (Windows 10/11 recommended)
- 🔌 USB port for the token

### Software Required
1. **Windows SDK** (for `signtool.exe`)
2. **SafeNet Authentication Client** (driver for your USB token)
3. **PowerShell** (comes with Windows)

## Part 1: Initial Setup (One-Time)

### Step 1: Install Windows SDK

The Windows SDK includes `signtool.exe`, which is the Microsoft tool for code signing.

**Option A: Install via Visual Studio Installer**
1. Download Visual Studio Installer: https://visualstudio.microsoft.com/downloads/
2. Choose "Desktop development with C++"
3. In "Individual components", check "Windows 10 SDK" or "Windows 11 SDK"
4. Install

**Option B: Standalone SDK**
1. Download Windows SDK: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/
2. Install (you only need "Windows SDK Signing Tools for Desktop Apps")
3. Default location: `C:\Program Files (x86)\Windows Kits\10\bin\<version>\x64\signtool.exe`

### Step 2: Install SafeNet Authentication Client

Your Comodo USB token requires a driver to work with Windows.

1. Download SafeNet Authentication Client from Thales/Gemalto:
   - Visit: https://supportportal.thalesgroup.com/csm
   - Search for "SafeNet Authentication Client"
   - Download latest version (usually 10.x or newer)

2. Install the driver
3. Restart your computer

### Step 3: Verify USB Token Recognition

1. Insert your USB token
2. Enter your token PIN when prompted
3. Open PowerShell and run:
   ```powershell
   certutil -store My
   ```
4. You should see your code signing certificate listed
5. Note the certificate's SHA1 thumbprint (a long hex string)

### Step 4: Find Your Certificate Thumbprint

Run this PowerShell command to list all code signing certificates:

```powershell
Get-ChildItem -Path Cert:\CurrentUser\My -CodeSigningCert | Format-List Subject, Thumbprint, NotAfter
```

Look for your Comodo certificate and copy the **Thumbprint** value.

Example output:
```
Subject    : CN=Your Company Name, O=Your Company, L=City, S=State, C=US
Thumbprint : A1B2C3D4E5F6G7H8I9J0K1L2M3N4O5P6Q7R8S9T0
NotAfter   : 11/25/2026 11:59:59 PM
```

## Part 2: Create Signing Script

Create a PowerShell script to automate the signing process.

### Save this as `sign-raptoreum.ps1`:

```powershell
# Raptoreum Windows Code Signing Script
# Usage: .\sign-raptoreum.ps1 -InputPath "path\to\unsigned" -CertThumbprint "YOUR_THUMBPRINT"

param(
    [Parameter(Mandatory=$true)]
    [string]$InputPath,
    
    [Parameter(Mandatory=$true)]
    [string]$CertThumbprint,
    
    [string]$SignToolPath = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe",
    
    [string]$TimestampServer = "http://timestamp.comodoca.com/authenticode",
    
    [string]$Description = "Raptoreum Cryptocurrency Wallet",
    
    [string]$DescriptionUrl = "https://raptoreum.com"
)

# Check if signtool exists
if (-not (Test-Path $SignToolPath)) {
    Write-Error "signtool.exe not found at: $SignToolPath"
    Write-Host "Please update the SignToolPath parameter with the correct path"
    Write-Host "Common locations:"
    Write-Host "  - C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
    Write-Host "  - C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"
    exit 1
}

# Check if input path exists
if (-not (Test-Path $InputPath)) {
    Write-Error "Input path not found: $InputPath"
    exit 1
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Raptoreum Code Signing Tool" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Find all .exe files
$exeFiles = Get-ChildItem -Path $InputPath -Filter "*.exe" -Recurse

if ($exeFiles.Count -eq 0) {
    Write-Warning "No .exe files found in: $InputPath"
    exit 0
}

Write-Host "Found $($exeFiles.Count) executable(s) to sign:" -ForegroundColor Green
$exeFiles | ForEach-Object { Write-Host "  - $($_.Name)" -ForegroundColor Yellow }
Write-Host ""

# Prompt for USB token PIN
Write-Host "Please ensure your USB token is inserted and unlocked" -ForegroundColor Cyan
Write-Host "You may be prompted for your PIN by the SafeNet client" -ForegroundColor Cyan
Write-Host ""

$signedCount = 0
$failedCount = 0
$failedFiles = @()

foreach ($file in $exeFiles) {
    Write-Host "Signing: $($file.Name)..." -NoNewline
    
    # Build signtool command
    $arguments = @(
        "sign",
        "/sha1", $CertThumbprint,
        "/fd", "SHA256",
        "/tr", $TimestampServer,
        "/td", "SHA256",
        "/d", $Description,
        "/du", $DescriptionUrl,
        "/v",
        "`"$($file.FullName)`""
    )
    
    # Execute signtool
    $process = Start-Process -FilePath $SignToolPath -ArgumentList $arguments -Wait -NoNewWindow -PassThru
    
    if ($process.ExitCode -eq 0) {
        Write-Host " ✓ Success" -ForegroundColor Green
        $signedCount++
    } else {
        Write-Host " ✗ Failed" -ForegroundColor Red
        $failedCount++
        $failedFiles += $file.Name
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Signing Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Total files:    $($exeFiles.Count)"
Write-Host "Signed:         $signedCount" -ForegroundColor Green
Write-Host "Failed:         $failedCount" -ForegroundColor Red

if ($failedCount -gt 0) {
    Write-Host ""
    Write-Host "Failed files:" -ForegroundColor Red
    $failedFiles | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    exit 1
}

Write-Host ""
Write-Host "All files signed successfully! ✓" -ForegroundColor Green

# Verify signatures
Write-Host ""
Write-Host "Verifying signatures..." -ForegroundColor Cyan
foreach ($file in $exeFiles) {
    $arguments = @("verify", "/pa", "/v", "`"$($file.FullName)`"")
    $process = Start-Process -FilePath $SignToolPath -ArgumentList $arguments -Wait -NoNewWindow -PassThru
    
    if ($process.ExitCode -eq 0) {
        Write-Host "  ✓ $($file.Name) - Valid signature" -ForegroundColor Green
    } else {
        Write-Host "  ✗ $($file.Name) - Verification failed" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "Done! Your binaries are now signed." -ForegroundColor Green
```

## Part 3: Signing Workflow

### Daily Workflow: After GitHub Actions Build

#### Step 1: Download Artifacts from GitHub Actions

1. Go to your GitHub repository
2. Click "Actions" tab
3. Click on the latest successful workflow run
4. Scroll down to "Artifacts" section
5. Download these artifacts:
   - `raptoreum-win-{version}.zip` (stripped binaries)
   - `raptoreum-win-not_strip-{version}.zip` (debug symbols)
   - `raptoreum-win-installation-{version}` (installer)

#### Step 2: Extract the Files

```powershell
# Create a workspace folder
New-Item -Path "C:\RaptoreumSigning" -ItemType Directory -Force
cd C:\RaptoreumSigning

# Extract downloaded artifacts
Expand-Archive -Path "Downloads\raptoreum-win-1.0.0.zip" -DestinationPath ".\unsigned"
Expand-Archive -Path "Downloads\raptoreum-win-installation-1.0.0.zip" -DestinationPath ".\unsigned\installer"
```

#### Step 3: Sign the Binaries

1. Insert your USB token
2. Open PowerShell as Administrator
3. Navigate to where you saved the script:
   ```powershell
   cd C:\RaptoreumSigning
   ```

4. Run the signing script:
   ```powershell
   .\sign-raptoreum.ps1 `
       -InputPath ".\unsigned" `
       -CertThumbprint "YOUR_CERTIFICATE_THUMBPRINT_HERE"
   ```

5. Enter your USB token PIN when prompted

#### Step 4: Verify Signatures

Check that files are signed:
```powershell
Get-AuthenticodeSignature .\unsigned\*.exe | Format-List *
```

You should see:
- `Status: Valid`
- `SignerCertificate: Your certificate details`

#### Step 5: Create Signed Release Package

```powershell
# Create signed directory
New-Item -Path ".\signed" -ItemType Directory -Force

# Copy signed files
Copy-Item ".\unsigned\*.exe" -Destination ".\signed\"

# Create new zip with signed binaries
Compress-Archive -Path ".\signed\*" -DestinationPath ".\raptoreum-win-signed-1.0.0.zip"
```

#### Step 6: Upload to Release

**Option A: Manual Upload**
1. Go to GitHub Releases
2. Create new release or edit existing
3. Upload `raptoreum-win-signed-1.0.0.zip`
4. Add note: "✅ Digitally signed with Comodo certificate"

**Option B: Using GitHub CLI**
```powershell
# Install GitHub CLI: https://cli.github.com/
gh release upload v1.0.0 raptoreum-win-signed-1.0.0.zip
```

## Part 4: Troubleshooting

### Issue: "Certificate not found"

**Solution:**
1. Make sure USB token is inserted
2. Run: `certutil -store My` to verify certificate is visible
3. Double-check the thumbprint (no spaces, correct capitalization)

### Issue: "Access denied" or "Token not responding"

**Solution:**
1. Close any other applications using the token
2. Re-insert the USB token
3. Restart SafeNet Authentication Client service:
   ```powershell
   Restart-Service -Name "SafeNet Authentication Service"
   ```

### Issue: "Timestamp server error"

**Solution:**
Try different timestamp servers:
- Comodo: `http://timestamp.comodoca.com/authenticode`
- Sectigo: `http://timestamp.sectigo.com`
- DigiCert: `http://timestamp.digicert.com`

Update the script with:
```powershell
-TimestampServer "http://timestamp.sectigo.com"
```

### Issue: signtool.exe not found

**Solution:**
Find signtool.exe location:
```powershell
Get-ChildItem -Path "C:\Program Files (x86)\Windows Kits" -Recurse -Filter "signtool.exe" | Select-Object FullName
```

Update the script with the correct path.

### Issue: "The specified file is not a valid executable"

**Solution:**
- Make sure you're signing .exe files, not .zip files
- Extract archives before signing
- Ensure files are actual Windows executables

## Part 5: Advanced - Automated Signing (Optional)

If you want to automate the download and signing process:

### Create `automated-sign-release.ps1`:

```powershell
param(
    [Parameter(Mandatory=$true)]
    [string]$Version,
    
    [Parameter(Mandatory=$true)]
    [string]$CertThumbprint,
    
    [Parameter(Mandatory=$true)]
    [string]$GitHubToken
)

# Configuration
$repo = "Raptor3um/raptoreum"
$workspace = "C:\RaptoreumSigning\$Version"

# Create workspace
New-Item -Path $workspace -ItemType Directory -Force
cd $workspace

Write-Host "Downloading artifacts from GitHub..." -ForegroundColor Cyan

# Download using GitHub CLI
gh release download "v$Version" --repo $repo --pattern "*.zip"

# Extract
Write-Host "Extracting artifacts..." -ForegroundColor Cyan
Get-ChildItem -Filter "*.zip" | ForEach-Object {
    Expand-Archive -Path $_.FullName -DestinationPath ".\unsigned" -Force
}

# Sign
Write-Host "Signing binaries..." -ForegroundColor Cyan
.\sign-raptoreum.ps1 -InputPath ".\unsigned" -CertThumbprint $CertThumbprint

# Package signed files
Write-Host "Creating signed package..." -ForegroundColor Cyan
Compress-Archive -Path ".\unsigned\*.exe" -DestinationPath ".\raptoreum-win-signed-$Version.zip"

# Upload
Write-Host "Uploading to release..." -ForegroundColor Cyan
gh release upload "v$Version" ".\raptoreum-win-signed-$Version.zip" --repo $repo --clobber

Write-Host "Done! Signed binaries uploaded to release." -ForegroundColor Green
```

## Part 6: Security Best Practices

### Protect Your Certificate

1. **Never commit certificate files to Git**
   - Already ignored in `.gitignore`
   - Store USB token in secure location

2. **Use strong PIN**
   - Change default PIN on USB token
   - Use 8+ characters

3. **Keep token secure**
   - Store in locked drawer when not in use
   - Only insert when signing
   - Eject after signing

4. **Backup certificate**
   - Create encrypted backup of certificate
   - Store backup separately from token
   - Test backup restoration procedure

5. **Monitor certificate expiration**
   - Comodo certificates typically last 1-3 years
   - Renew 30 days before expiration
   - Update thumbprint in scripts after renewal

### Certificate Management

Keep track of:
- Issue date: _________________
- Expiration date: _____________
- Thumbprint: _________________
- USB token serial: ___________

## Part 7: Quick Reference Card

### One-Line Signing Command

Save your thumbprint in a variable:
```powershell
$thumbprint = "YOUR_THUMBPRINT_HERE"
```

Then sign with:
```powershell
.\sign-raptoreum.ps1 -InputPath ".\path\to\files" -CertThumbprint $thumbprint
```

### Verify a Signed File

```powershell
Get-AuthenticodeSignature file.exe | Format-List *
```

### Check Certificate in Token

```powershell
certutil -store My
```

### Find signtool.exe

```powershell
Get-ChildItem "C:\Program Files*" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue
```

## Part 8: Integration with GitHub Actions (Future)

If you eventually want to sign in GitHub Actions (requires certificate export):

⚠️ **WARNING:** This requires exporting your private key from the USB token, which reduces security. Only do this if absolutely necessary.

See: `.github/workflows/windows-signing-cloud.md` (to be created separately if needed)

## Support

### Useful Resources

- **Signtool documentation:** https://docs.microsoft.com/en-us/windows/win32/seccrypto/signtool
- **SafeNet support:** https://supportportal.thalesgroup.com/
- **Comodo/Sectigo support:** https://sectigo.com/support

### Common Commands

```powershell
# List all certificates
Get-ChildItem Cert:\CurrentUser\My

# Check if token is recognized
certutil -scinfo

# Test token
certutil -csptest

# Verify signature
signtool verify /pa /v file.exe

# View certificate details
certutil -v -store My "THUMBPRINT"
```

---

## Next Steps

1. ✅ Complete "Part 1: Initial Setup"
2. ✅ Save the PowerShell script from "Part 2"
3. ✅ Run a test signing with one binary
4. ✅ Integrate into your release process
5. ✅ Document your specific thumbprint and paths

**Questions?** Check the troubleshooting section or open an issue on GitHub.

