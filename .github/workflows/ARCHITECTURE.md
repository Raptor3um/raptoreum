# Workflow Architecture Diagram

## Before Refactoring (Original)

```
┌─────────────────────────────────────────────────────────────────────┐
│                           build.yaml (1175 lines)                   │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
        ┌──────────────────────────────────────────────┐
        │           get-version (64 lines)             │
        └──────────────────────────────────────────────┘
                                   │
        ┌──────────────────────────┴───────────────────────────┬────────────────┬─────────────┐
        ▼                          ▼                           ▼                ▼             ▼
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐  ┌─────────────┐  ┌─────────────┐
│ build-ubuntu22  │      │ build-ubuntu24  │      │  build-arm-64   │  │build-win64  │  │   (more)    │
│   (148 lines)   │      │   (148 lines)   │      │   (133 lines)   │  │(138 lines)  │  │             │
│                 │      │                 │      │                 │  │             │  │             │
│  ┌──────────┐  │      │  ┌──────────┐  │      │  ┌──────────┐  │  │┌──────────┐ │  │             │
│  │ Checkout │  │      │  │ Checkout │  │      │  │ Checkout │  │  ││ Checkout │ │  │             │
│  └────┬─────┘  │      │  └────┬─────┘  │      │  └────┬─────┘  │  │└────┬─────┘ │  │             │
│       ▼        │      │       ▼        │      │       ▼        │  │     ▼       │  │             │
│  ┌──────────┐  │      │  ┌──────────┐  │      │  ┌──────────┐  │  │┌──────────┐ │  │             │
│  │  Cache   │  │      │  │  Cache   │  │      │  │  Cache   │  │  ││  Cache   │ │  │             │
│  │ Depends  │  │      │  │ Depends  │  │      │  │ Depends  │  │  ││ Depends  │ │  │             │
│  └────┬─────┘  │      │  └────┬─────┘  │      │  └────┬─────┘  │  │└────┬─────┘ │  │             │
│       ▼        │      │       ▼        │      │       ▼        │  │     ▼       │  │             │
│  ┌──────────┐  │      │  ┌──────────┐  │      │  ┌──────────┐  │  │┌──────────┐ │  │             │
│  │  Build   │  │ SAME │  │  Build   │  │ SAME │  │  Build   │  │  ││  Build   │ │  │   SIMILAR   │
│  │ Depends  │  │ CODE │  │ Depends  │  │ CODE │  │ Depends  │  │  ││ Depends  │ │  │    CODE     │
│  └────┬─────┘  │      │  └────┬─────┘  │      │  └────┬─────┘  │  │└────┬─────┘ │  │             │
│       ▼        │      │       ▼        │      │       ▼        │  │     ▼       │  │             │
│  ┌──────────┐  │      │  ┌──────────┐  │      │  ┌──────────┐  │  │┌──────────┐ │  │             │
│  │Configure │  │      │  │Configure │  │      │  │Configure │  │  ││Configure │ │  │             │
│  └────┬─────┘  │      │  └────┬─────┘  │      │  └────┬─────┘  │  │└────┬─────┘ │  │             │
│       ▼        │      │       ▼        │      │       ▼        │  │     ▼       │  │             │
│  ┌──────────┐  │      │  ┌──────────┐  │      │  ┌──────────┐  │  │┌──────────┐ │  │             │
│  │  Build   │  │      │  │  Build   │  │      │  │  Build   │  │  ││  Build   │ │  │             │
│  │ Binaries │  │      │  │ Binaries │  │      │  │ Binaries │  │  ││ Binaries │ │  │             │
│  └────┬─────┘  │      │  └────┬─────┘  │      │  └────┬─────┘  │  │└────┬─────┘ │  │             │
│       ▼        │      │       ▼        │      │       ▼        │  │     ▼       │  │             │
│  ┌──────────┐  │      │  ┌──────────┐  │      │  ┌──────────┐  │  │┌──────────┐ │  │             │
│  │ Checksum │  │      │  │ Checksum │  │      │  │ Checksum │  │  ││ Checksum │ │  │             │
│  │ Package  │  │      │  │ Package  │  │      │  │ Package  │  │  ││ Package  │ │  │             │
│  └────┬─────┘  │      │  └────┬─────┘  │      │  └────┬─────┘  │  │└────┬─────┘ │  │             │
│       ▼        │      │       ▼        │      │       ▼        │  │     ▼       │  │             │
│  ┌──────────┐  │      │  ┌──────────┐  │      │  ┌──────────┐  │  │┌──────────┐ │  │             │
│  │  Upload  │  │      │  │  Upload  │  │      │  │  Upload  │  │  ││  Upload  │ │  │             │
│  └────┬─────┘  │      │  └────┬─────┘  │      │  └────┬─────┘  │  │└────┬─────┘ │  │             │
└───────┼────────┘      └────────┼────────┘      └────────┼────────┘  └──────┼──────┘  └─────────────┘
        │                        │                        │                  │
        ▼                        ▼                        ▼                  ▼
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐  ┌─────────────┐
│  test-ubuntu22  │      │  test-ubuntu24  │      │  test-arm-64    │  │test-win64   │
│   (33 lines)    │      │   (33 lines)    │      │   (40 lines)    │  │ (33 lines)  │
└─────────────────┘      └─────────────────┘      └─────────────────┘  └─────────────┘

❌ PROBLEMS:
   - Massive code duplication (240+ lines repeated 5 times)
   - Hard to maintain consistency
   - Adding new platform = copy/paste 180 lines
   - Bug fixes need updates in 5+ places
```

