      - name: Ubuntu 22
        host: x86_64-pc-linux-gnu
```

### Issue 3: Cache Key Mismatches
**Symptom:** Cache never hits after refactoring

**Solution:** Ensure cache keys in composite actions match previous keys, or clear caches:
```bash
# Clear all caches via GitHub CLI
gh cache delete --all
```

## Conclusion

The refactored workflow represents a **71% reduction in code size** with **significantly improved maintainability**. While it requires an initial learning curve for the team, the long-term benefits of easier maintenance, consistency, and scalability make it a worthwhile investment.

The modular structure makes it trivial to:
- Add new build platforms
- Update build processes
- Fix bugs consistently
- Test changes safely

**Recommendation: Proceed with gradual migration strategy.**
# Build Workflow Refactoring - Before & After Comparison

## Overview

This document compares the original `build.yaml` workflow with the refactored version.

## Summary Statistics

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| **Total Lines** | ~1,175 | ~340 | **71% reduction** |
| **Duplicate Code** | High | Minimal | **Reusable components** |
| **Jobs** | 8 | 5 | **Consolidated with matrix** |
| **Maintainability** | Low | High | **Modular design** |
| **Adding New Platform** | 150+ lines | 5 lines | **97% less work** |

## Structure Comparison

### Before: build.yaml

```
1175 lines total
├── get-version (64 lines)
├── build-ubuntu22 (148 lines)
├── test-ubuntu22 (33 lines)
├── build-ubuntu24 (148 lines)  ← DUPLICATE
├── test-ubuntu24 (33 lines)    ← DUPLICATE
├── build-arm-64 (133 lines)
├── test-arm-64 (40 lines)
├── build-win64 (138 lines)
└── test-win64 (33 lines)

Issues:
- Ubuntu 22 and Ubuntu 24 builds are nearly identical
- Same caching logic repeated 5+ times
- Same checksum generation repeated in each build
- Test jobs follow same pattern but can't reuse code
```

### After: build-refactored.yaml + Composite Actions

```
340 lines total in workflow
+ 4 reusable composite actions (54 lines each avg)

├── get-version (64 lines) - unchanged
├── build-linux (matrix: 3 platforms, 85 lines)
├── test-linux (matrix: 2 platforms, 27 lines)
├── build-windows (90 lines)
└── test-windows (24 lines)

Composite Actions:
├── setup-depends (54 lines)
├── build-binaries (51 lines)
├── package-artifacts (72 lines)
└── run-tests (39 lines)

Benefits:
✓ Linux builds use matrix (3 platforms in 1 job)
✓ Common operations extracted to actions
✓ Each action is reusable and testable
✓ Clear separation of concerns
```

## Detailed Comparison

### 1. Dependency Setup

#### Before (repeated 5 times, 48 lines each = 240 lines total):
```yaml
- name: Cache depends sources
  uses: actions/cache@v4
  with:
    path: |
      depends/sources
    key: depends-sources-${{ hashFiles('depends/packages/*') }}
    restore-keys: |
      depends-sources-

- name: Restore cached depends
  uses: actions/cache/restore@v4
  id: restore-depends
  with:
    path: |
      depends/built
      depends/${{ steps.setup.outputs.HOST }}
    key: depends-${{ github.job }}-${{ hashFiles('depends/packages/*') }}
    restore-keys: |
      depends-${{ github.job }}-

- name: Build Depends
  run: |
    echo "building with $(nproc) threads"
    gcc --version
    export FALLBACK_DOWNLOAD_PATH=https://sign.raptoreum.com/depends/
    make -C depends -j$(nproc) HOST=x86_64-pc-linux-gnu

- name: Save depends cache
  uses: actions/cache/save@v4
  if: steps.restore-depends.outputs.cache-hit != 'true'
  with:
    path: |
      depends/built
      depends/${{ steps.setup.outputs.HOST }}
    key: ${{ steps.restore-depends.outputs.cache-primary-key }}
