package cgo

import (
	"fmt"
	"os"
	"reflect"
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
	payload []byte
	offset  int
	p       C.AVFormatContextPtr
}

// NewAVFormatContext creates an AVFormatContext
func NewAVFormatContext(fmt string, frameCh <-chan *qrpc.Frame) *AVFormatContext {
	ctx := &AVFormatContext{fmt: fmt, frameCh: frameCh}
	fmtCStr := C.CString(fmt)
	ctx.p = C.AVFormat_Open(fmtCStr, C.uintptr_t(uintptr(unsafe.Pointer(ctx))))
	C.free(unsafe.Pointer(fmtCStr))

	return ctx
}

// ReadFrame will call AVIOContext internally
func (ctx *AVFormatContext) ReadFrame() {
	C.AVFormat_ReadFrame(ctx.p)
}

//export read_packet_callback
func read_packet_callback(ioctx unsafe.Pointer, buf *C.char, bufSize C.int) C.int {

	slice := &reflect.SliceHeader{Data: uintptr(unsafe.Pointer(buf)), Len: int(bufSize), Cap: int(bufSize)}

	fmt.Println("bufSize", int(bufSize))
	ctx := (*AVFormatContext)(ioctx)

	return C.int(ctx.fillSlice(*(*[]byte)(unsafe.Pointer(slice))))
}

func (ctx *AVFormatContext) fillSlice(buf []byte) int {
	fmt.Println("fillSlice buf size =", len(buf))

	size := len(buf)

	if ctx.payload != nil {
		if size < len(ctx.payload)-ctx.offset {
			copy(buf, ctx.payload[ctx.offset:ctx.offset+size])
			ctx.offset += size
			return size
		}

		copy(buf, ctx.payload[ctx.offset:])
		copied := len(ctx.payload) - ctx.offset
		ctx.payload = nil
		ctx.offset = 0
		return copied
	}

	frame := <-ctx.frameCh
	if size < len(frame.Payload) {
		copy(buf, frame.Payload[0:size])
		ctx.payload = frame.Payload
		ctx.offset = size
		return size
	}

	if len(frame.Payload) == 0 {
		fmt.Fprintln(os.Stderr, "found empty frame")
		return int(C.GOAVERROR_EINVAL())
	}

	copy(buf, frame.Payload)
	return len(frame.Payload)

}
