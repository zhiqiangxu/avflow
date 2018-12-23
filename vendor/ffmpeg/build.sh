#!/bin/bash

set -e
set -x

echo "Doing build..."

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"
BUILD_DIR="$DIR/build"
pushd "$DIR/remote"

git reset --hard
git clean -f

cp ../qrpc/qrpc* libavformat/
sed -i '' '/#include "libavformat\/protocol_list.c"/i \
extern const URLProtocol ff_qrpc_protocol;\                                
' libavformat/protocols.c

sed -i '' '/+= tcp.o/a \
OBJS += qrpc.o qrpcpkt.o\
' libavformat/Makefile
./configure \
	--prefix="$BUILD_DIR" \
	--pkg-config-flags="--static"  \
	--extra-libs="-lpthread -lm" \
	--bindir="$BUILD_DIR/bin" \
	 && make -j 4 && make install


git reset --hard
git clean -f

popd
