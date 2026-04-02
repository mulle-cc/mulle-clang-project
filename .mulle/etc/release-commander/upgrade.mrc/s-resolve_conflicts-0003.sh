#!/bin/bash
# Verify @mulle- marker count is unchanged after cherry-pick.
# Run after manually resolving conflicts and committing.
set -e

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"

cd "${MULLE_CLANG_DIR}/mulle-clang-project"

BEFORE=.before-markers.txt
AFTER=.after-markers.txt

if [ ! -f "${BEFORE}" ]; then
   echo "No ${BEFORE} found — migrate script may not have run yet" >&2
   exit 1
fi

grep -R '@mulle-' clang/include/ clang/lib/ > "${AFTER}"

if ! diff "${BEFORE}" "${AFTER}"; then
   echo "ERROR: @mulle- markers differ — fix conflicts above" >&2
   exit 1
fi

echo "OK: all @mulle- markers intact"
rm -f "${BEFORE}" "${AFTER}"
