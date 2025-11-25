# Windows Code Signing - Quick Start Guide

## 🚀 Quick Setup (First Time Only)

### 1. Install Windows SDK
Download and install: https://developer.microsoft.com/windows/downloads/windows-sdk/

### 2. Install SafeNet Authentication Client  
Download from: https://supportportal.thalesgroup.com/csm
(Search for "SafeNet Authentication Client")

### 3. Get Your Certificate Thumbprint
```powershell
# Insert USB token, then run:
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-List Subject, Thumbprint, NotAfter
```
Copy the **Thumbprint** value (40-character hex string)

## 📋 Daily Signing Workflow

### Step 1: Download from GitHub Actions
1. Go to: https://github.com/Raptor3um/raptoreum/actions
2. Click latest successful workflow
3. Download artifacts:
   - `raptoreum-win-{version}.zip`
   - `raptoreum-win-installation-{version}`

### Step 2: Extract Files
```powershell
# Create workspace
mkdir C:\RaptoreumSigning
cd C:\RaptoreumSigning

# Extract (adjust filename to match your version)
Expand-Archive -Path "$env:USERPROFILE\Downloads\raptoreum-win-*.zip" -DestinationPath ".\unsigned"
```

### Step 3: Sign Files
```powershell
# Navigate to scripts directory
cd path\to\raptoreum\scripts

# Sign (replace YOUR_THUMBPRINT with your actual thumbprint)
.\sign-raptoreum.ps1 -InputPath "C:\RaptoreumSigning\unsigned" -CertThumbprint "YOUR_THUMBPRINT" -AutoFindSignTool
```

### Step 4: Verify
```powershell
# Check signature
Get-AuthenticodeSignature C:\RaptoreumSigning\unsigned\*.exe | Format-List Status, SignerCertificate
```
Should show: `Status: Valid`

### Step 5: Package & Upload
```powershell
# Create signed package
cd C:\RaptoreumSigning
Compress-Archive -Path ".\unsigned\*" -DestinationPath ".\raptoreum-win-signed-1.0.0.zip"

# Upload to GitHub release (using GitHub CLI)
gh release upload v1.0.0 raptoreum-win-signed-1.0.0.zip
```

## 🔧 Common Commands

### Find Your Certificate
```powershell
certutil -store My
# OR
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert
```

### Find signtool.exe
```powershell
Get-ChildItem "C:\Program Files (x86)\Windows Kits" -Recurse -Filter "signtool.exe" | Select-Object FullName
```

### Verify a Signature
```powershell
signtool verify /pa /v file.exe
# OR
Get-AuthenticodeSignature file.exe | Format-List *
```

### Check USB Token
```powershell
certutil -scinfo
```

## 🎯 One-Liner (After Setup)

Save this in a file named `quick-sign.ps1`:
```powershell
param([string]$Path)
$thumbprint = "PUT_YOUR_THUMBPRINT_HERE"  # Update this once
.\sign-raptoreum.ps1 -InputPath $Path -CertThumbprint $thumbprint -AutoFindSignTool
```

Then just run:
```powershell
.\quick-sign.ps1 -Path "C:\RaptoreumSigning\unsigned"
```

## ⚠️ Troubleshooting

| Problem | Solution |
|---------|----------|
| "Certificate not found" | Insert USB token, unlock with PIN |
| "signtool not found" | Use `-AutoFindSignTool` parameter |
| "Access denied" | Run PowerShell as Administrator |
| Timestamp error | Try: `-TimestampServer "http://timestamp.sectigo.com"` |
| SafeNet error | Restart: `Restart-Service "SafeNet Authentication Service"` |

## 📝 Script Parameters

### Basic Usage
```powershell
.\sign-raptoreum.ps1 -InputPath "path\to\files" -CertThumbprint "YOUR_THUMBPRINT"
```

### Advanced Options
```powershell
.\sign-raptoreum.ps1 `
    -InputPath "C:\unsigned" `
    -CertThumbprint "A1B2C3..." `
    -AutoFindSignTool `
    -TimestampServer "http://timestamp.sectigo.com" `
    -Description "Raptoreum Wallet v2.0" `
    -DescriptionUrl "https://raptoreum.com"
```

### Auto-find signtool
```powershell
.\sign-raptoreum.ps1 -InputPath ".\files" -CertThumbprint "ABC123..." -AutoFindSignTool
```

## 🔒 Security Checklist

- [ ] USB token stored securely when not in use
- [ ] Strong PIN set on token (not default)
- [ ] Certificate expiration monitored
- [ ] Backup of certificate created and secured
- [ ] Only sign on trusted computer
- [ ] Verify signatures after signing
- [ ] Remove token after signing

## 📆 Certificate Maintenance

**Record your certificate info:**

```
Certificate Details:
-------------------
Subject Name: ________________________________
Issuer:       ________________________________
Thumbprint:   ________________________________
Issue Date:   ________________________________
Expiry Date:  ________________________________
Token S/N:    ________________________________

Next Renewal: ________________________________
```

**Set reminder 30 days before expiration!**

## 🆘 Emergency: Lost Token

If USB token is lost or damaged:

1. Contact Comodo/Sectigo immediately
2. Revoke certificate (if necessary)
3. Request replacement or new certificate
4. Update thumbprint in all scripts
5. Re-sign any recent releases

## 📚 Full Documentation

For complete details, see:
- [WINDOWS_CODE_SIGNING_GUIDE.md](WINDOWS_CODE_SIGNING_GUIDE.md) - Full guide
- [sign-raptoreum.ps1](../../scripts/sign-raptoreum.ps1) - Signing script

## 🔗 Useful Links

- **Windows SDK**: https://developer.microsoft.com/windows/downloads/windows-sdk/
- **SafeNet Client**: https://supportportal.thalesgroup.com/csm
- **GitHub CLI**: https://cli.github.com/
- **Signtool Docs**: https://docs.microsoft.com/en-us/windows/win32/seccrypto/signtool

## 💡 Pro Tips

1. **Save thumbprint in environment variable:**
   ```powershell
   $env:CERT_THUMBPRINT = "YOUR_THUMBPRINT"
   .\sign-raptoreum.ps1 -InputPath ".\files" -CertThumbprint $env:CERT_THUMBPRINT
   ```

2. **Create desktop shortcut:**
   - Target: `powershell.exe -ExecutionPolicy Bypass -File "C:\path\to\sign-raptoreum.ps1" -InputPath "C:\RaptoreumSigning\unsigned" -CertThumbprint "YOUR_THUMBPRINT"`

3. **Batch sign multiple versions:**
   ```powershell
   Get-ChildItem -Directory | ForEach-Object {
       .\sign-raptoreum.ps1 -InputPath $_.FullName -CertThumbprint "YOUR_THUMBPRINT"
   }
   ```

4. **Log signing operations:**
   ```powershell
   .\sign-raptoreum.ps1 -InputPath ".\files" -CertThumbprint "..." 2>&1 | Tee-Object -FilePath "sign-log.txt"
   ```

---

**Questions?** See the full guide: [WINDOWS_CODE_SIGNING_GUIDE.md](WINDOWS_CODE_SIGNING_GUIDE.md)

