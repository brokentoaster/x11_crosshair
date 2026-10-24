# TBC-ci-container: Project-owned CI container images — Spec

## Goal

The CI workflows (GitHub Actions and local `act` runs) execute in the same
project-maintained Docker image, so CI behavior is identical locally and in
the cloud, dependencies are pre-installed (no per-run apt install), and a
local `act` run works out of the box without installing cmake into the runner
image.

## Context

The current workflow runs on `ubuntu-latest` and installs
`libx11-dev libxfixes-dev libxi-dev pkg-config` on every run. Locally, `act`
cannot reproduce this reliably: its default runner images lack cmake (and the
X11 dev packages), and the apt install step depends on sudo/network behavior
inside the image. Project-owned images remove this drift: the Dockerfile
lives in the repo, the image is built and published from the same repo, and
both `act` and GitHub Actions reference the same image tag.

## Scope

### In scope

- A `ci/` directory containing:
  - A Dockerfile building a minimal CI image: base distro (see Decisions),
    compiler with C++20 support, cmake (>= project minimum 3.13), ninja or
    make, pkg-config, and the X11/XFIXES/XI dev packages. Nothing else.
  - A short `ci/README.md` documenting what is installed and how to rebuild
    and publish the image.
- A GitHub Actions workflow (or job) that builds the image and publishes it
  to GHCR (`ghcr.io/<owner>/<repo>/ci`) tagged with `latest` plus the image
  version; publish only on pushes affecting `ci/**` or the workflow, on the
  default branch.
- Updated `.github/workflows/ci.yml`: both jobs run inside the published
  image (`container:` key), drop the apt install steps, keep the build/test
  steps unchanged otherwise.
- Local parity: a documented `act` invocation (in `ci/README.md`) that uses
  the same image (either pulled from GHCR or built locally from the same
  Dockerfile when offline).
- Image versioning scheme and how versions advance (see Decisions).

### Out of scope

- Running the overlay itself inside a container (X server/Xvfb display
  tests) — tests remain the display-free logic tests.
- Multi-arch images beyond linux/amd64 (act and ubuntu runners are amd64).
- Caching optimizations beyond Docker layer caching; registry choice beyond
  GHCR.
- Changing what the CI jobs do (same configure/build/test commands).

## Constraints & Assumptions

- The image must stay minimal: no dev toolchains beyond what the build and
  tests need; target size a few hundred MB at most.
- GHCR publishing needs a `GITHUB_TOKEN` with `packages: write`; assume the
  default workflow token suffices for the repo's own packages.
- `act` must be able to pull from GHCR without special setup for public
  images (repo assumed public; if private, document the docker login step
  instead of working around it).
- The workflows keep using `actions/checkout@v4` inside the container.
- Image rebuilds must be reproducible from the repo alone (Dockerfile pins
  the base image digest or at least a specific version tag, never
  `:latest`).

## Decisions

- Base image: Debian slim (e.g. `debian:12-slim`) — same apt ecosystem as
  the current ubuntu jobs, smallest footprint, g++ ships C++20 support.
- Tagging: `<version>` (e.g. `1`), `latest`, and the git SHA of the
  `ci/**` commit. Bump `<version>` manually in the workflow/Dockerfile when
  toolchain changes are wanted; `latest` always points at the newest
  published image, workflows pin `<version>`.
- The build-and-publish workflow lives in the same repo
  (`.github/workflows/ci-image.yml`) and triggers on changes to `ci/**`.
- The CI jobs pin the image with `<version>` (not `latest`) so a toolchain
  change is an explicit, reviewable bump.

## Acceptance Criteria

1. `ci/Dockerfile` builds successfully (`docker build ci/` or
   `docker build -f ci/Dockerfile .`) and the resulting image can configure,
   build (with `-Wconversion` + `-Werror`), and run `ctest` for this repo.
2. The image contains only: base distro, g++ (C++20-capable), cmake, make
   or ninja, pkg-config, git (needed by actions/checkout), and the X11,
   XFIXES, and XI dev packages.
3. `.github/workflows/ci.yml` runs both jobs in `container: ghcr.io/<owner>/.../ci:<version>` and contains no apt install steps.
4. `act` runs the workflow locally using the same image, and both jobs pass
   without modifications to the workflow for local use (at most an
   `--image`/pull flag or env documented in ci/README.md).
5. `.github/workflows/ci-image.yml` builds the image and pushes
   `<version>`, `latest`, and SHA tags to GHCR on `ci/**` changes to the
   default branch.
6. Base image in the Dockerfile is pinned to a specific version (not
   `:latest`).
7. `ci/README.md` documents: contents of the image, how to build locally,
   how the publish workflow works, how to bump `<version>`, and the exact
   `act` command to run CI locally.
8. A CI run on GitHub using the image passes, proving the published image
   works end to end.

## Verification Plan

- Local: `docker build` the image, run `ctest` inside it (AC 1); run `act`
  against the workflow (AC 4).
- Cloud: push the branch; confirm `ci-image.yml` publishes tags and
  `ci.yml` runs green in containers (AC 5, 8).
- Review `docker history` output or the Dockerfile to confirm AC 2 and 6.
- README review for AC 7.

## Rollback Plan

- Revert the commit series; the workflow returns to `ubuntu-latest` +
  apt install. Published GHCR images are inert leftovers and harmless.

## Open Questions

- Exact GHCR image path spelling (`ghcr.io/<owner>/<repo>-ci` vs a
  namespaced `ghcr.io/<owner>/<repo>/ci`) — GHCR only supports a flat
  package name per repo; settle on `<owner>/<repo>/ci` if the registry
  accepts it, else `<owner>-<repo>-ci`.
- Whether `act` should default to the published image (pull) or a
  locally-built one; default proposal: pull, with a documented
  `--pull=false`-style offline alternative.