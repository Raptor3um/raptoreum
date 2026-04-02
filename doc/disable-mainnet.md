# Disable Mainnet Feature

## Overview

The `--disable-mainnet` configure flag allows building Raptoreum binaries that default to testnet mode instead of mainnet. This is useful for development, testing, and non-production builds.

## Usage

### Manual Build

When building manually, you can disable mainnet by passing the flag to configure:

```bash
./autogen.sh
./configure --disable-mainnet
make
```

This will build binaries that:
- Default to testnet mode
- Cannot be started in mainnet mode (the -testnet flag is forced)
- Prevent accidental connection to the production network

### Automated Builds (GitHub Actions)

The build workflow automatically applies `--disable-mainnet` for:

1. **Snapshot builds**: Triggered by:
   - Pull requests
   - Pushes to `develop` branch
   - Pushes to feature branches (`ft/*`)
   - Pushes to bug fix branches (`bug/*`)

2. **Release candidate builds**: Triggered by:
   - Pushes to release branches (`release/*`)

3. **Production builds**: The flag is NOT applied for:
   - Pushes to `master` branch (production releases)

### Version Types

The build type is determined from `build.properties`:

- `snapshot-version`: Development builds with mainnet disabled
- `candidate-version`: Release candidate builds with mainnet disabled
- `release-version`: Production builds with mainnet enabled

## Configuration

The workflow is configured in `.github/workflows/build.yaml`:

1. The `get-version` job determines the build type and sets the `disable-mainnet` output
2. Build jobs (`build-linux`, `build-windows`) receive the flag via `needs.get-version.outputs.disable-mainnet`
3. The flag is passed to the `build-binaries` action via the `configure-flags` input

## Verification

To verify a build has mainnet disabled:

```bash
# Try to start the daemon (it should default to testnet)
./raptoreumd

# Check the data directory - it should use testnet3
ls ~/.raptoreumcore/testnet3/

# The daemon will not accept connections from mainnet nodes
```

## Security Note

Builds with `--disable-mainnet` **cannot** be used for mainnet operations. This is a compile-time restriction that prevents the binary from ever connecting to or operating on the mainnet network.

