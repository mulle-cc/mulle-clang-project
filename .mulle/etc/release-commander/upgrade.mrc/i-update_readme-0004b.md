Update `README.md` in `mulle-clang-project` with the new llvm version and
any mulle-specific features added since the last release.

## Steps

1. Open `README.md` and update the version number in the title/header line:
   ```
   # mulle-clang
   This is an Objective-C compiler based on clang <new-version>, ...
   ```

2. Review the git log of the old dev branch for mulle-specific commits
   since the last squash, to find features worth mentioning:
   ```bash
   OLD_BRANCH="$(grep '^OLD_MULLE_DEV_BRANCH=' clang/bin/migrate-to-next-release | cut -d'"' -f2)"
   git log --oneline "squashed_${OLD_BRANCH}..${OLD_BRANCH}" -- clang/
   ```

3. Add a changelog entry or update the features section. Typical things to
   document:
   - New compiler flags added (`-Wmulle-*`, `--mulle-objc-*`)
   - New warnings or diagnostics
   - MetaABI changes
   - Rewriter improvements

4. Commit:
   ```bash
   git add README.md
   git commit -m "Update README for <new-version>"
   ```

## Patch bumps

For same-major patch bumps (e.g. 22.1.2 → 22.1.8), step 2 usually produces
no output — there are no new mulle features between patch releases, just
upstream LLVM fixes. In that case, only the version string in step 1 needs
updating.