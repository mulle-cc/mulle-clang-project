#!/usr/bin/env bash
# Update mulle-clang-mingw to the new compiler version

[ "${TRACE}" = 'YES' ] && set -x

: "${MULLE_CLANG_PROJECT_TAG:?must be set}"

REPO="${MULLE_REPOS_DIR}/mulle-cc/mulle-clang-mingw"

if [ ! -d "$REPO" ]
then
   git clone git@github.com:mulle-cc/mulle-clang-mingw.git "$REPO" || exit 1
fi

cd "$REPO" || exit 1
git pull || exit 1

# Derive the llvmorg tag: strip patch version (22.1.2.2 -> llvmorg-22.1.2)
LLVM_ORG_VERSION="llvmorg-${MULLE_CLANG_PROJECT_TAG%.*}"

# Update build-llvm.sh default version
sed -i "s/: \${LLVM_VERSION:=llvmorg-[0-9.]*/: \${LLVM_VERSION:=${LLVM_ORG_VERSION}/" build-llvm.sh

# Update README.md
sed -i "s/LLVM_VERSION=[0-9][0-9.]*/LLVM_VERSION=${MULLE_CLANG_PROJECT_TAG}/g" README.md
sed -i "s|mulle-clang-project-windows/[0-9][0-9.]*|mulle-clang-project-windows/${MULLE_CLANG_PROJECT_TAG}|g" README.md
sed -i "s|ln -sfn [0-9][0-9.]* latest|ln -sfn ${MULLE_CLANG_PROJECT_TAG} latest|g" README.md

git add build-llvm.sh README.md
git commit -m "new compiler version ${MULLE_CLANG_PROJECT_TAG}

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>" || echo "nothing to commit"
git push
