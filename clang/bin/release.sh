#!/bin/bash

cd .mulle/etc/release-commander/release.mrc || exit 1
exec aws-kiro -- chat --agent commander
