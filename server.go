package main

import (
	"bytes"
	"fmt"
	"net/http"
	"strconv"

	"github.com/zhiqiangxu/avflow/cmd"
	"github.com/zhiqiangxu/qrpc"
)

func main() {
	handler := qrpc.NewServeMux()
	playCmd := cmd.NewPlayCmd()

	go startHTTP(playCmd)

	handler.Handle(cmd.Play, playCmd)

	bindings := []qrpc.ServerBinding{
		qrpc.ServerBinding{Addr: "0.0.0.0:8888", Handler: handler, DefaultReadTimeout: 10 /*second*/}}

	qserver := qrpc.NewServer(bindings)
	qserver.ListenAndServe()

}

func startHTTP(playCmd *cmd.PlayCmd) {
	srv := &http.Server{Addr: "0.0.0.0:8080"}
	http.HandleFunc("/watch", func(w http.ResponseWriter, r *http.Request) {

		buf := &bytes.Buffer{}
		err := playCmd.ReadLatestVideoFrame("xu", "mjpeg", buf)
		if err != nil {
			w.Write([]byte(err.Error()))
			return
		}

		w.Header().Add("Content-Type", "image/jpeg")
		w.Header().Add("Content-Length", strconv.Itoa(len(buf.Bytes())))
		w.Write(buf.Bytes())
	})
	fmt.Println(srv.ListenAndServe())
}
