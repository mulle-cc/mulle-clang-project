# Write GitHub release notes for the mulle-clang compiler

Fill in the GitHub release body for `MULLE_CLANG_PROJECT_TAG` on
`mulle-cc/mulle-clang-project`, in the established style (see release
`21.1.8.4` for the feature-list format and release `22.1.8.7` for the
Installation section).

## Style reference (21.1.8.4)

```
This is a feature and bug fix release

* requires mulle-objc-runtime 0.24 or better
* requires load version 18
* adds `__attribute__((mulle_confined_loop))`
* adds `__attribute__((mulle_confined_return))`
* no compiler runtimes (e.g. ASAN) on the macOS platform due to some cmake misconfiguration (not my problem)
* ...

mulle-clang-project-windows-21.1.8.4.tgz is a cross-compiler ...

If you want pre-built binaries or a bottle for some platform, put up an "issue".
```

## Installation section — REQUIRED

Starting with this release the body must include the `## Installation` block
that first appeared on the `22.1.8.7` release page, with the version
substituted. It contains:

* `### Debian / Ubuntu (`.deb`)` — a table of the codenames **actually present**
  on the release (built from the `.deb` assets), the `curl`/`apt-get` snippet
  using `version=${MULLE_CLANG_PROJECT_TAG}`, where the toolchain is installed,
  and the forky no-runtimes note.
* `### macOS (Homebrew)` — the `brew install` line plus the bottle tags
  **actually attached** (built from the `*.bottle.tar.gz` assets).
* `### Verify` — `mulle-clang --version`.
* `### From source` — the `git clone --branch <version>` /
  `clang/bin/cmake-ninja.linux` snippet.

The script builds this section from the release's actual assets, so never list a
distro or a bottle that is not there.

## Authoritative facts — NEVER guess

Only write lines you can verify from authoritative sources:

| Fact | Authoritative source |
|---|---|
| Required runtime version | parent `MULLE_RELEASE_TAG` (e.g. `0.30`); note "mulle-objc-runtime 0.30 or better" |
| Load version | `COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION` in `clang/include/clang/Basic/ObjCRuntime.h` |
| Prebuilt .deb distros present | run `gh release view <VER> --json assets -q '.assets[].name'` and list the `.deb` codenames actually present |
| Forky no-sanitizers note | the forky `.deb` was built with `-DLLVM_ENABLE_RUNTIMES=` (no ASAN/compiler-rt). State only that it has no compiler runtimes — do NOT add an explanatory "because" clause unless (and only if) you can verify it from source
| Windows cross-compiler .tgz | check `gh release` assets and/or `/opt/mulle-clang-project-windows/` — include the windows instructions ONLY if a matching tgz actually exists; otherwise omit |
| Attribute additions | only claim a specific `__attribute__` if you can verify it was newly added in this version |

## Steps

1. Read `COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION`.
2. `gh release view "${MULLE_CLANG_PROJECT_TAG}" --repo mulle-cc/mulle-clang-project --json assets -q '.assets[].name'` to see which distros/debs are deployed.
3. Compose the body in the reference style with only verifiable lines, including
   the required `## Installation` section (see above).
4. Set it. The body contains backticks and `${...}` literals, so write it to a
   file and use `--notes-file` (do NOT pass it through `--notes`, shell quoting
   will mangle it):
   ```
   gh release edit "${MULLE_CLANG_PROJECT_TAG}" --repo mulle-cc/mulle-clang-project --notes-file "<FILE>"
   ```
5. Verify with `gh release view ... --json body -q '.body'`, including that the
   body contains `## Installation`.

Do NOT invent features or claim a Windows tgz if it does not exist.