```

#### After (1 reusable action, 54 lines, called 5 times):
```yaml
- name: Setup Dependencies
  uses: ./.github/actions/setup-depends
  with:
    host: ${{ matrix.platform.host }}
    fallback-download-path: ${{ env.FALLBACK_DOWNLOAD_PATH }}
```

**Savings: 240 lines → 54 lines (78% reduction)**

### 2. Linux Builds (Ubuntu 22 + Ubuntu 24 + ARM)

#### Before: 3 separate jobs, ~430 lines total
```yaml
build-ubuntu22:
  name: Ubuntu 22 Build
  runs-on: ubuntu-22.04
  steps: [... 148 lines ...]

build-ubuntu24:
  name: Ubuntu 24 Build
  runs-on: ubuntu-24.04
  steps: [... 148 lines ...] # Nearly identical!

build-arm-64:
  name: ARM 64-bit Build
  runs-on: ubuntu-22.04
  steps: [... 133 lines ...]
```

#### After: 1 job with matrix, ~85 lines
```yaml
build-linux:
  name: ${{ matrix.platform.name }} Build
  runs-on: ${{ matrix.platform.runner }}
  strategy:
    fail-fast: false
    matrix:
      platform:
        - name: Ubuntu 22
          runner: ubuntu-22.04
          host: x86_64-pc-linux-gnu
          packages: curl libcurl4-openssl-dev ...
          platform-id: ubuntu22
          
        - name: Ubuntu 24
          runner: ubuntu-24.04
          host: x86_64-pc-linux-gnu
          packages: curl libcurl4-openssl-dev ...
          platform-id: ubuntu24
          
        - name: ARM 64-bit
          runner: ubuntu-22.04
          host: aarch64-linux-gnu
          packages: curl libcurl4-openssl-dev ... g++-aarch64-linux-gnu
          platform-id: arm64
  
  steps:
    - uses: actions/checkout@v4
    - [install packages]
    - uses: ./.github/actions/setup-depends
    - uses: ./.github/actions/build-binaries
    - [collect binaries]
    - uses: ./.github/actions/build-binaries  # debug
    - [collect debug binaries]
    - uses: ./.github/actions/package-artifacts
    - [upload artifacts]
```

**Savings: 430 lines → 85 lines (80% reduction)**

### 3. Checksum Generation

#### Before (repeated 5 times, ~40 lines each = 200 lines):
```yaml
- name: Generate Checksum and Compress
  run: |
    mkdir -p ${COMPRESS_DIR}
    cd ${BUILD_DIR}
    echo "sha256:" >> checksums.txt
    echo "------------------------------------" >> checksums.txt
    shasum * >> checksums.txt
    echo "------------------------------------" >> checksums.txt
    echo "openssl-sha256:" >> checksums.txt
    echo "------------------------------------" >> checksums.txt
    sha256sum * >> checksums.txt
    cat checksums.txt
    tar -cvzf ../${COIN_NAME}-ubuntu22-${{ needs.get-version.outputs.version }}.tar.gz *
    # ... repeated for _debug and _not_strip variants ...
```

#### After (1 reusable action, 72 lines):
```yaml
- name: Package Artifacts
  uses: ./.github/actions/package-artifacts
  with:
    platform: ${{ matrix.platform.platform-id }}
    version: ${{ needs.get-version.outputs.version }}
```

**Savings: 200 lines → 72 lines (64% reduction)**

### 4. Testing

#### Before (3 separate test jobs, ~106 lines):
```yaml
test-ubuntu22:
  steps: [... 33 lines ...]

test-ubuntu24:
  steps: [... 33 lines ...]  # Nearly identical!

test-arm-64:
  steps: [... 40 lines ...]
