#!/usr/bin/env mulle-bash

set -e 

cd /Volumes/Source/srcM/homebrew-prerelease

version=$(sw_vers -productVersion | cut -d. -f1)
case $version in
    11) distro="bigsur" ;;
    12) distro="monterey" ;;
    13) distro="ventura" ;;
    14) distro="sonoma" ;;
    15) distro="sequoia" ;;
    26) distro="tahoe" ;;
    *) echo "Unknown macOS version" && exit 1 ;;
esac

# now download tar.gz and compute checksum
curl -O -L "https://github.com/mulle-cc/mulle-clang-project/archive/${MULLE_CLANG_PROJECT_VERSION}.tar.gz"

sha256=$(shasum -a 256 -b ${MULLE_CLANG_PROJECT_VERSION}.tar.gz)

mulle-replace --regex 'sha256 "[^"]*"' \
                      "sha256 \""${sha256}"\"" \
                      mulle-clang-project.rb

brew uninstall mulle-kybernetik/software/mulle-clang-project
brew install --formula --build-bottle mulle-clang-project.rb

# Now it gets retarded:
brew untap mulle-kybernetik/software
brew tap-new mulle-kybernetik/software
cp mulle-clang-project.rb /usr/local/Homebrew/Library/Taps/mulle-kybernetik/homebrew-software/Formula/

brew bottle mulle-kybernetik/software/mulle-clang-project

mv ./mulle-clang-project--${MULLE_OBJC_RUNTIME_LOAD_VERSION}.${distro}.bottle.tar.gz  \
   ./mulle-clang-project-${MULLE_OBJC_RUNTIME_LOAD_VERSION}.${distro}.bottle.tar.gz

