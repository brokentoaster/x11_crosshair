# TBC-ci-container: Project-owned CI container images — Plan

## Spec
- Spec: docs/workplans/TBC-ci-container-Spec.md
- Jira: n/a

## Overview
- Outcome: `ci/Dockerfile` builds a minimal Debian-slim image with the exact
  toolchain the CI jobs need; a publish workflow pushes it to GHCR with
  version/SHA tags; both CI jobs run inside the pinned image with the apt
  steps removed; `act` runs the same workflow locally against the same image.
- Approach: Dockerfile first (verifiable locally with docker build + a
  manual ctest run), then rewire `ci.yml` to use the image and verify via
  `act`, then the publish workflow and docs.

## Tasks
- [x] Task 1: Create `ci/Dockerfile`
  - Notes: FROM `debian:12-slim` (pinned, spec AC 6); apt-get install
    `g++ cmake make pkg-config git libx11-dev libxfixes-dev libxi-dev`;
    clean apt lists to keep size down; no other tools (spec AC 2).
  - Touchpoints: `ci/Dockerfile` (new)
  - Done when: `docker build -f ci/Dockerfile .` succeeds; configure/build
    with `-Wconversion` + `-Werror` and `ctest` pass inside the image
    (spec AC 1).
- [x] Task 2: Rewire `.github/workflows/ci.yml` to the container
  - Notes: add `container: ghcr.io/<owner>/<repo>-ci:<version>` (exact GHCR
    path per spec Open Questions — check what GHCR accepts, default
    `<owner>/<repo>-ci`), pin `<version>` = `1`; delete the apt install
    steps; keep checkout, configure, build, test steps as-is.
  - Touchpoints: `.github/workflows/ci.yml`
  - Done when: `act` runs both jobs green locally using the image
    (spec AC 4); workflow has no apt steps (spec AC 3).
- [x] Task 3: Add `.github/workflows/ci-image.yml` publish workflow
  - Notes: trigger on push to default branch with paths `ci/**` and the
    workflow file itself; use docker/build-push-action with
    `tags: ghcr.io/<owner>/<repo>-ci:1, :latest, :<sha>`; permissions
    `packages: write`; only publish from the default branch.
  - Touchpoints: `.github/workflows/ci-image.yml` (new)
  - Done when: workflow file lints (`act -l` or review); after merge the
    GHCR package shows the three tags (spec AC 5).
- [x] Task 4: Write `ci/README.md` and tick plan
  - Notes: image contents, local build command, how the publish workflow
    triggers, how to bump `<version>`, exact `act` invocation
    (`act --container-options`/pull behavior per spec Open Questions,
    default: pull published image), offline fallback (build locally first).
    Tick plan tasks; run the markdown table formatter if any tables.
  - Touchpoints: `ci/README.md` (new), plan file
  - Done when: spec AC 7; docs match the shipped workflows.

## Dependencies & prerequisites
- Task 2 requires Task 1 (needs an image to reference; for local `act`
  testing, build/tag it locally as the GHCR name).
- Task 3 after Task 2 (publish workflow's tags must match what `ci.yml`
  pins).
- Task 4 last.
- Prerequisite: docker available locally for Tasks 1–2 verification; GHCR
  publish verified only after merge (Task 3).

## Verification
- See spec Acceptance Criteria and Verification Plan.
- Task-local checks as in "Done when"; the cloud-side ACs (5, 8) are proven
  post-merge and noted in the plan if they need follow-up.

## Risks & mitigations
- Risk: GHCR package naming for `owner/repo/ci` subpaths may not be
  accepted → Mitigate: spec Open Question; default to `<owner>-<repo>-ci`
  flat name, confirm on first publish.
- Risk: `act` needs the image pre-pulled and GHCR may be private at
  first run → Mitigate: `ci/README.md` documents `docker build -t` the
  exact GHCR name locally so `act` finds it without pulling.
- Risk: `-Werror` build inside container fails due to different compiler
  version than local → Mitigate: Task 1 verification includes the exact
  CI build commands inside the image before any workflow rewiring.

## Out of scope
- Xvfb/display tests in containers, multi-arch, non-GHCR registries, job
  command changes (spec Out of scope).

## Rollback
- See spec Rollback Plan (revert series; workflows return to
  ubuntu-latest + apt).