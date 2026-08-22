Update `README.md` in `mulle-clang-project` with the new llvm version and
any mulle-specific features added since the last release.

## Steps

1. Open `README.md` and update the version number in the title/header.

2. Review the git log of `mulle/21.1.8` (the old branch) for mulle-specific
   commits since the last squash, to find features worth mentioning:
   ```bash
   git log --oneline squashed_mulle/21.1.8..mulle/21.1.8 -- clang/
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
   git commit -m "Update README for 22.1.2"
   ```
