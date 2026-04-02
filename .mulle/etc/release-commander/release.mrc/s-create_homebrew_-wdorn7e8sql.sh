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
url="https://github.com/mulle-cc/mulle-clang-project/archive/${MULLE_CLANG_PROJECT_TAG}.tar.gz"
file="${MULLE_CLANG_PROJECT_TAG}.tar.gz"
if [ ! -f "${file}" ]
then
   echo "Downloading \"${url}\"..."
   curl -O -L "${url}"
fi

sha256=$(shasum -a 256 -b "${file}" | awk '{ printf $1 }')
echo "sha256=\"${sha256}\""

mulle-replace --regex 'sha256 "[^"]*"' \
                      "sha256 \""${sha256}"\"" \
                      mulle-clang-project.rb

echo "Uninstalling old project (needed)"
brew uninstall mulle-kybernetik/software/mulle-clang-project || true

echo "Creating tap if not present"
brew tap-new mulle-kybernetik/software || true

tap_dir="/usr/local/Homebrew/Library/Taps/mulle-kybernetik/homebrew-software/Formula/"

echo "Copy formula"
cp mulle-clang-project.rb "${tap_dir}/"
   
brew install --formula --build-bottle "${tap_dir}/mulle-clang-project.rb"

# Now it gets retarded:
# brew untap mulle-kybernetik/software
# echo "Refreshing Tap"
# brew tap-new mulle-kybernetik/software

echo "Brew formula"
brew bottle mulle-kybernetik/software/mulle-clang-project

file="mulle-clang-project-${MULLE_CLANG_PROJECT_TAG}.${distro}.bottle.tar.gz"
orig="${file/project-/project--}"

if [ ! "${file}" ]
then
   echo "Bottle is missing \"${file}\""
   exit 1
fi

echo "Bottle produced: ${PWD}/${file}"

mv "${orig}" "${PWD}/${file}"

