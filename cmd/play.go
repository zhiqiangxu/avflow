package cmd

import (
	"encoding/json"
	"fmt"

	"github.com/zhiqiangxu/avflow/cgo"
	"github.com/zhiqiangxu/qrpc"
)

// PlayCmd for video
type PlayCmd struct {
}

// PlayRequest is param for PlayCmd
type PlayRequest struct {
	ID   string `json:"id"`
	Pass string `json:"pass"`
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

	writer.StartWrite(frame.RequestID, PlayResp, qrpc.StreamFlag)
	writer.WriteBytes([]byte("OK"))
	err = writer.EndWrite()
	if err != nil {
		fmt.Println("EndWrite", err)
		frame.Close()
		return
	}

	fCtx := cgo.NewAVFormatContext("h264", frame.FrameCh())

	for {
		fmt.Println("before ReadFrame")
		fCtx.ReadFrame()
		fmt.Println("after ReadFrame")
	}

}
