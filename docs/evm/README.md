# EVM Integration Documentation

This directory contains the design and implementation documentation for the
native EVM integration on the `feat/evm-integration` branch.

## Files

| File | Purpose |
|---|---|
| [`PLAN.md`](PLAN.md) | Full design plan with 8-phase roadmap, architecture, decisions D1-D6, review findings, failure modes, and parallelization strategy. **Source of truth for what we're building and why.** |
| [`README.md`](README.md) | This file — index. |

## Reading order

1. **First time?** Read [`BRANCH-GUIDE.md`](BRANCH-GUIDE.md) at the repo root for orientation.
2. **What's frozen and what's open?** Read [`../../TODOS.md`](../../TODOS.md) at the repo root.
3. **Why these choices?** Read [`PLAN.md`](PLAN.md) — especially "Arquitectura global", "Decisiones de revisión", and "Failure modes".

## Editing this directory

- **`PLAN.md`** is the canonical plan. Treat changes carefully — they should reflect deliberate review or new decisions, not drift.
- Decisions D1-D6 are **frozen** — to revisit one, run a new `/plan-eng-review` cycle and update the corresponding section in `PLAN.md`.
- New per-phase design docs (e.g., `PHASE-1-AAL.md`, `PHASE-2-EVMC.md`) should land here as each phase begins detailed design.

## Status snapshot (2026-05-09)

- ☐ Phase 0 — spike in progress (this commit).
- ☐ Phase 1-8 — pending Phase 0 success.
- 🔒 Decisions D1-D6 frozen.
- ⚠️ P0 blockers open: Foundation legal (CQ4), strategic alignment with public README.
