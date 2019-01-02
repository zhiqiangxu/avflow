package cmd

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"sync"

	"github.com/zhiqiangxu/avflow/cgo"
	w "github.com/zhiqiangxu/avflow/pkg/writer"
	"github.com/zhiqiangxu/qrpc"
)

// PlayCmd for video
type PlayCmd struct {
	sync.RWMutex
	fCtxMap map[string]*cgo.AVFormatQrpcContext
}

// PlayRequest is param for PlayCmd
type PlayRequest struct {
	Publish int    `json:"publish"`
	URI     string `json:"uri"`
}

// NewPlayCmd creates PlayCmd
func NewPlayCmd() *PlayCmd {
	return &PlayCmd{fCtxMap: make(map[string]*cgo.AVFormatQrpcContext)}
}

var (
	// ErrNotPlaying when requested id is not playing
	ErrNotPlaying = errors.New("request id is not playing")
)

// ReadLatestVideoFrame latest video frame of some id
func (cmd *PlayCmd) ReadLatestVideoFrame(id, fmt string, w io.Writer) error {
	cmd.RLock()
	fCtx := cmd.fCtxMap[id]
	cmd.RUnlock()
	if fCtx == nil {
		return ErrNotPlaying
	}

	return fCtx.ReadLatestVideoFrame(fmt, w)
}

// SubcribeAVFrame to id with fmt
func (cmd *PlayCmd) SubcribeAVFrame(id, fmt string, w io.Writer) error {
	cmd.RLock()
	fCtx := cmd.fCtxMap[id]
	cmd.RUnlock()
	if fCtx == nil {
		return ErrNotPlaying
	}

	return fCtx.SubcribeAVFrame(fmt, w)
}

// UnsubcribeAVFrame w from id
func (cmd *PlayCmd) UnsubcribeAVFrame(id string, w io.Writer) {
	cmd.RLock()
	fCtx := cmd.fCtxMap[id]
	cmd.RUnlock()
	if fCtx == nil {
		return
	}

	fCtx.UnsubcribeAVFrame(w)
}

// ServeQRPC implements qrpc.Handler
func (cmd *PlayCmd) ServeQRPC(writer qrpc.FrameWriter, frame *qrpc.RequestFrame) {
	fmt.Println("PlayCmd start, payload =", string(frame.Payload))

	ci := frame.ConnectionInfo()
	sc := ci.SC
	if sc.GetID() == "" {
		fmt.Println("Should auth first")
		frame.Close()
		return
	}

	req := PlayRequest{}
	if frame.Flags&qrpc.StreamFlag == 0 {
		fmt.Println("PlayCmd must be streaming")
		frame.Close()
		return
	}
	err := json.Unmarshal(frame.Payload, &req)
	if err != nil {
		fmt.Println("Unmarshal", err)
		frame.Close()
		return
	}

	if req.Publish == 0 {
		if req.URI == "" {
			fmt.Println("uri empty")
			frame.Close()
			return
		}
		id := req.URI[1:len(req.URI)]
		fmt.Println("id = ", id)

		cmd.Lock()
		fCtx := cmd.fCtxMap[id]
		cmd.Unlock()

		if fCtx == nil {
			fmt.Println("requested id not playing", id)
			frame.Close()
			return
		}

		writer.StartWrite(frame.RequestID, PlayResp, qrpc.StreamFlag)
		writer.WriteBytes([]byte("OK"))
		err = writer.EndWrite()
		if err != nil {
			fmt.Println("EndWrite", err)
			frame.Close()
			return
		}

		err := fCtx.SubcribeAVFrame("mpegts", w.NewQrpcWriter(writer, frame, PlayResp))
		if err != nil {
			fmt.Println("SubcribeAVFrame", err)
			frame.Close()
			return
		}
		fmt.Println("SubcribeAVFrame ok")
		<-frame.Context().Done()
		fmt.Println("done")
		return
	}

	var fCtx *cgo.AVFormatQrpcContext
	defer func() {
		if fCtx != nil {
			// AVFormatQrpcContext must be freed last
			fCtx.Free()
		}
	}()

	cmd.Lock()
	if _, ok := cmd.fCtxMap[sc.GetID()]; ok {
		cmd.Unlock()
		fmt.Println("publishing twice:", sc.GetID())
		frame.Close()
		return
	}
	cmd.fCtxMap[sc.GetID()] = nil
	cmd.Unlock()
	defer func() {
		cmd.Lock()
		delete(cmd.fCtxMap, sc.GetID())
		cmd.Unlock()
	}()

	writer.StartWrite(frame.RequestID, PlayResp, qrpc.StreamFlag)
	writer.WriteBytes([]byte("OK"))
	err = writer.EndWrite()
	if err != nil {
		fmt.Println("EndWrite", err)
		frame.Close()
		return
	}

	fCtx = cgo.NewAVFormatQrpcContext("mpegts", frame.FrameCh())

	cmd.Lock()
	cmd.fCtxMap[sc.GetID()] = fCtx
	cmd.Unlock()

	for {
		// fmt.Println("before ReadFrame")
		err := fCtx.ReadFrame()
		if err != nil {
			fmt.Printf("ReadFrame return: %s", err.Error())
			return
		}
		// fmt.Println("after ReadFrame")
	}

}