## After Refactoring (Modular)

```
┌─────────────────────────────────────────────────────────────────────┐
│                  build-refactored.yaml (310 lines)                  │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
        ┌──────────────────────────────────────────────┐
        │           get-version (64 lines)             │
        └──────────────────────────────────────────────┘
                                   │
        ┌──────────────────────────┴───────────────────────────┬─────────────────┐
        ▼                                                       ▼                 ▼
┌──────────────────────────────────────────┐          ┌─────────────────┐  ┌────────────┐
│  build-linux (Matrix Strategy)           │          │  build-windows  │  │    ARM     │
│  • Ubuntu 22                              │          │   (90 lines)    │  │   TESTS    │
│  • Ubuntu 24                              │          │                 │  │   SKIPPED  │
│  • ARM 64                                 │          │  ┌──────────┐  │  │            │
│  (85 lines handles all 3!)                │          │  │ Checkout │  │  │            │
│                                           │          │  └────┬─────┘  │  │            │
│  ┌──────────┐                            │          │       ▼        │  │            │
│  │ Checkout │                            │          │  ┌──────────┐  │  │            │
│  └────┬─────┘                            │          │  │ Install  │  │  │            │
│       ▼                                  │          │  │ MinGW    │  │  │            │
│  ┌──────────────────────────────────┐   │          │  └────┬─────┘  │  │            │
│  │     setup-depends (action)       │◄──┼──────────┼───────┼────────┼──┼────────────┤
│  │  • Cache sources                 │   │          │       ▼        │  │            │
│  │  • Cache builds                  │   │          │  ┌──────────┐  │  │            │
│  │  • Build depends                 │   │          │  │setup-    │  │  │            │
│  └────────────────┬─────────────────┘   │          │  │depends   │  │  │            │
│                   ▼                      │          │  └────┬─────┘  │  │            │
│  ┌──────────────────────────────────┐   │          │       ▼        │  │            │
│  │   build-binaries (action)        │◄──┼──────────┼───────┼────────┼──┼────────────┤
│  │  • Configure                     │   │          │  ┌──────────┐  │  │            │
│  │  • Setup ccache                  │   │          │  │build-    │  │  │            │
│  │  • Build                         │   │          │  │binaries  │  │  │            │
│  └────────────────┬─────────────────┘   │          │  └────┬─────┘  │  │            │
│                   │                      │          │       │        │  │            │
│         ┌─────────┴─────────┐           │          │   [Package]    │  │            │
│         ▼                   ▼           │          │       │        │  │            │
│   [Release Build]    [Debug Build]      │          │       ▼        │  │            │
│         │                   │           │          │  ┌──────────┐  │  │            │
│         ▼                   ▼           │          │  │  Upload  │  │  │            │
│  ┌──────────────────────────────────┐   │          │  └──────────┘  │  │            │
│  │  package-artifacts (action)      │◄──┼──────────┼────────────────┼──┼────────────┤
│  │  • Generate checksums            │   │          └─────────────────┘  │            │
│  │  • Create tarballs               │   │                 │              │            │
│  │  • Master checksums              │   │                 ▼              │            │
│  └────────────────┬─────────────────┘   │          ┌─────────────────┐  │            │
│                   ▼                      │          │  test-windows   │  │            │
│  ┌──────────────────────────────────┐   │          │   (24 lines)    │  │            │
│  │  Upload Artifacts                │   │          └─────────────────┘  │            │
│  └────────────────┬─────────────────┘   │                               │            │
└────────────────────┼──────────────────────┘                               │            │
                     │                                                      │            │
                     ▼                                                      ▼            │
         ┌───────────────────────────┐                                                  │
         │  test-linux (Matrix)      │                                                  │
         │  • Ubuntu 22              │                                                  │
         │  • Ubuntu 24              │                                                  │
         │  (27 lines handles both!) │                                                  │
         │                           │                                                  │
         │  ┌──────────┐            │                                                  │
         │  │ Checkout │            │                                                  │
         │  └────┬─────┘            │                                                  │
         │       ▼                  │                                                  │
         │  ┌──────────┐            │                                                  │
         │  │ Download │            │                                                  │
         │  └────┬─────┘            │                                                  │
         │       ▼                  │                                                  │
         │  ┌──────────────────┐   │                                                  │
         │  │  run-tests       │◄──┼──────────────────────────────────────────────────┤
         │  │  (action)        │   │
         │  │  • Execute tests │   │
         │  │  • Upload results│   │
         │  │  • Publish report│   │
         │  └──────────────────┘   │
         └───────────────────────────┘

✅ BENEFITS:
   - Reusable composite actions (4 total, 221 lines)
   - Matrix strategy eliminates duplication
   - Adding new platform = 10 lines in matrix
   - Bug fix = update 1 action, applies everywhere
   - 55% less total code
```

