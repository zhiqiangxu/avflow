package cmd

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"sync"

	"github.com/zhiqiangxu/avflow/cgo"
	"github.com/zhiqiangxu/qrpc"
)

// PlayCmd for video
type PlayCmd struct {
	sync.RWMutex
	fCtxMap map[string]*cgo.AVFormatContext
}

// PlayRequest is param for PlayCmd
type PlayRequest struct {
	ID   string `json:"id"`
	Pass string `json:"pass"`
}

// NewPlayCmd creates PlayCmd
func NewPlayCmd() *PlayCmd {
	return &PlayCmd{fCtxMap: make(map[string]*cgo.AVFormatContext)}
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

// ServeQRPC implements qrpc.Handler
func (cmd *PlayCmd) ServeQRPC(writer qrpc.FrameWriter, frame *qrpc.RequestFrame) {
	fmt.Println("PlayCmd start, payload =", string(frame.Payload))

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
	cmd.Lock()
	if _, ok := cmd.fCtxMap[req.ID]; ok {
		cmd.Unlock()
		fmt.Println("publishing twice:", req.ID)
		frame.Close()
		return
	}
	cmd.fCtxMap[req.ID] = nil
	cmd.Unlock()
	defer func() {
		cmd.Lock()
		delete(cmd.fCtxMap, req.ID)
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

	fCtx := cgo.NewAVFormatContext("h264", frame.FrameCh())
	defer fCtx.Free()

	cmd.Lock()
	cmd.fCtxMap[req.ID] = fCtx
	cmd.Unlock()

	for {
		// fmt.Println("before ReadFrame")
		fCtx.ReadFrame()
		// fmt.Println("after ReadFrame")
	}

}
