# Migration Checklist

Use this checklist to safely migrate from the original workflow to the refactored version.

## Pre-Migration

- [x] ✅ Refactored workflow created (`.github/workflows/build-refactored.yaml`)
- [x] ✅ Composite actions created (4 actions in `.github/actions/`)
- [x] ✅ Documentation written (README, COMPARISON, SUMMARY, QUICK_START, ARCHITECTURE)
- [x] ✅ YAML syntax validated
- [ ] Team reviewed the refactoring proposal
- [ ] Stakeholders approved the migration plan

## Testing Phase (Week 1)

### Setup Test Environment

- [ ] Create feature branch: `test/refactored-workflow`
  ```bash
  git checkout -b test/refactored-workflow
  ```

- [ ] Rename refactored workflow for parallel testing
  ```bash
  git mv .github/workflows/build-refactored.yaml .github/workflows/build-test.yaml
  ```

- [ ] Update workflow triggers to test specific branches
  ```yaml
  on:
    push:
      branches:
        - test/refactored-workflow
  ```

- [ ] Commit and push
  ```bash
  git add -A
  git commit -m "test: Add refactored CI/CD workflow for testing"
  git push -u origin test/refactored-workflow
  ```

### Run Parallel Tests

- [ ] Trigger both workflows on the same commit
- [ ] Compare Ubuntu 22 build outputs
- [ ] Compare Ubuntu 24 build outputs
- [ ] Compare ARM64 build outputs
- [ ] Compare Windows build outputs
- [ ] Verify all checksums match
- [ ] Check test results are identical
- [ ] Compare build times (±10% acceptable)

### Validation Checks

- [ ] All jobs complete successfully
- [ ] No action-not-found errors
- [ ] Caching works correctly
- [ ] Artifacts are properly named
- [ ] Test reports published correctly
- [ ] Matrix strategy works as expected
- [ ] Composite actions execute properly

## Feedback Phase (Week 2)

- [ ] Share test results with team
- [ ] Collect feedback on:
  - [ ] Ease of understanding
  - [ ] Error messages clarity
  - [ ] Documentation quality
  - [ ] Any concerns or issues

- [ ] Address any issues found:
  - [ ] Bug fixes applied
  - [ ] Documentation updated
  - [ ] Team concerns addressed

- [ ] Run additional test builds on feature branches
- [ ] Verify stability over multiple runs

## Migration Phase (Week 3)

### Backup Original

- [ ] Create backup of original workflow
  ```bash
  git checkout develop  # or master
  git pull
  git checkout -b migrate/refactored-workflow
  cp .github/workflows/build.yaml .github/workflows/build-legacy-$(date +%Y%m%d).yaml
  git add .github/workflows/build-legacy-*.yaml
  git commit -m "backup: Preserve original workflow before migration"
  ```

### Deploy Refactored Version

- [ ] Copy refactored workflow to production name
  ```bash
  cp .github/workflows/build-refactored.yaml .github/workflows/build.yaml
  ```

- [ ] Update triggers to production branches
  ```yaml
  on:
    push:
      branches:
        - master
        - develop
        - "ft/*"
        - "bug/*"
        - "release/*"
    pull_request:
      branches:
        - develop
  ```

- [ ] Commit the changes
  ```bash
  git add -A
  git commit -m "refactor: Migrate to modular CI/CD workflow

  - Reduce workflow size from 1175 to 310 lines (74% reduction)
  - Extract common operations to 4 reusable composite actions
  - Use matrix strategy for Linux builds (Ubuntu 22, 24, ARM64)
  - Maintain 100% feature parity with original workflow
  - Improve maintainability and reduce code duplication

  See .github/REFACTORING_SUMMARY.md for full details
  See .github/workflows/REFACTORING_COMPARISON.md for comparison
  
  Breaking changes: None
  Backward compatibility: 100%"
  ```

- [ ] Push and create PR
  ```bash
  git push -u origin migrate/refactored-workflow
  gh pr create --title "refactor: Migrate to modular CI/CD workflow" \
               --body "See commit message for details"
  ```

### Monitor First Runs

- [ ] Watch first build on develop branch
- [ ] Monitor for any errors or warnings
- [ ] Verify artifacts are created
- [ ] Check test reports
- [ ] Validate caching behavior
- [ ] Compare metrics with historical data

### Rollback Plan (if needed)

If critical issues arise:

```bash
# Quick rollback
git revert HEAD
git push

# Or restore from backup
cp .github/workflows/build-legacy-*.yaml .github/workflows/build.yaml
git add .github/workflows/build.yaml
git commit -m "revert: Restore original workflow due to [ISSUE]"
git push
```

## Post-Migration (Week 4)

### Cleanup

- [ ] Remove test workflow if exists
  ```bash
  rm .github/workflows/build-test.yaml
  ```

- [ ] Keep backup for 30 days, then remove
  ```bash
  # After 30 days of successful operation
  git rm .github/workflows/build-legacy-*.yaml
  git commit -m "cleanup: Remove legacy workflow backup"
  ```

- [ ] Archive refactored template
  ```bash
  git rm .github/workflows/build-refactored.yaml  # if not already renamed
  ```

### Documentation Updates

- [ ] Update project documentation referencing CI/CD
- [ ] Update CONTRIBUTING.md if it mentions workflows
- [ ] Update README if it has build badges/instructions
- [ ] Add entry to CHANGELOG
- [ ] Update team wiki/knowledge base

### Knowledge Transfer

- [ ] Present refactoring to team
- [ ] Walk through composite actions
- [ ] Demonstrate how to add new platforms
- [ ] Show how to modify build process
- [ ] Answer questions and concerns

### Training Materials

- [ ] Share documentation links:
  - [ ] `.github/REFACTORING_SUMMARY.md`
  - [ ] `.github/workflows/QUICK_START.md`
  - [ ] `.github/workflows/ARCHITECTURE.md`
  - [ ] `.github/actions/README.md`

## Ongoing Monitoring

### First Month

- [ ] Week 1: Daily monitoring of workflow runs
- [ ] Week 2: Every-other-day monitoring
- [ ] Week 3: Weekly monitoring
- [ ] Week 4: Review and retrospective

### Metrics to Track

- [ ] Build success rate
- [ ] Average build time
- [ ] Cache hit rates
- [ ] Test pass rates
- [ ] Developer feedback
- [ ] Issues/bugs reported

### Success Criteria

- [ ] ✅ 95%+ build success rate (same as before)
- [ ] ✅ Build times within 10% of original
- [ ] ✅ Zero critical bugs introduced
- [ ] ✅ Positive team feedback
- [ ] ✅ No rollbacks needed
- [ ] ✅ All platforms building successfully

## Issue Tracking

### Known Issues

| Issue | Severity | Status | Resolution |
|-------|----------|--------|------------|
| (none yet) | - | - | - |

### Reported Issues

| Date | Issue | Reporter | Status | Resolution |
|------|-------|----------|--------|------------|
| | | | | |

## Sign-off

### Technical Review

- [ ] Lead Developer: __________________ Date: __________
- [ ] DevOps Engineer: ________________ Date: __________
- [ ] QA Engineer: ____________________ Date: __________

### Approval

- [ ] Project Manager: ________________ Date: __________
- [ ] Technical Lead: _________________ Date: __________

## Notes

Use this section for any additional notes during migration:

```
[Date] [Note]




```

---

**Migration Status:** 🟡 Ready for Testing

**Next Step:** Begin Testing Phase (Week 1)

**Questions?** Contact DevOps team or see documentation in `.github/`