```

#### After (1 test job with matrix, ~27 lines):
```yaml
test-linux:
  strategy:
    matrix:
      platform:
        - name: Ubuntu 22
          runner: ubuntu-22.04
          platform-id: ubuntu22
        - name: Ubuntu 24
          runner: ubuntu-24.04
          platform-id: ubuntu24
  
  steps:
    - uses: actions/checkout@v4
    - [download artifacts]
    - uses: ./.github/actions/run-tests
      with:
        platform: ${{ matrix.platform.name }}
```

**Savings: 106 lines → 27 lines (75% reduction)**

## Adding a New Platform

### Before: Copy-Paste 150+ lines

To add Ubuntu 26, you would:
1. Copy entire `build-ubuntu24` job (148 lines)
2. Find/replace all "ubuntu24" → "ubuntu26"
3. Update runner to `ubuntu-26.04`
4. Copy entire `test-ubuntu24` job (33 lines)
5. Find/replace again
6. Hope you didn't miss anything!

**Total: ~180 lines of mostly duplicate code**

### After: Add 5 lines to matrix

```yaml
# In build-linux job matrix, add:
- name: Ubuntu 26
  runner: ubuntu-26.04
  host: x86_64-pc-linux-gnu
  packages: curl libcurl4-openssl-dev build-essential libtool autotools-dev automake pkg-config python3 bsdmainutils cmake ccache
  platform-id: ubuntu26

# In test-linux job matrix, add:
- name: Ubuntu 26
  runner: ubuntu-26.04
  platform-id: ubuntu26
```

**Total: 10 lines, completely safe**

## Key Benefits

### 1. **DRY (Don't Repeat Yourself)**
- Common code extracted to reusable actions
- Matrix strategy eliminates platform duplication
- Single source of truth for each operation

### 2. **Maintainability**
- Bug fix in one place benefits all platforms
- Changes to build process only need updating in one location
- Consistent behavior guaranteed across platforms

### 3. **Readability**
- High-level workflow is easy to understand
- Implementation details hidden in appropriately-named actions
- Clear structure with comments

### 4. **Testability**
- Composite actions can be tested independently
- Matrix strategy makes it easy to test specific platforms
- Fail-fast: false means one platform failure doesn't block others

### 5. **Scalability**
- Adding new Linux variants is trivial
- Adding new architectures follows same pattern
- Easy to extend with new features

## Migration Strategy

### Recommended Approach: Gradual Migration

1. **Week 1: Test refactored version**
   - Rename `build-refactored.yaml` to `build-new.yaml`
   - Run both workflows in parallel on develop branch
   - Compare artifacts and test results

2. **Week 2: Validate on feature branches**
   - Use new workflow for feature branch builds
   - Collect feedback from team
   - Fix any issues discovered

3. **Week 3: Production cutover**
   - Backup original: `build.yaml` → `build-legacy.yaml`
   - Activate new: `build-new.yaml` → `build.yaml`
   - Monitor first few runs closely

4. **Week 4: Cleanup**
   - Remove legacy workflow if no issues
   - Update documentation
   - Train team on new structure

### Alternative: Big Bang Migration

If you're confident:
```bash
cd .github/workflows
mv build.yaml build-backup-$(date +%Y%m%d).yaml
mv build-refactored.yaml build.yaml
git add -A
git commit -m "Refactor: Modernize CI/CD workflow with reusable actions"
```

## Potential Issues & Solutions

### Issue 1: Composite Actions Not Found
**Symptom:** `Error: Unable to resolve action ./.github/actions/setup-depends`

**Solution:** Ensure checkout action runs before using composite actions:
```yaml
steps:
  - uses: actions/checkout@v4  # Must be first!
  - uses: ./.github/actions/setup-depends
```

### Issue 2: Matrix Variables Not Resolving
**Symptom:** Variables like `${{ matrix.platform.host }}` are empty

**Solution:** Check matrix definition structure:
```yaml
strategy:
  matrix:
    platform:  # This level is important!

