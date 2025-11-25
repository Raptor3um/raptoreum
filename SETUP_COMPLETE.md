- Review troubleshooting sections
- Open a GitHub issue if you need help

---

**🎉 Happy Signing! Your Windows releases will now be properly code-signed and trusted by users.**

**Document Location:** All files are in the repository at:
- Main: `https://github.com/Raptor3um/raptoreum/tree/bug/build-fix`

**Last Updated:** November 25, 2024
# Windows Code Signing Setup - Complete! ✅

## 🎉 What Was Created

I've set up a complete Windows code signing workflow for your Comodo USB token certificate. Here's everything that was created:

### 📚 Documentation (4 files)

1. **SIGNING_README.md** (Root directory) - Master index
   - Links to all documentation
   - Quick navigation
   - Setup checklist
   - **👉 START HERE**

2. **.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md** - Complete guide
   - 8 comprehensive parts
   - Step-by-step setup instructions
   - Troubleshooting guide
   - Security best practices
   - Advanced automation options
   - **📖 ~500 lines of detailed documentation**

3. **.github/workflows/SIGNING_QUICK_START.md** - Quick reference
   - One-page cheat sheet
   - Daily workflow commands
   - Common troubleshooting
   - Command reference card
   - **⚡ For daily use after setup**

4. **scripts/README.md** - Script documentation
   - Parameter explanations
   - Usage examples
   - Features overview

### 🛠️ Scripts (2 files)

1. **scripts/sign-raptoreum.ps1** - PowerShell signing script
   - ✅ Auto-finds signtool.exe
   - ✅ Batch signs all .exe files
   - ✅ Verifies signatures
   - ✅ Progress display
   - ✅ Error handling
   - ✅ SHA256 + RFC 3161 timestamping
   - **~400 lines of production-ready code**

2. **scripts/CERTIFICATE_INFO_TEMPLATE.md** - Certificate tracker
   - Fill out and keep private
   - Tracks expiration dates
   - Renewal checklist
   - Security incident log

### 🔒 Security

- **.gitignore updated** to exclude:
  - Certificate files (*.pfx, *.p12, *.cer, *.crt)
  - Private keys (*.key, *.pem)
  - Certificate info files
  - **Your sensitive data won't be committed**

## 📋 Your Next Steps

### Step 1: Read the Documentation (10 minutes)

```bash
# Start here
cat SIGNING_README.md

# Then read the quick start
cat .github/workflows/SIGNING_QUICK_START.md
```

### Step 2: Install Required Software (30 minutes)

**On your Windows machine:**

1. Install Windows SDK
   - Download: https://developer.microsoft.com/windows/downloads/windows-sdk/
   - Or install via Visual Studio

2. Install SafeNet Authentication Client
   - Download: https://supportportal.thalesgroup.com/csm
   - Search for "SafeNet Authentication Client"

3. Test USB token
   ```powershell
   certutil -scinfo
   ```

### Step 3: Find Your Certificate Thumbprint (2 minutes)

```powershell
# Insert USB token, then run:
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-List Subject, Thumbprint, NotAfter
```

**Copy the Thumbprint value** (40-character hex string) - you'll need this!

### Step 4: Test the Script (5 minutes)

```powershell
# Clone the repo to your Windows machine (if not already)
git clone https://github.com/Raptor3um/raptoreum
cd raptoreum

# Create test directory
mkdir C:\TestSigning
# Put a test .exe file there

# Run the script
.\scripts\sign-raptoreum.ps1 `
    -InputPath "C:\TestSigning" `
    -CertThumbprint "YOUR_THUMBPRINT_HERE" `
    -AutoFindSignTool
```

### Step 5: Create Your Certificate Info File (5 minutes)

```powershell
# Copy the template
copy scripts\CERTIFICATE_INFO_TEMPLATE.md scripts\CERTIFICATE_INFO.md

# Edit and fill it out (use notepad or any editor)
notepad scripts\CERTIFICATE_INFO.md
```

**Save this file securely** - it won't be committed to git.

## 🔄 Daily Workflow (After Setup)

When GitHub Actions completes a build:

### 1. Download Artifacts (2 minutes)
- Go to: https://github.com/Raptor3um/raptoreum/actions
- Click the successful build
- Download: `raptoreum-win-{version}.zip` and installer artifacts

### 2. Extract (1 minute)
```powershell
mkdir C:\RaptoreumSigning\v1.0.0
cd C:\RaptoreumSigning\v1.0.0
Expand-Archive -Path "$env:USERPROFILE\Downloads\raptoreum-win-*.zip" -DestinationPath ".\unsigned"
```

### 3. Sign (2 minutes)
```powershell
cd path\to\raptoreum\scripts
.\sign-raptoreum.ps1 `
    -InputPath "C:\RaptoreumSigning\v1.0.0\unsigned" `
    -CertThumbprint "YOUR_THUMBPRINT" `
    -AutoFindSignTool
```

