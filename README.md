# avflow

real time publish and play!

## usage


```bash
third_party/ffmpeg/build.sh  

PKG_CONFIG_PATH=third_party/ffmpeg/build/lib/pkgconfig/
CGO_LDFLAGS_ALLOW=".*"

go run server.go


third_party/ffmpeg/build/bin/ffmpeg -f avfoundation -framerate 30 -i "0"     -pix_fmt yuvj420p -ac 2 -codec:v mpeg1video -maxrate 2000k -bufsize 2000k -f mpegts "qrpc://localhost:8888?id=publisher&pass=abc&mode=publish"

open http://localhost:8080/static/player.html
```

