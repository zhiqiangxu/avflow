#!/bin/bash

set -e
set -x

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"
BUILD_DIR="$DIR/build"
pushd "$DIR/remote"

./configure \
	--prefix="$BUILD_DIR" \
	--pkg-config-flags="--static"  \
	--extra-libs="-lpthread -lm" \
	--bindir="$BUILD_DIR/bin" \
	 && make -j 4 && make install


popd