### 4. Verify (1 minute)
```powershell
Get-AuthenticodeSignature C:\RaptoreumSigning\v1.0.0\unsigned\*.exe | Format-List Status
```

### 5. Upload to Release (2 minutes)
```powershell
# Package
cd C:\RaptoreumSigning\v1.0.0
Compress-Archive -Path "unsigned\*" -DestinationPath "raptoreum-win-signed-1.0.0.zip"

# Upload (manually or with GitHub CLI)
gh release upload v1.0.0 raptoreum-win-signed-1.0.0.zip
```

**Total time: ~10 minutes per release**

## 💡 Pro Tips

### Save Your Thumbprint
```powershell
# Add to your PowerShell profile for quick access
$env:CERT_THUMBPRINT = "YOUR_THUMBPRINT_HERE"
```

### Create a Desktop Shortcut
Create `Sign-Raptoreum.bat`:
```batch
@echo off
powershell -ExecutionPolicy Bypass -File "C:\path\to\raptoreum\scripts\sign-raptoreum.ps1" -InputPath "%1" -CertThumbprint "YOUR_THUMBPRINT" -AutoFindSignTool
pause
```

Then drag-and-drop folders onto it!

### Quick Alias
Add to PowerShell profile:
```powershell
function Sign-RTM {
    param([string]$Path)
    & "C:\path\to\raptoreum\scripts\sign-raptoreum.ps1" -InputPath $Path -CertThumbprint $env:CERT_THUMBPRINT -AutoFindSignTool
}
```

Then just: `Sign-RTM "C:\folder"`

## 📊 What You Get

### Before (Unsigned)
```
⚠️ Windows SmartScreen warning
⚠️ "Unknown publisher"
⚠️ Users hesitant to run
⚠️ Antivirus flags as suspicious
```

### After (Signed)
```
✅ Verified publisher: Your Company Name
✅ No SmartScreen warning
✅ Trusted by Windows
✅ Professional appearance
✅ Better antivirus detection
```

## 🔒 Security Features

### USB Token Advantages
- ✅ Private key **never leaves** the token
- ✅ Cannot be copied or extracted
- ✅ PIN-protected hardware
- ✅ FIPS 140-2 Level 2 certified
- ✅ Industry standard for code signing

### Script Security
- ✅ Verifies certificate before signing
- ✅ Confirms with user before proceeding
- ✅ Validates all signatures after signing
- ✅ Detailed error messages
- ✅ No certificate export required

### Repository Security
- ✅ .gitignore prevents committing certificates
- ✅ Documentation emphasizes security
- ✅ Template for tracking certificate info
- ✅ Renewal reminders included

## 📞 Support Resources

### Documentation
- **Master Index**: `SIGNING_README.md`
- **Quick Start**: `.github/workflows/SIGNING_QUICK_START.md`
- **Full Guide**: `.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md`
- **Script Docs**: `scripts/README.md`

### External Help
- **Microsoft Signtool**: https://docs.microsoft.com/en-us/windows/win32/seccrypto/signtool
- **Sectigo Support**: https://sectigo.com/support
- **SafeNet Support**: https://supportportal.thalesgroup.com/

### Troubleshooting
Quick Start guide has a troubleshooting section covering:
- Certificate not found
- signtool.exe not found
- USB token issues
- Timestamp server errors
- Access denied errors

## 🎯 Success Checklist

- [ ] Documentation read and understood
- [ ] Windows SDK installed
- [ ] SafeNet client installed
- [ ] USB token recognized
- [ ] Certificate thumbprint obtained
- [ ] Test signing successful
- [ ] Certificate info file created
- [ ] Calendar reminder set for renewal
- [ ] Team members trained (if applicable)
- [ ] First production release signed

## 📝 Files Created Summary

```
Repository Root:
├── SIGNING_README.md                          (Master index)
│
├── .github/workflows/
│   ├── WINDOWS_CODE_SIGNING_GUIDE.md         (Complete guide)
│   └── SIGNING_QUICK_START.md                (Quick reference)
│
├── scripts/
│   ├── sign-raptoreum.ps1                    (Signing script)
│   ├── README.md                             (Script docs)
│   └── CERTIFICATE_INFO_TEMPLATE.md          (Tracking template)
│
└── .gitignore                                 (Updated with cert exclusions)
```

**Total:** 7 files created/updated  
**Lines of code/docs:** ~2000+  
**Commit:** ✅ Pushed to repository

## 🚀 You're All Set!

Everything is committed and pushed. You now have:

1. ✅ Complete documentation
2. ✅ Production-ready signing script
3. ✅ Security best practices
4. ✅ Certificate tracking template
5. ✅ Quick reference guides
6. ✅ Troubleshooting resources

### What's Next?

1. **Install the software** on your Windows signing machine
2. **Get your certificate thumbprint**
3. **Run a test signing** with one file
4. **Sign your next release** using the daily workflow
5. **Share the quick start guide** with your team

### Questions?

- Check the documentation first (especially SIGNING_QUICK_START.md)

