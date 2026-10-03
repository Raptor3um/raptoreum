#!/usr/bin/env python3
"""Run with python3 scripts/tests/test_sign_raptoreum.py [path/to/pwsh]."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    powershell = sys.argv[1] if len(sys.argv) > 1 else "pwsh"
    script = Path(__file__).resolve().parents[1] / "sign-raptoreum.ps1"
    with tempfile.TemporaryDirectory(prefix="raptoreum-signing-test-") as directory:
        temporary = Path(directory)
        binaries = temporary / "binaries"
        binaries.mkdir()
        (binaries / "raptoreum.exe").write_bytes(b"unsigned test placeholder")
        calls = temporary / "calls.txt"
        tool = temporary / "signtool.ps1"
        tool.write_text("""param([Parameter(ValueFromRemainingArguments=$true)][object[]]$ToolArguments)
Add-Content -Path $env:SIGNING_TEST_CALLS -Value $ToolArguments[0]
$global:LASTEXITCODE = [int]$env:SIGNING_TEST_EXIT
""")
        wrapper = temporary / "run.ps1"
        wrapper.write_text("""param([switch]$SkipConfirmation)
$ErrorActionPreference = 'Stop'
function Get-ChildItem {
    param([string]$Path, [string]$Filter, [switch]$Recurse)
    if ($Path -eq 'Cert:\\CurrentUser\\My') {
        if ($env:SIGNING_TEST_MISSING_CERT -ne '1') {
            [pscustomobject]@{Thumbprint=('a'*40); Subject='Test certificate'; NotAfter=[datetime]'2030-01-01'}
        }
    } else {
        Microsoft.PowerShell.Management\\Get-ChildItem -Path $Path -Filter $Filter -Recurse:$Recurse
    }
}
function Read-Host { throw 'manual-confirmation-required' }
& $env:SIGNING_TEST_SCRIPT -InputPath $env:SIGNING_TEST_BINARIES -CertThumbprint ('a'*40) -SignToolPath $env:SIGNING_TEST_TOOL -SkipConfirmation:$SkipConfirmation
exit $LASTEXITCODE
""")
        environment = dict(os.environ, SIGNING_TEST_SCRIPT=str(script),
                           SIGNING_TEST_BINARIES=str(binaries), SIGNING_TEST_TOOL=str(tool),
                           SIGNING_TEST_CALLS=str(calls))

        for case, skip, tool_exit, missing in (
                ("unattended", True, 0, False),
                ("manual", False, 0, False),
                ("signing failure", True, 7, False),
                ("missing certificate", True, 0, True)):
            calls.unlink(missing_ok=True)
            environment.update(SIGNING_TEST_EXIT=str(tool_exit),
                               SIGNING_TEST_MISSING_CERT=str(int(missing)))
            command = [powershell, "-NoProfile", "-NonInteractive", "-File", str(wrapper)]
            if skip:
                command.append("-SkipConfirmation")
            result = subprocess.run(command, env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            invoked = calls.read_text().splitlines() if calls.exists() else []
            if case == "unattended":
                assert result.returncode == 0, output
                assert invoked == ["sign", "verify"], invoked
            else:
                assert result.returncode != 0, output
                assert invoked == (["sign"] if tool_exit else []), invoked
                if case == "manual":
                    assert "manual-confirmation-required" in output, output
                if missing:
                    assert "Certificate not found" in output, output
            print(f"PASS: {case}")


if __name__ == "__main__":
    main()
