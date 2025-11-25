# Code Signing Documentation Index

Complete documentation for signing Raptoreum Windows binaries with Comodo USB token certificate.

## 📚 Documentation Files

### Getting Started

1. **[SIGNING_QUICK_START.md](.github/workflows/SIGNING_QUICK_START.md)** ⭐ **START HERE**
   - One-page quick reference
   - Daily workflow
   - Common commands
   - Troubleshooting

2. **[WINDOWS_CODE_SIGNING_GUIDE.md](.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md)**
   - Complete setup instructions
   - Detailed explanations
   - Security best practices
   - Advanced scenarios

### Scripts and Tools

3. **[sign-raptoreum.ps1](scripts/sign-raptoreum.ps1)**
   - PowerShell signing script
   - Automatic detection
   - Batch signing
   - Verification

4. **[scripts/README.md](scripts/README.md)**
   - Script documentation
   - Parameters and options
   - Usage examples

### Templates

5. **[CERTIFICATE_INFO_TEMPLATE.md](scripts/CERTIFICATE_INFO_TEMPLATE.md)**
   - Certificate tracking template
   - Renewal checklist
   - Security incident log
   - **⚠️ Keep private - not committed to git**

## 🎯 Quick Navigation

### I want to...

**Set up signing for the first time**
→ [WINDOWS_CODE_SIGNING_GUIDE.md](.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md) - Part 1

**Sign binaries after a build**
→ [SIGNING_QUICK_START.md](.github/workflows/SIGNING_QUICK_START.md) - Daily Workflow

**Understand the signing script**
→ [scripts/README.md](scripts/README.md)

**Track my certificate details**
→ Copy [CERTIFICATE_INFO_TEMPLATE.md](scripts/CERTIFICATE_INFO_TEMPLATE.md) to `CERTIFICATE_INFO.md` and fill it out

