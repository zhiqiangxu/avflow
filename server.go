package main

import (
	"bytes"
	"fmt"
	"net/http"
	_ "net/http/pprof"
	"os"
	"strconv"

	"github.com/gorilla/websocket"
	"github.com/zhiqiangxu/avflow/cmd"
	"github.com/zhiqiangxu/avflow/pkg/writer"
	"github.com/zhiqiangxu/qrpc"
)

func main() {
	handler := qrpc.NewServeMux()
	playCmd := cmd.NewPlayCmd()

	go startHTTP(playCmd)

	handler.Handle(cmd.Auth, cmd.NewAuthCmd())
	handler.Handle(cmd.Play, playCmd)

	bindings := []qrpc.ServerBinding{
		qrpc.ServerBinding{Addr: "0.0.0.0:8888", Handler: handler, DefaultReadTimeout: 10 /*second*/}}

	qserver := qrpc.NewServer(bindings)
	qserver.ListenAndServe()

}

func startHTTP(playCmd *cmd.PlayCmd) {
	srv := &http.Server{Addr: "0.0.0.0:8080"}
	http.HandleFunc("/watch", func(w http.ResponseWriter, r *http.Request) {

		who := r.URL.Query().Get("who")
		if who == "" {
			w.Write([]byte("please specify who"))
			return
		}
		buf := &bytes.Buffer{}
		err := playCmd.ReadLatestVideoFrame(who, "mjpeg", buf)
		if err != nil {
			w.Write([]byte(err.Error()))
			return
		}

		w.Header().Add("Content-Type", "image/jpeg")
		w.Header().Add("Content-Length", strconv.Itoa(len(buf.Bytes())))
		w.Write(buf.Bytes())
	})

	http.HandleFunc("/static/", func(w http.ResponseWriter, r *http.Request) {
		wd, _ := os.Getwd()
		http.ServeFile(w, r, wd+r.URL.Path)
	})
	var upgrader = websocket.Upgrader{
		CheckOrigin: func(r *http.Request) bool {
			return true
		}} // use default options

	http.HandleFunc("/watch_mpegts", func(w http.ResponseWriter, r *http.Request) {

		who := r.URL.Query().Get("who")
		if who == "" {
			w.Write([]byte("please specify who"))
			// return
		}

		c, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			fmt.Println("upgrade:", err)
			return
		}

		c.WriteMessage(websocket.TextMessage, []byte("hello world"))
		select {}
		err = playCmd.SubcribeAVFrame(who, "mpegts", writer.NewWSWriter(c))
		if err != nil {
			fmt.Println("SubcribeAVFrame", err)
			c.Close()
			return
		}

		select {
		case <-r.Context().Done():
			playCmd.UnsubcribeAVFrame(who, w)
		}
	})
	fmt.Println(srv.ListenAndServe())
}
