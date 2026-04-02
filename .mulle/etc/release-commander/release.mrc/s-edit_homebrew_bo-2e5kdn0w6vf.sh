#!/usr/bin/env mulle-bash

set -e 

[ ! -z "${MULLE_OBJC_RUNTIME_LOAD_VERSION}" ]

cd /Volumes/Source/srcM/homebrew-prerelease

old_tag="$(sed -n 's/.*version "\([0-9.][0-9.]*\)".*/\1/p' mulle-clang-project.rb | head -1)"
old_version="${old_tag%\.*}"

echo "old_tag=${old_tag}"
echo "old_version=${old_version}"

mulle-replace "${old_tag}" "${MULLE_CLANG_PROJECT_TAG}" mulle-clang-project.rb
mulle-replace "${old_version}" "${MULLE_CLANG_PROJECT_TAG%\.*}" mulle-clang-project.rb

# lowercase distroname
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

escaped="${MULLE_CLANG_PROJECT_TAG//./\\.}"
old_distro="$(sed -n 's/.*-'"${escaped}"'\.\([^.]*\)\.bottle\..*/\1/p' mulle-clang-project.rb | head -1)"

echo "distro=${old_distro}"
echo "old_distro=${old_distro}"

[ -z "${old_distro}" ] && exit 1

mulle-replace --word "${old_distro}" "${distro}" mulle-clang-project.rb

mulle-replace --regex 'runtime-load-version: \([0-9][0-9]*\)' "runtime-load-version: ${MULLE_OBJC_RUNTIME_LOAD_VERSION}" mulle-clang-project.rb

#
# get rid of old bottle info from Formula
#
sed -I "" '/^ *bottle *do/,/^ *end *$/ { 
           /^ *bottle *do/p; 
           /^ *end *$/p; 
           d; 
           }' mulle-clang-project.rb  


