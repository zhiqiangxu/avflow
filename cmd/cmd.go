package cmd

import "github.com/zhiqiangxu/qrpc"

const (
	// Play for real time video
	Play qrpc.Cmd = iota + 1
	// PlayResp is resp for PlayCmd
	PlayResp
)
