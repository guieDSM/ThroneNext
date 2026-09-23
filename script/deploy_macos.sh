#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy golang => .app ####
source "$(dirname "$0")/extract_core_artifact.sh"

mv deployment/$DEST_SUFFIX/* $GITHUB_WORKSPACE/build/Throne.app/Contents/MacOS

#### deploy qt & Dylib runtime => .app ####
pushd $GITHUB_WORKSPACE/build
macdeployqt Throne.app -verbose=3
popd

codesign --force --deep --sign - $GITHUB_WORKSPACE/build/Throne.app

dsymutil $GITHUB_WORKSPACE/build/Throne.app/Contents/MacOS/Throne
strip -S $GITHUB_WORKSPACE/build/Throne.app/Contents/MacOS/Throne

mv $GITHUB_WORKSPACE/build/Throne.app $DEST
