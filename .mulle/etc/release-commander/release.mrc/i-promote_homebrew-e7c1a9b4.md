## Promote the homebrew formula from prerelease to the stable tap

The macOS steps above only ever push `mulle-clang-project.rb` to
`mulle-objc/homebrew-prerelease`. Without this step the stable taps
(`mulle-objc/homebrew-software` and `mulle-kybernetik/homebrew-software`) stay
frozen on the previous compiler, so
`brew install mulle-objc/software/mulle-clang-project` keeps installing the old
version. This was the 22.1.8.7 oversight.

This step copies the formula (version, source sha256, `bottle do` block) from
the prerelease tap to **both** stable taps and normalises the
`runtime-load-version` string to `${MULLE_OBJC_RUNTIME_LOAD_VERSION}`. The
prerelease formula is hand-edited and drifts — e.g. for 22.1.8.7 it said `19`
while the compiler actually reports `21` (`mulle-clang --version`).

Run `./s-promote_homebrew-e7c1a9b4.sh`.

Notes:

* Requires `gh` to be authenticated with push access to both tap repos.
* If this release has **no** macOS bottles (the three macOS steps above were
  skipped), mark this step `skipped` too — there is nothing new to promote.
* The script fails if the prerelease formula is not already at
  `${MULLE_CLANG_PROJECT_TAG}`. Fix the prerelease tap first.
* Do not edit the stable taps by hand; this step keeps both identical.
