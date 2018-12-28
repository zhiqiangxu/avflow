package cgo

import (
	"fmt"
	"io"
	"os"
	"reflect"
	"sync"
	"sync/atomic"
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

const (
	maxErrSize = 30
)

// AVFormatQrpcContext wrapper for go
type AVFormatQrpcContext struct {
	sequence uint64
	lock     sync.Mutex
	s2w      map[uint64]io.Writer
	w2s      map[io.Writer]uint64
	fmt      string
	frameCh  <-chan *qrpc.Frame
	payload  []byte
	offset   int
	p        C.AVFormatContextPtr
	freed    int32
}

// NewAVFormatQrpcContext creates an AVFormatQrpcContext
func NewAVFormatQrpcContext(fmt string, frameCh <-chan *qrpc.Frame) *AVFormatQrpcContext {
	ctx := &AVFormatQrpcContext{fmt: fmt, frameCh: frameCh, s2w: make(map[uint64]io.Writer), w2s: make(map[io.Writer]uint64)}
	fmtCStr := C.CString(fmt)
	ctx.p = C.AVFormat_Open(fmtCStr, C.uintptr_t(uintptr(unsafe.Pointer(ctx))))
	C.free(unsafe.Pointer(fmtCStr))

	return ctx
}

// Free the AVFormatQrpcContext
func (ctx *AVFormatQrpcContext) Free() {
	atomic.StoreInt32(&ctx.freed, 1)
	C.AVFormat_Free(ctx.p)
}

// ReadFrame will call AVIOContext internally
func (ctx *AVFormatQrpcContext) ReadFrame() error {
	ret := int(C.AVFormat_ReadFrame(ctx.p))
	if ret == 0 || ret == int(C.GOAVERROR_EAGAIN) {
		return nil
	}

	errBuf := make([]byte, maxErrSize)
	C.AV_STRERROR(C.int(ret), (*C.char)(unsafe.Pointer(&errBuf[0])), C.int(len(errBuf)))
	return fmt.Errorf("%s", errBuf)
}

// ReadLatestVideoFrame for snapshot
func (ctx *AVFormatQrpcContext) ReadLatestVideoFrame(ofmt string, w io.Writer) error {
	seq := atomic.AddUint64(&ctx.sequence, 1)
	ctx.lock.Lock()
	ctx.s2w[seq] = w
	ctx.lock.Unlock()

	fmtCStr := C.CString(ofmt)
	ret := int(C.AVFormat_ReadLatestVideoFrame(ctx.p, fmtCStr, C.uint64_t(seq)))
	C.free(unsafe.Pointer(fmtCStr))

	ctx.lock.Lock()
	delete(ctx.s2w, seq)
	ctx.lock.Unlock()
	if ret == 0 {
		return nil
	}

	errBuf := make([]byte, maxErrSize)
	C.AV_STRERROR(C.int(ret), (*C.char)(unsafe.Pointer(&errBuf[0])), C.int(len(errBuf)))
	return fmt.Errorf("%s", errBuf)

}

// SubcribeAVFrame for video
func (ctx *AVFormatQrpcContext) SubcribeAVFrame(ofmt string, w io.Writer) error {
	seq := atomic.AddUint64(&ctx.sequence, 1)
	ctx.lock.Lock()
	ctx.s2w[seq] = w
	ctx.w2s[w] = seq
	ctx.lock.Unlock()

	fmtCStr := C.CString(ofmt)
	ret := int(C.AVFormat_SubcribeAVFrame(ctx.p, fmtCStr, C.uint64_t(seq)))
	C.free(unsafe.Pointer(fmtCStr))

	if ret == 0 {
		return nil
	}

	ctx.lock.Lock()
	delete(ctx.s2w, seq)
	delete(ctx.w2s, w)
	ctx.lock.Unlock()

	errBuf := make([]byte, maxErrSize)
	C.AV_STRERROR(C.int(ret), (*C.char)(unsafe.Pointer(&errBuf[0])), C.int(len(errBuf)))
	return fmt.Errorf("%s", errBuf)
}

// UnsubcribeAVFrame for stop watch
func (ctx *AVFormatQrpcContext) UnsubcribeAVFrame(w io.Writer) {
	ctx.lock.Lock()
	seq, ok := ctx.w2s[w]
	if ok {
		delete(ctx.s2w, seq)
		delete(ctx.w2s, w)
	}
	ctx.lock.Unlock()
	C.AVFormat_UnsubcribeAVFrame(ctx.p, C.uint64_t(seq))
}

//export read_latest_callback
func read_latest_callback(ioctx unsafe.Pointer, seq C.uint64_t, buf *C.char, bufSize C.int) C.int {
	slice := &reflect.SliceHeader{Data: uintptr(unsafe.Pointer(buf)), Len: int(bufSize), Cap: int(bufSize)}

	goseq := uint64(seq)
	ctx := (*AVFormatQrpcContext)(ioctx)
	ctx.lock.Lock()
	w := ctx.s2w[goseq]
	ctx.lock.Unlock()
	if w == nil {
		fmt.Fprintf(os.Stderr, "no writer for seq:%d", goseq)
		return C.int(-1)
	}
	_, err := w.Write(*(*[]byte)(unsafe.Pointer(slice)))
	if err != nil {
		return -1
	}
	return C.int(0)
}

//export write_packet_callback
func write_packet_callback(ioctx unsafe.Pointer, seq uint64, buf *C.char, bufSize C.int) C.int {
	return C.int(-1)
}

//export read_packet_callback
func read_packet_callback(ioctx unsafe.Pointer, buf *C.char, bufSize C.int) C.int {

	slice := &reflect.SliceHeader{Data: uintptr(unsafe.Pointer(buf)), Len: int(bufSize), Cap: int(bufSize)}

	// fmt.Println("bufSize", int(bufSize))
	ctx := (*AVFormatQrpcContext)(ioctx)

	return C.int(ctx.fillSlice(*(*[]byte)(unsafe.Pointer(slice))))
}

func (ctx *AVFormatQrpcContext) fillSlice(buf []byte) int {
	// fmt.Println("fillSlice buf size =", len(buf))

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
	if frame == nil {
		return int(C.GOAVERROR_EOF)
	}
	if size < len(frame.Payload) {
		copy(buf, frame.Payload[0:size])
		ctx.payload = frame.Payload
		ctx.offset = size
		return size
	}

	if len(frame.Payload) == 0 {
		fmt.Fprintln(os.Stderr, "found empty frame")
		return int(C.GOAVERROR_EINVAL)
	}

	copy(buf, frame.Payload)
	return len(frame.Payload)

}
