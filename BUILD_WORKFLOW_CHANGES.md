# Build Workflow Changes - Disable Mainnet for Snapshot and Candidate Builds

## Summary

Modified `.github/workflows/build.yaml` to automatically apply the `--disable-mainnet` configure flag for snapshot and release candidate builds. This ensures that only production builds from the `master` branch can connect to mainnet.

## Changes Made

### 1. Modified `get-version` Job

**File:** `.github/workflows/build.yaml`

**Changes:**
- Added `disable-mainnet` output to the job
- Added logic to set `disable_mainnet="--disable-mainnet"` for:
  - Snapshot builds (pull requests, develop, ft/*, bug/* branches)
  - Release candidate builds (release/* branches)
- Production builds on master branch do NOT get the flag
- Added debug output to show the flag value

### 2. Updated Build Jobs

**Modified sections:**
- `build-linux` job - Both release and debug builds
- `build-windows` job - Release build

**Changes:**
- Added `configure-flags: ${{ needs.get-version.outputs.disable-mainnet }}` to all `build-binaries` action calls
- This passes the flag through to the configure command

### 3. Documentation

**Created:** `doc/disable-mainnet.md`

Comprehensive documentation covering:
- Overview of the feature
- Manual build instructions
- Automated build behavior
- Version type explanations
- Configuration details
- Verification steps
- Security notes

### 4. Test Script

**Created:** `test_disable_mainnet_workflow.sh`

Tests all scenarios:
- ✓ Develop branch → sets --disable-mainnet
- ✓ Release branch → sets --disable-mainnet  
- ✓ Master branch → does NOT set flag
- ✓ Feature branch → sets --disable-mainnet
- ✓ Pull request → sets --disable-mainnet

All tests pass successfully.

## Build Behavior

| Branch/Trigger | Version Type | Disable Mainnet? |
|----------------|--------------|------------------|
| `master` | Release | ❌ No |
| `release/*` | Candidate | ✅ Yes |
| `develop` | Snapshot | ✅ Yes |
| `ft/*` | Snapshot | ✅ Yes |
| `bug/*` | Snapshot | ✅ Yes |
| Pull Requests | Snapshot | ✅ Yes |

## How It Works

1. The `get-version` job determines the build type based on branch/event
2. It sets the `disable-mainnet` output to either `--disable-mainnet` or empty string
3. Build jobs (`build-linux`, `build-windows`) reference this output via `needs.get-version.outputs.disable-mainnet`
4. The flag is passed to the `build-binaries` action's `configure-flags` input
5. The action includes this in the `./configure` command

## Impact

- **Snapshot builds** (development/testing): Cannot connect to mainnet, safer for testing
- **Candidate builds** (release candidates): Cannot connect to mainnet, safer for pre-release testing
- **Release builds** (production): Can connect to mainnet as expected

This provides an additional safety layer preventing accidental use of test/development builds on the production network.

## Verification

To verify the workflow file syntax:
```bash
# No YAML syntax errors
```

To test the logic:
```bash
./test_disable_mainnet_workflow.sh
```

All tests pass! ✅

