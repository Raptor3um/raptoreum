# Code Signing Certificate Information

**⚠️ DO NOT COMMIT THIS FILE TO GIT - KEEP PRIVATE ⚠️**

This file is for your personal records. Store securely, ideally encrypted.

---

## Certificate Details

**Organization Name**: ________________________________

**Certificate Type**: Comodo Code Signing Certificate

**Issued By**: ________________________________

**Certificate Status**: ⬜ Active  ⬜ Expiring Soon  ⬜ Expired

---

## Certificate Identifiers

**SHA1 Thumbprint** (for signing script):
```
_________________________________________
```

**Serial Number**: ________________________________

**Subject Name**: 
```
CN=________________________________
O=________________________________
L=________________________________
S=________________________________
C=________________________________
```

---

## Important Dates

**Issue Date**: ______ / ______ / ______

**Expiration Date**: ______ / ______ / ______ ⚠️

**Renewal Due By**: ______ / ______ / ______  
*(30 days before expiration)*

**Set Calendar Reminder**: ⬜ Done

---

## Hardware Token Details

**Token Type**: ⬜ USB Token  ⬜ Smart Card  ⬜ Other: ________

**Token Model**: ________________________________

**Token Serial Number**: ________________________________

**PIN Last Changed**: ______ / ______ / ______

**PIN Strength**: ⬜ Default (⚠️)  ⬜ Custom (✓)

---

## Signing Configuration

**Primary Timestamp Server**:
```
http://timestamp.comodoca.com/authenticode
```

**Backup Timestamp Server**:
```
http://timestamp.sectigo.com
```

**signtool.exe Location**:
```
C:\Program Files (x86)\Windows Kits\10\bin\__________\x64\signtool.exe
```

**Signing Script Location**:
```
________________________________________
```

---

## Backup Information

**Certificate Backup**: ⬜ Yes  ⬜ No

**Backup Location**: ________________________________

**Backup Encrypted**: ⬜ Yes  ⬜ No

**Backup Password Location**: ________________________________

**Backup Last Tested**: ______ / ______ / ______

**Test Restore**: ⬜ Successful  ⬜ Failed  ⬜ Not Tested

---

## Quick Command Reference

### Find Certificate
```powershell
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object { $_.Thumbprint -eq "YOUR_THUMBPRINT" }
```

### Sign Files
```powershell
.\sign-raptoreum.ps1 `
    -InputPath "C:\RaptoreumSigning\unsigned" `
    -CertThumbprint "YOUR_THUMBPRINT" `
    -AutoFindSignTool
```

### Verify Signature
```powershell
Get-AuthenticodeSignature "file.exe" | Format-List *
```

---

## Contact Information

**Certificate Provider Support**:
- **Sectigo (Comodo)**: https://sectigo.com/support
- **Phone**: ________________________________
- **Email**: ________________________________

**Token Vendor Support**:
- **Thales/SafeNet**: https://supportportal.thalesgroup.com/
- **Phone**: ________________________________

**Internal Contact** (if applicable):
- **Name**: ________________________________
- **Email**: ________________________________
- **Phone**: ________________________________

---

## Renewal Process

### 90 Days Before Expiration
- [ ] Check certificate is still needed
- [ ] Verify company information is current
- [ ] Budget for renewal cost

### 60 Days Before Expiration  
- [ ] Contact Sectigo to initiate renewal
- [ ] Verify contact information
- [ ] Confirm renewal pricing

### 30 Days Before Expiration
- [ ] Complete renewal paperwork
- [ ] Submit payment
- [ ] Arrange validation if required

### After Renewal
- [ ] Receive new certificate/token
- [ ] Install and test new certificate
- [ ] Update thumbprint in scripts
- [ ] Update this document
- [ ] Test signing workflow
- [ ] Archive old certificate info

---

## Security Incidents Log

| Date | Incident | Action Taken | Status |
|------|----------|--------------|--------|
| | | | |
| | | | |
| | | | |

---

## Notes

```
________________________________________
________________________________________
________________________________________
________________________________________
________________________________________
________________________________________
```

---

## Checklist: Initial Setup Complete

- [ ] Windows SDK installed
- [ ] SafeNet Authentication Client installed
- [ ] USB token tested and working
- [ ] Certificate thumbprint identified
- [ ] Signing script downloaded and tested
- [ ] First successful signing completed
- [ ] Backup created and secured
- [ ] Calendar reminder set for renewal
- [ ] This document completed and secured

---

**Document Created**: ______ / ______ / ______

**Last Updated**: ______ / ______ / ______

**Next Review**: ______ / ______ / ______

---

**Storage Location**: ________________________________

**Access Control**: ⬜ Encrypted  ⬜ Password Protected  ⬜ Physical Security

**🔒 KEEP THIS DOCUMENT SECURE AND PRIVATE 🔒**

