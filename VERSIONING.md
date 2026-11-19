# Versioning

This library follows a three-part version scheme, `X.Y.Z`
(`Major.Minor.Patch`).

## What each part means

- **X — Major.** A significant rewrite or redesign of the library. Adopting a
  new major version may require effort on your part. Major versions are
  expected to be rare.
- **Y — Minor.** A moderate change that may need your attention, such as a
  significant feature addition, a change in default behavior, a change to
  supported platforms, compilers, or build requirements, or the removal of
  previously deprecated APIs. A minor bump may also include a
  backwards-incompatible ABI change; when it does, this is noted and explained
  in the release notes. Minor updates are typically low effort to adopt:
  consumers need to rebuild against the new headers, but source code changes
  are typically not required.
- **Z — Patch.** A small change, such as a bug fix, a new option or function on
  an existing API, or an update to the bundled C libraries. A patch release is
  not expected to break backwards compatibility and may warn of an upcoming
  change. Patch updates can be picked up automatically.

## Public API

This policy covers the headers under `include/aws/crt/` and `include/aws/iot/`.
It does not cover:

- Headers under `private/` directories and anything documented as for internal
  use.
- The bundled C libraries.
- APIs documented as experimental, which may change or be removed in any
  release.

## Deprecation

APIs planned for removal are first marked with a `@deprecated` note naming the
replacement, where one exists. Deprecated APIs continue to work for a period of
time and may be removed in a later minor release. Removals are called out in the
release notes.

## Bundled C libraries

This library bundles the AWS Common Runtime C libraries (`aws-c-*`,
`aws-checksums`, `aws-lc`, `s2n-tls`). Updates to these libraries ship as part
of this library's releases and use the version part that best reflects their
impact on you.

## Branch stability

Untagged branches (for example, `main`) are **not** subject to any API or ABI
stability policy. Stability guarantees apply only to tagged releases.

## What this means for you

We recommend running the latest release. Use the version parts as a guide to how
much effort an upgrade requires:

- **Major (X):** expect adoption effort; review the release notes.
- **Minor (Y):** low effort; rebuild against the new headers and review the
  release notes for changed defaults, removed APIs, and build requirement
  changes.
- **Patch (Z):** pick up automatically.

Addressing deprecation notes as they appear keeps most minor upgrades to a
rebuild.
