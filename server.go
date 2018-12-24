package main

import (
	"github.com/zhiqiangxu/avflow/cmd"
	"github.com/zhiqiangxu/qrpc"
)

func main() {
	handler := qrpc.NewServeMux()
	handler.Handle(cmd.Play, &cmd.PlayCmd{})

	bindings := []qrpc.ServerBinding{
		qrpc.ServerBinding{Addr: "0.0.0.0:8888", Handler: handler, DefaultReadTimeout: 10 /*second*/}}

	qserver := qrpc.NewServer(bindings)
	qserver.ListenAndServe()

}