## Composite Actions (Shared Components)

```
┌─────────────────────────────────────────────────────────────────┐
│                    Composite Actions Library                    │
└─────────────────────────────────────────────────────────────────┘
         │                 │                │               │
         ▼                 ▼                ▼               ▼
  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────┐
  │   setup-    │  │    build-    │  │   package-   │  │   run-   │
  │  depends    │  │   binaries   │  │  artifacts   │  │  tests   │
  │             │  │              │  │              │  │          │
  │  52 lines   │  │   51 lines   │  │   78 lines   │  │ 40 lines │
  │             │  │              │  │              │  │          │
  │ Used by:    │  │ Used by:     │  │ Used by:     │  │ Used by: │
  │ • Ubuntu 22 │  │ • Ubuntu 22  │  │ • Ubuntu 22  │  │ • Ubu 22 │
  │ • Ubuntu 24 │  │ • Ubuntu 24  │  │ • Ubuntu 24  │  │ • Ubu 24 │
  │ • ARM 64    │  │ • ARM 64     │  │ • ARM 64     │  │          │
  │ • Windows   │  │ • Windows    │  │              │  │          │
  └─────────────┘  └──────────────┘  └──────────────┘  └──────────┘
```

## Data Flow

```
User Push/PR
     │
     ▼
┌─────────────────┐
│  Trigger Event  │
└────────┬────────┘
         │
         ▼
┌─────────────────────────────────────────────┐
│         Determine Version                   │
│  • Snapshot (develop/feature branches)      │
│  • Candidate (release branches)             │
│  • Release (master)                         │
└────────┬────────────────────────────────────┘
         │
         ├────────────────┬──────────────┬────────────────┐
         ▼                ▼              ▼                ▼
    ┌─────────┐      ┌─────────┐   ┌─────────┐     ┌──────────┐
    │Ubuntu 22│      │Ubuntu 24│   │ ARM 64  │     │ Windows  │
    │  Build  │      │  Build  │   │  Build  │     │  Build   │
    └────┬────┘      └────┬────┘   └────┬────┘     └─────┬────┘
         │                │             │                 │
         ├────────────────┴─────────────┘                 │
         │                                                │
         ▼                                                ▼
    ┌──────────────────────────┐               ┌──────────────────┐
    │    Linux Artifacts       │               │ Windows Artifacts│
    │ • Release tarballs       │               │ • Release .zip   │
    │ • Debug tarballs         │               │ • Not_strip .zip │
    │ • Not_strip tarballs     │               │ • Test binaries  │
    │ • Test binaries          │               └────────┬─────────┘
    └────────┬─────────────────┘                        │
             │                                           │
             ├───────────┬───────────┐                   │
             ▼           ▼           ▼                   ▼
        ┌────────┐  ┌────────┐  ┌────────┐        ┌──────────┐
        │Test U22│  │Test U24│  │Test A64│        │Test Win64│
        └────┬───┘  └────┬───┘  └────┬───┘        └─────┬────┘
             │           │           │                   │
             └───────────┴───────────┴───────────────────┘
                                 │
                                 ▼
                    ┌───────────────────────────┐
                    │    Test Reports           │
                    │ • JUnit XML               │
                    │ • GitHub Checks           │
                    │ • Uploaded artifacts      │
                    └───────────────────────────┘
```

