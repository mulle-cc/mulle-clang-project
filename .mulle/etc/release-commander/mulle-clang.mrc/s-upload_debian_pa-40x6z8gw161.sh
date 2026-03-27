#!/bin/bash

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

file="mulle-clang-project-${MULLE_CLANG_PROJECT_TAG}.${distro}.bottle.tar.gz"

gh release upload --clobber "${MULLE_CLANG_PROJECT_TAG}" \
                  --repo mulle-cc/mulle-clang-project \
                  "${file}"

sha256="$(shasum -a 256 -b "${file}" | awk '{ print $1 }')"
root_url="https://github.com/mulle-cc/mulle-clang-project/releases/download/${MULLE_CLANG_PROJECT_TAG}"
      
# add this to formula
#     root_url "https://github.com/mulle-cc/mulle-clang-project/releases/download/17.0.6.0"
#    sha256 cellar: :any, sonoma: "aac00f815b7234abf66ac2bb868cc58630f7c2dce325ec351b07d5c5771f267a"

sed -i '' "/bottle do/a\\"$'\n'"  root_url \"${root_url}\"\\"$'\n'"  sha256 cellar: :any, ${distro}: \"${sha256}\""$'\n' mulle-clang-project.rb

git add mulle-clang-project.rb
git commit -m "new mulle-clang-project ${MULLE_CLANG_PROJECT_TAG}" mulle-clang-project.rb
git push origin master
