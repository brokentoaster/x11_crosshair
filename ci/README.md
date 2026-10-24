# CI container image

The CI workflows (`.github/workflows/ci.yml`) run in the image built from
this directory, so local `act` runs and GitHub Actions use the same
environment.

## What is installed

`debian:12-slim` plus: `g++`, `cmake`, `make`, `pkg-config`, `git`,
`ca-certificates`, and the `libx11-dev`, `libxfixes-dev`, `libxi-dev` dev
packages. Nothing else by design — keep the image minimal.

## Image name and tags

- `ghcr.io/brokentoaster/x11-crosshair-ci:<version>` (currently `1`) — what `ci.yml`
  pins.
- `:latest` — newest published image.
- `:<git-sha>` — exact provenance for each publish.

## Build locally

```sh
docker build -f ci/Dockerfile -t ghcr.io/brokentoaster/x11-crosshair-ci:1 .
# If DNS fails inside the build, use: docker build --network=host ...
```

Tagging it with the GHCR name locally is enough for `act` to find it
(`act --pull=false` uses the local image instead of pulling).

## Publish

`.github/workflows/ci-image.yml` triggers on pushes to `main` that touch
`ci/**` (or the workflow itself), and pushes the three tags above to GHCR
using the workflow token (`packages: write`). Manual trigger:
`workflow_dispatch`.

## Bump the version

Change `VERSION` in `ci-image.yml` **and** the `container.image` tag in
`ci.yml` together (e.g. `1` → `2`) when the toolchain changes. Both files
must agree; the CI jobs deliberately pin a version instead of `latest` so a
toolchain change is an explicit, reviewable commit.

## Run CI locally with act

```sh
# Preferred once the image is on GHCR (or already tagged locally):
act push

# Offline / before first publish: build locally with the same tag, then:
act --pull=false push

# Single job:
act --pull=false push -j asan
```

Note: on hosts where docker builds cannot resolve DNS, add `--network=host`
to `docker build` (see above); `act` job containers run the workflow steps,
not the build, so this does not affect them.