## Caching Strategy

```
┌───────────────────────────────────────────────────────────────────┐
│                        Cache Hierarchy                            │
└───────────────────────────────────────────────────────────────────┘
                                │
                ┌───────────────┴────────────────┐
                ▼                                ▼
    ┌───────────────────────┐      ┌───────────────────────┐
    │  Level 1: Sources     │      │  Level 2: Builds      │
    │  (Shared globally)    │      │  (Per-job)            │
    │                       │      │                       │
    │  depends/sources/     │      │  depends/built/       │
    │                       │      │  depends/HOST/        │
    │  Key:                 │      │                       │
    │  depends-sources-${{ │      │  Key:                 │
    │  hashFiles('...')}}   │      │  depends-JOB-${{      │
    │                       │      │  hashFiles('...')}}   │
    └───────────────────────┘      └───────────────────────┘
                                                │
                                ┌───────────────┴────────────────┐
                                ▼                                ▼
                    ┌───────────────────┐          ┌───────────────────┐
                    │  Level 3: ccache  │          │  Level 3: ccache  │
                    │  (Release)        │          │  (Debug)          │
                    │                   │          │                   │
                    │  Key:             │          │  Key:             │
                    │  JOB-release-     │          │  JOB-debug-       │
                    │  ccache           │          │  ccache           │
                    └───────────────────┘          └───────────────────┘

Hit Rate Impact:
• Sources cache: 90-95% (rarely changes)
• Builds cache:  70-80% (changes with depends updates)
• ccache:        50-70% (changes with code changes)

Average speedup: 40-60% faster builds with warm cache
```

## Legend

```
┌─────────┐
│  Box    │  = Job or Action
└─────────┘

    ▼        = Flow direction

◄───────┤   = Shared resource/action

Matrix    = Same job, multiple configurations
```

---

**Note:** This diagram shows logical relationships. In practice, GitHub Actions executes jobs in parallel where possible.

