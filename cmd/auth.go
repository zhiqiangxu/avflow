package cmd

import (
	"encoding/json"
	"fmt"

	"github.com/zhiqiangxu/qrpc"
)

// AuthCmd for authentication
type AuthCmd struct {
}

// NewAuthCmd creates AuthCmd
func NewAuthCmd() *AuthCmd {
	return &AuthCmd{}
}

// AuthRequest is param for AuthCmd
type AuthRequest struct {
	ID   string `json:"id"`
	Pass string `json:"pass"`
}

// ServeQRPC implements qrpc.Handler
func (cmd *AuthCmd) ServeQRPC(writer qrpc.FrameWriter, frame *qrpc.RequestFrame) {
	fmt.Println("AuthCmd start, payload =", string(frame.Payload))

	req := AuthRequest{}
	if frame.Flags&qrpc.StreamFlag != 0 {
		fmt.Println("AuthCmd must be non-streaming")
		frame.Close()
		return
	}
	err := json.Unmarshal(frame.Payload, &req)
	if err != nil {
		fmt.Println("Unmarshal", err)
		frame.Close()
		return
	}

	// TODO actually authentication

	ci := frame.ConnectionInfo()
	sc := ci.SC
	sc.SetID(req.ID)
	sc.Reader().SetReadTimeout(0)
	// ci.SetAnything()

	writer.StartWrite(frame.RequestID, AuthResp, 0)
	writer.WriteBytes([]byte("OK"))
	err = writer.EndWrite()
	if err != nil {
		fmt.Println("EndWrite", err)
		frame.Close()
		return
	}

}
