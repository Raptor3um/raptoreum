# Quick Start Guide - Refactored Workflow

## 🚀 Getting Started

### To Use the Refactored Workflow

**Option A: Test it safely (recommended)**
```bash
cd /home/tri/workspace/raptoreum

# Rename to test alongside the original
git mv .github/workflows/build-refactored.yaml .github/workflows/build-test.yaml

# Commit and push to a feature branch
git checkout -b test/refactored-workflow
git add -A
git commit -m "test: Add refactored CI/CD workflow"
git push -u origin test/refactored-workflow
```

**Option B: Replace directly (when ready)**
```bash
cd /home/tri/workspace/raptoreum

# Backup the original
git mv .github/workflows/build.yaml .github/workflows/build-legacy.yaml

# Activate the refactored version
git mv .github/workflows/build-refactored.yaml .github/workflows/build.yaml

# Commit
git add -A
git commit -m "refactor: Modernize CI/CD with reusable composite actions

- Reduce workflow size from 1175 to 310 lines (74% reduction)
- Extract common operations to 4 reusable composite actions
- Use matrix strategy for Linux builds (Ubuntu 22, 24, ARM64)
- Maintain 100% feature parity with original workflow
- Improve maintainability and reduce code duplication

See .github/REFACTORING_SUMMARY.md for details"

git push
```

## 📝 Common Tasks

### Add a New Linux Platform (e.g., Ubuntu 26)

1. Edit `.github/workflows/build-refactored.yaml` (or `build.yaml` after migration)

2. In the `build-linux` job, add to the matrix:
```yaml
- name: Ubuntu 26
  runner: ubuntu-26.04
  host: x86_64-pc-linux-gnu
  packages: curl libcurl4-openssl-dev build-essential libtool autotools-dev automake pkg-config python3 bsdmainutils cmake ccache
  platform-id: ubuntu26
```

3. In the `test-linux` job, add to the matrix:
```yaml
- name: Ubuntu 26
  runner: ubuntu-26.04
  platform-id: ubuntu26
```

4. Commit and test!

### Modify Dependency Building

Edit `.github/actions/setup-depends/action.yml`

Changes will automatically apply to:
- Ubuntu 22 builds
- Ubuntu 24 builds
- ARM64 builds
- Windows builds

### Change Build Configuration

Edit `.github/actions/build-binaries/action.yml`

For example, to add a new configure flag to all builds:
```yaml
- name: Configure
  shell: bash
  run: |
    ./autogen.sh
    if [ "${{ inputs.build-type }}" = "debug" ]; then
      ./configure --prefix=`pwd`/depends/${{ inputs.host }} \
        --disable-tests --enable-debug --enable-crash-hooks \
        --enable-zmq ${{ inputs.configure-flags }}  # <-- Add your flag
    else
      ./configure --prefix=`pwd`/depends/${{ inputs.host }} \
        --enable-zmq ${{ inputs.configure-flags }}  # <-- Add your flag
    fi
```

### Update Checksum/Package Logic

Edit `.github/actions/package-artifacts/action.yml`

### Modify Test Execution

Edit `.github/actions/run-tests/action.yml`

## 🔍 Troubleshooting

### Workflow Doesn't Start
- Ensure the YAML file is in `.github/workflows/`
- Check that the filename ends with `.yaml` or `.yml`
- Verify the `on:` triggers match your branch

### Actions Not Found Error
```
Error: Unable to resolve action ./.github/actions/setup-depends
```

**Fix:** Ensure checkout happens first:
```yaml
steps:
  - name: Checkout
    uses: actions/checkout@v4  # <-- Must be first!
    
  - name: Setup Dependencies
    uses: ./.github/actions/setup-depends
```

### Cache Not Working
Clear all caches:
```bash
# Using GitHub CLI
gh cache delete --all

# Or via GitHub UI
# Settings → Actions → Caches → Delete all
```

### Matrix Variable Empty
Ensure correct structure:
```yaml
strategy:
  matrix:
    platform:  # <-- This level is required!
      - name: Ubuntu 22
        host: x86_64-pc-linux-gnu
```

Access with: `${{ matrix.platform.host }}`

## 📊 Monitoring

### Check Workflow Status
```bash
# List recent workflow runs
gh run list --workflow=build.yaml

# Watch a specific run
gh run watch <run-id>

# View logs
gh run view <run-id> --log
```

### Compare Build Times

Original vs Refactored:
```bash
# Get timing for original
gh run view <original-run-id> | grep "Total duration"

# Get timing for refactored
gh run view <refactored-run-id> | grep "Total duration"
```

## 📦 Artifacts

After a successful build, artifacts are available:

### Release Builds
- `raptoreum-ubuntu22-{version}.tar.gz`
- `raptoreum-ubuntu24-{version}.tar.gz`
- `raptoreum-arm64-{version}.tar.gz`
- `raptoreum-win-{version}.zip`

### Debug Builds
- `raptoreum-ubuntu22-debug-{version}.tar.gz`
- `raptoreum-ubuntu24-debug-{version}.tar.gz`
- `raptoreum-arm64-debug-{version}.tar.gz`

### Non-stripped Builds
- `raptoreum-ubuntu22-not_strip-{version}.tar.gz`
- `raptoreum-ubuntu24-not_strip-{version}.tar.gz`
- `raptoreum-win-not_strip-{version}.zip`

### Test Results
- `raptoreum-ubuntu22-test-results-{version}`
- `raptoreum-ubuntu24-test-results-{version}`
- `raptoreum-win64-test-results-{version}`

## 🎓 Learning Resources

### Understanding the Refactoring
1. Read: `.github/REFACTORING_SUMMARY.md` (overview)
2. Read: `.github/workflows/REFACTORING_COMPARISON.md` (detailed comparison)
3. Read: `.github/actions/README.md` (action documentation)

### GitHub Actions Concepts Used
- **Composite Actions**: Reusable action bundles
- **Matrix Strategy**: Run same job with different parameters
- **Caching**: Speed up builds by reusing artifacts
- **Artifacts**: Store and share build outputs

### External Documentation
- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Composite Actions Guide](https://docs.github.com/en/actions/creating-actions/creating-a-composite-action)
- [Matrix Strategy](https://docs.github.com/en/actions/using-jobs/using-a-matrix-for-your-jobs)

## ✅ Validation Checklist

Before considering the refactoring complete:

- [x] All YAML files validated
- [ ] Tested on feature branch
- [ ] All platforms build successfully
- [ ] Tests pass on all platforms
- [ ] Artifacts match original workflow
- [ ] Build times are comparable
- [ ] No cache issues
- [ ] Team reviewed and approved

## 🤝 Getting Help

If you encounter issues:

1. Check the troubleshooting section above
2. Review workflow logs in GitHub Actions UI
3. Compare with original workflow behavior
4. Check composite action definitions
5. Open an issue with:
   - Workflow run URL
   - Error messages
   - Expected vs actual behavior

## 📈 Next Steps

After successful migration:

1. **Monitor for 1-2 weeks** - Watch for any issues
2. **Collect feedback** - Ask team about their experience
3. **Remove legacy workflow** - If everything works well
4. **Update documentation** - Remove references to old workflow
5. **Share knowledge** - Train team on new structure

---

**Questions?** See `.github/REFACTORING_SUMMARY.md` for more details.

