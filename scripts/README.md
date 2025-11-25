# Raptoreum Scripts

This directory contains utility scripts for building, signing, and releasing Raptoreum binaries.

## 📁 Contents

### Code Signing

- **`sign-raptoreum.ps1`** - Windows code signing script for Comodo USB token certificates

## 🔐 Windows Code Signing

The `sign-raptoreum.ps1` script signs Windows executables using a hardware USB token (Comodo certificate).

### Prerequisites

1. Windows SDK (for signtool.exe)
2. SafeNet Authentication Client (USB token driver)
3. Comodo Code Signing Certificate on USB token

### Quick Usage

```powershell
.\sign-raptoreum.ps1 `
    -InputPath "C:\path\to\unsigned\binaries" `
    -CertThumbprint "YOUR_CERTIFICATE_THUMBPRINT" `
    -AutoFindSignTool
```

### Parameters

| Parameter | Required | Description |
|-----------|----------|-------------|
| `-InputPath` | Yes | Directory containing .exe files to sign |
| `-CertThumbprint` | Yes | SHA1 thumbprint of your code signing certificate |
| `-SignToolPath` | No | Path to signtool.exe (default: auto-detected) |
| `-AutoFindSignTool` | No | Automatically search for signtool.exe |
| `-TimestampServer` | No | Timestamp server URL (default: Comodo) |
| `-Description` | No | Description in signature (default: "Raptoreum Cryptocurrency Wallet") |
| `-DescriptionUrl` | No | URL in signature (default: "https://raptoreum.com") |

### Finding Your Certificate Thumbprint

```powershell
# List all code signing certificates
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-List Subject, Thumbprint, NotAfter
```

### Example: Complete Workflow

```powershell
# 1. Create workspace
mkdir C:\RaptoreumSigning
cd C:\RaptoreumSigning

# 2. Download and extract GitHub Actions artifacts
Expand-Archive -Path "Downloads\raptoreum-win-1.0.0.zip" -DestinationPath ".\unsigned"

# 3. Sign all executables
cd path\to\raptoreum\scripts
.\sign-raptoreum.ps1 `
    -InputPath "C:\RaptoreumSigning\unsigned" `
    -CertThumbprint "A1B2C3D4E5F6..." `
    -AutoFindSignTool

# 4. Verify signatures
Get-AuthenticodeSignature C:\RaptoreumSigning\unsigned\*.exe | Format-List Status

# 5. Package signed binaries
cd C:\RaptoreumSigning
Compress-Archive -Path "unsigned\*" -DestinationPath "raptoreum-win-signed-1.0.0.zip"
```

### Features

✅ **Automatic Discovery** - Finds signtool.exe automatically  
✅ **Batch Signing** - Signs all .exe files in directory recursively  
✅ **Verification** - Verifies signatures after signing  
✅ **Error Handling** - Clear error messages and troubleshooting  
✅ **Progress Display** - Shows signing progress for each file  
✅ **Timestamping** - Includes timestamp for long-term validity  
✅ **Security** - Uses USB token (private key never exported)  

### Troubleshooting

#### Certificate Not Found
```powershell
# Check if USB token is recognized
certutil -scinfo

# List available certificates
certutil -store My
```

#### signtool.exe Not Found
```powershell
# Search for signtool.exe
Get-ChildItem "C:\Program Files*" -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue

# Or use auto-find
.\sign-raptoreum.ps1 ... -AutoFindSignTool
```

#### Timestamp Server Timeout
```powershell
# Try alternative timestamp server
.\sign-raptoreum.ps1 ... -TimestampServer "http://timestamp.sectigo.com"
```

### Security Best Practices

1. **Protect USB Token**
   - Store securely when not in use
   - Use strong PIN
   - Never share or clone

2. **Verify After Signing**
   - Always verify signatures
   - Check certificate details
   - Test on clean system

3. **Monitor Certificate**
   - Track expiration date
   - Renew 30 days before expiry
   - Update thumbprint after renewal

## 📚 Documentation

For complete setup instructions and troubleshooting:

- **[Quick Start Guide](../.github/workflows/SIGNING_QUICK_START.md)** - Fast setup and daily workflow
- **[Full Documentation](../.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md)** - Complete guide with all details

## 🔗 Related Resources

- **Signtool Documentation**: https://docs.microsoft.com/en-us/windows/win32/seccrypto/signtool
- **SafeNet Client**: https://supportportal.thalesgroup.com/csm
- **Comodo/Sectigo Support**: https://sectigo.com/support

## 🆘 Support

If you encounter issues:

1. Check the [Quick Start troubleshooting section](../.github/workflows/SIGNING_QUICK_START.md#-troubleshooting)
2. Review the [Full Guide](../.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md)
3. Open an issue on GitHub with:
   - Error message
   - PowerShell version: `$PSVersionTable.PSVersion`
   - Windows version: `winver`
   - Script parameters used (without thumbprint)

## 📝 Notes

- The signing script is designed for **post-build signing** after GitHub Actions completes
- Signing **cannot** be done in GitHub Actions because the certificate is on a physical USB token
- For automated signing, you would need to export the certificate (not recommended for security)
- The script supports both .exe files and can be extended for .dll, .msi, etc.

---

**Last Updated**: November 2024  
**Script Version**: 1.0.0  
**Tested On**: Windows 10, Windows 11, PowerShell 5.1+