**Troubleshoot an issue**
→ [SIGNING_QUICK_START.md](.github/workflows/SIGNING_QUICK_START.md#-troubleshooting)

**Automate the process**
→ [WINDOWS_CODE_SIGNING_GUIDE.md](.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md) - Part 5

## 📋 Complete Workflow

### Pre-requisites (One-Time Setup)

1. ✅ Install Windows SDK
2. ✅ Install SafeNet Authentication Client  
3. ✅ Get certificate thumbprint
4. ✅ Test USB token
5. ✅ Download signing script

**Time**: ~30 minutes  
**Guide**: [WINDOWS_CODE_SIGNING_GUIDE.md](.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md) - Parts 1-2

### Daily Workflow (After Each Build)

1. **Build completes** in GitHub Actions (~30-60 min)
2. **Download artifacts** from GitHub Actions (~2 min)
3. **Extract files** to signing workspace (~1 min)
4. **Run signing script** (~2 min)
5. **Verify signatures** (~1 min)
6. **Upload to release** (~2 min)

**Total Time**: ~10 minutes  
**Guide**: [SIGNING_QUICK_START.md](.github/workflows/SIGNING_QUICK_START.md)

## 🔐 Security Overview

### Why USB Token?

✅ **Private key never leaves the token**  
✅ **Cannot be extracted or copied**  
✅ **PIN-protected hardware**  
✅ **Industry best practice**  

### Why Not GitHub Actions?

GitHub Actions cannot sign because:
- Certificate is on physical USB token
- Private key cannot be exported
- Requires human interaction (PIN entry)

**Result**: Signing must be done locally on Windows machine with USB token

## 🛠️ Tools Required

| Tool | Purpose | Download |
|------|---------|----------|
| **Windows SDK** | Provides signtool.exe | [Download](https://developer.microsoft.com/windows/downloads/windows-sdk/) |
| **SafeNet Client** | USB token driver | [Download](https://supportportal.thalesgroup.com/csm) |
| **PowerShell** | Run signing script | Built into Windows |
| **GitHub CLI** (optional) | Automate uploads | [Download](https://cli.github.com/) |

## 📊 File Overview

```
raptoreum/
├── .github/
│   └── workflows/
│       ├── WINDOWS_CODE_SIGNING_GUIDE.md    ← Full guide (comprehensive)
│       ├── SIGNING_QUICK_START.md           ← Quick reference (daily use)
│       └── build.yaml                       ← GitHub Actions workflow
│
├── scripts/
│   ├── sign-raptoreum.ps1                   ← Signing script (PowerShell)
│   ├── README.md                            ← Script documentation
│   └── CERTIFICATE_INFO_TEMPLATE.md         ← Certificate tracking template
│
└── .gitignore                               ← Excludes certificates (security)
```

## ⚡ Quick Commands

### One-Time Setup
```powershell
# Find your certificate thumbprint
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-List Thumbprint
```

### Daily Signing
```powershell
# Sign all executables
.\scripts\sign-raptoreum.ps1 `
    -InputPath "C:\RaptoreumSigning\unsigned" `
    -CertThumbprint "YOUR_THUMBPRINT" `
    -AutoFindSignTool
```

### Verification
```powershell
# Verify signature
Get-AuthenticodeSignature file.exe | Format-List Status
```

## 🆘 Troubleshooting Quick Links

| Issue | Solution |
|-------|----------|
| Certificate not found | [Quick Start - Troubleshooting](SIGNING_QUICK_START.md#-troubleshooting) |
| signtool not found | Use `-AutoFindSignTool` parameter |
| USB token not recognized | Install/restart SafeNet client |
| Timestamp timeout | Try backup server in guide |
| Access denied | Run as Administrator |
| PIN locked | Contact Sectigo support |

## 📅 Maintenance

### Monthly
- [ ] Verify USB token works
- [ ] Check certificate expiration date
- [ ] Test backup certificate (if exists)

### Before Expiration (30 days)
- [ ] Initiate certificate renewal
- [ ] Test new certificate
- [ ] Update thumbprint in scripts
- [ ] Update documentation

### After Renewal
- [ ] Install new certificate
- [ ] Test signing with new cert
- [ ] Update all scripts
- [ ] Archive old certificate info

## 🔗 External Resources

- **Microsoft Signtool**: https://docs.microsoft.com/en-us/windows/win32/seccrypto/signtool
- **Sectigo Support**: https://sectigo.com/support
- **SafeNet Support**: https://supportportal.thalesgroup.com/
- **Windows SDK**: https://developer.microsoft.com/windows/downloads/windows-sdk/

## 📞 Support Contacts

### Technical Issues
- Check documentation first
- Review troubleshooting sections
- Open GitHub issue with error details

### Certificate Issues
- **Sectigo/Comodo**: https://sectigo.com/support
- Have certificate serial number ready
- Have USB token serial number ready

### USB Token Issues
- **Thales/SafeNet**: https://supportportal.thalesgroup.com/
- Check token is properly inserted
- Verify driver installation

## ✅ Setup Checklist

Use this checklist to track your progress:

### Initial Setup
- [ ] Read [WINDOWS_CODE_SIGNING_GUIDE.md](.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md)
- [ ] Install Windows SDK
- [ ] Install SafeNet Authentication Client
- [ ] Test USB token recognition
- [ ] Find certificate thumbprint
- [ ] Save thumbprint securely
- [ ] Download signing script
- [ ] Test signing with one file
- [ ] Copy certificate template
- [ ] Fill out certificate information
- [ ] Store certificate info securely

### First Production Signing
- [ ] Download build artifacts
- [ ] Extract to workspace
- [ ] Run signing script
- [ ] Verify all signatures valid
- [ ] Package signed binaries
- [ ] Upload to GitHub release
- [ ] Test download and verify signature
- [ ] Document process for next time

### Ongoing Maintenance
- [ ] Set calendar reminder for renewal
- [ ] Create certificate backup
- [ ] Document any issues encountered
- [ ] Update scripts if needed
- [ ] Share knowledge with team

## 💡 Best Practices

1. **Always verify signatures** after signing
2. **Test on clean system** before public release
3. **Keep USB token secure** when not in use
4. **Use strong PIN** on token
5. **Monitor expiration** proactively
6. **Document your process** for consistency
7. **Test backups** regularly

## 🎓 Learning Path

**Day 1**: Setup (2-3 hours)
- Read quick start guide
- Install required software
- Test USB token
- Run first signing

**Day 2**: Practice (1 hour)
- Sign test build
- Verify signatures
- Try troubleshooting steps

**Week 1**: Regular Use
- Sign each build
- Refine your workflow
- Create shortcuts/aliases

**Month 1**: Mastery
- Automate repetitive tasks
- Help others on team
- Improve documentation

## 📝 Version History

| Date | Version | Changes |
|------|---------|---------|
| 2024-11-25 | 1.0.0 | Initial documentation created |

## 🤝 Contributing

If you find issues or improvements:
1. Update relevant documentation
2. Test your changes
3. Submit pull request
4. Help others in issues

---

**📖 Start Here**: [SIGNING_QUICK_START.md](.github/workflows/SIGNING_QUICK_START.md)

**🔐 Full Guide**: [WINDOWS_CODE_SIGNING_GUIDE.md](.github/workflows/WINDOWS_CODE_SIGNING_GUIDE.md)

**💻 Script Docs**: [scripts/README.md](scripts/README.md)

