package cmd

import "github.com/zhiqiangxu/qrpc"

const (
	// Auth for authentication
	Auth qrpc.Cmd = iota + 1
	// AuthResp is resp for Auth
	AuthResp
	// Play for real time streaming
	Play
	// PlayResp is resp for Play
	PlayResp
)
