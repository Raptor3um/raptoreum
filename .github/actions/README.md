# GitHub Actions Refactoring

This directory contains refactored GitHub Actions workflows and reusable composite actions to improve maintainability and reduce code duplication.

## Structure

```
.github/
├── actions/                    # Reusable composite actions
│   ├── setup-depends/         # Build and cache dependencies
│   ├── build-binaries/        # Configure and build binaries
│   ├── package-artifacts/     # Generate checksums and package
│   └── run-tests/             # Execute unit tests
└── workflows/
    ├── build.yaml             # Original workflow (legacy)
    └── build-refactored.yaml  # New refactored workflow
```

## Composite Actions

### 1. setup-depends

Handles dependency building and caching.

**Inputs:**
- `host`: Host triplet for cross-compilation (default: `x86_64-pc-linux-gnu`)
- `fallback-download-path`: Fallback URL for downloading dependencies

**Usage:**
```yaml
- uses: ./.github/actions/setup-depends
  with:
    host: x86_64-pc-linux-gnu
    fallback-download-path: https://sign.raptoreum.com/depends/
```

### 2. build-binaries

Configures and builds Raptoreum binaries with ccache support.

**Inputs:**
- `host`: Host triplet for cross-compilation
- `configure-flags`: Additional configure flags (optional)
- `build-type`: Build type - `release` or `debug` (default: `release`)

**Usage:**
```yaml
- uses: ./.github/actions/build-binaries
  with:
    host: x86_64-pc-linux-gnu
    build-type: release
```

### 3. package-artifacts

Generates checksums and packages build artifacts.

**Inputs:**
- `platform`: Platform identifier (e.g., `ubuntu22`, `win64`)
- `version`: Version string for the package
- `coin-name`: Coin name (default: `raptoreum`)

**Usage:**
```yaml
- uses: ./.github/actions/package-artifacts
  with:
    platform: ubuntu22
    version: ${{ needs.get-version.outputs.version }}
```

### 4. run-tests

Executes unit tests and publishes results.

**Inputs:**
- `platform`: Platform identifier for test reports
- `version`: Version string
- `coin-name`: Coin name (default: `raptoreum`)

**Usage:**
```yaml
- uses: ./.github/actions/run-tests
  with:
    platform: Ubuntu 22
    version: ${{ needs.get-version.outputs.version }}
```

## Key Improvements

### 1. **Eliminated Duplication**
- Original: ~1175 lines with significant duplication
- Refactored: ~350 lines using matrix strategy and composite actions
- **Reduction: ~70% fewer lines**

### 2. **Matrix Strategy**
Linux builds now use a matrix strategy, making it trivial to add new platforms:

```yaml
matrix:
  platform:
    - name: Ubuntu 22
      runner: ubuntu-22.04
      host: x86_64-pc-linux-gnu
      packages: curl libcurl4-openssl-dev ...
      platform-id: ubuntu22
```

To add a new Linux variant, just add one matrix entry instead of copying 150+ lines.

### 3. **Reusable Components**
Common operations are now centralized:
- Dependency building and caching
- Binary compilation with ccache
- Checksum generation and packaging
- Test execution and reporting

### 4. **Easier Maintenance**
- Bug fixes in one place benefit all platforms
- Consistent behavior across all builds
- Clear separation of concerns
- Self-documenting structure

### 5. **Better Readability**
- High-level workflow shows structure at a glance
- Implementation details hidden in composite actions
- Comments clearly delineate sections

## Migration Guide

### Option 1: Test Side-by-Side
1. Keep `build.yaml` as-is
2. Rename `build-refactored.yaml` to something like `build-test.yaml`
3. Test on a feature branch
4. Once validated, replace `build.yaml`

### Option 2: Direct Replacement
```bash
# Backup original
mv .github/workflows/build.yaml .github/workflows/build-backup.yaml

# Use refactored version
mv .github/workflows/build-refactored.yaml .github/workflows/build.yaml
```

## Adding a New Platform

### Example: Adding Ubuntu 26

**Before (old approach):** Add 150+ lines copying from existing Ubuntu build

**After (new approach):** Add 5 lines to the matrix:

```yaml
- name: Ubuntu 26
  runner: ubuntu-26.04
  host: x86_64-pc-linux-gnu
  packages: curl libcurl4-openssl-dev build-essential libtool autotools-dev automake pkg-config python3 bsdmainutils cmake ccache
  platform-id: ubuntu26
```

## Customization Points

### Per-Platform Package Requirements
Defined in matrix `packages` field:
```yaml
packages: curl libcurl4-openssl-dev build-essential ...
```

### Cross-Compilation Hosts
Defined in matrix `host` field:
```yaml
host: aarch64-linux-gnu  # for ARM builds
host: x86_64-w64-mingw32 # for Windows builds
```

### Configure Flags
Can be passed to build-binaries action:
```yaml
- uses: ./.github/actions/build-binaries
  with:
    host: x86_64-pc-linux-gnu
    configure-flags: --enable-zmq --with-incompatible-bdb
```

## Caching Strategy

The refactored workflow uses three cache layers:

1. **Depends Sources Cache**: Shared across all jobs
   - Key: `depends-sources-${{ hashFiles('depends/packages/*') }}`

2. **Depends Build Cache**: Per-job caching
   - Key: `depends-${{ github.job }}-${{ hashFiles('depends/packages/*') }}`

3. **ccache Cache**: Per-job and per-build-type
   - Key: `${{ github.job }}-${{ build-type }}-ccache`

## Environment Variables

Global variables defined at workflow level:

```yaml
env:
  COIN_NAME: raptoreum
  BUILD_DIR: raptoreum-build
  COMPRESS_DIR: raptoreum-compress
  TEST_DIR: raptoreum-test
  FALLBACK_DOWNLOAD_PATH: https://sign.raptoreum.com/depends/
```

These are available to all jobs and composite actions.

## Troubleshooting

### Action Not Found
If you see errors like "action not found", ensure:
1. Composite actions are in `.github/actions/*/action.yml`
2. The workflow checks out the repository before using actions
3. You're using `uses: ./.github/actions/action-name` (note the `./` prefix)

### Cache Not Working
- Check that cache keys are consistent
- Ensure cache paths exist before saving
- Review GitHub Actions cache limits (10GB per repository)

### Matrix Strategy Issues
- Ensure all matrix variables are referenced correctly: `${{ matrix.platform.field }}`
- Use `fail-fast: false` to continue other builds if one fails

## Future Enhancements

Potential improvements:

1. **Add macOS Builds**: Extend matrix to include macOS runners
2. **Parallel Testing**: Split tests into multiple jobs
3. **Docker Builds**: Add containerized build environments
4. **Release Automation**: Auto-create GitHub releases on tag push
5. **Artifact Signing**: Add GPG signing of release artifacts
6. **Performance Metrics**: Track build times and cache hit rates

## Questions?

For questions or issues with the refactored workflows, please open an issue or contact the DevOps team.

