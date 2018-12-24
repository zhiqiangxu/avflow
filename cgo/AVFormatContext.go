package cgo

import (
	"fmt"
	"unsafe"

	"github.com/zhiqiangxu/qrpc"
)

// #cgo CFLAGS: -I${SRCDIR}/../vendor/ffmpeg/build/include
// #cgo LDFLAGS: ${SRCDIR}/../vendor/ffmpeg/build/lib/libavcodec.a
// #cgo LDFLAGS: ${SRCDIR}/../vendor/ffmpeg/build/lib/libavdevice.a
// #cgo LDFLAGS: ${SRCDIR}/../vendor/ffmpeg/build/lib/libavfilter.a
// #cgo LDFLAGS: ${SRCDIR}/../vendor/ffmpeg/build/lib/libavformat.a
// #cgo LDFLAGS: ${SRCDIR}/../vendor/ffmpeg/build/lib/libavutil.a
// #cgo LDFLAGS: ${SRCDIR}/../vendor/ffmpeg/build/lib/libpostproc.a
// #cgo LDFLAGS: ${SRCDIR}/../vendor/ffmpeg/build/lib/libswresample.a
// #cgo LDFLAGS: ${SRCDIR}/../vendor/ffmpeg/build/lib/libswscale.a
// #cgo pkg-config: libavcodec libavdevice libavfilter libavformat libavutil libpostproc libswresample libswscale
// #include "utils.h"
import "C"

// AVFormatContext wrapper for go
type AVFormatContext struct {
	fmt     string
	frameCh <-chan *qrpc.Frame
	p       C.AVFormatContextPtr
}

// NewAVFormatContext creates an AVFormatContext
func NewAVFormatContext(fmt string, frameCh <-chan *qrpc.Frame) *AVFormatContext {
	ctx := &AVFormatContext{fmt: fmt, frameCh: frameCh}
	fmtCStr := C.CString(fmt)
	ctx.p = C.AVFormat_Open(fmtCStr, unsafe.Pointer(ctx))
	C.free(unsafe.Pointer(fmtCStr))

	return ctx
}

// ReadFrame will call AVIOContext internally
func (fctx *AVFormatContext) ReadFrame() {
	frame := <-fctx.frameCh
	fmt.Println("len", len(frame.Payload))
	C.AVFormat_ReadFrame(fctx.p)
}
