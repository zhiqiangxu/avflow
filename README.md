# avflow

real time publish and play!

## usage


```bash

# 编译ffmpeg
third_party/ffmpeg/build.sh  

# 设置一些环境变量
PKG_CONFIG_PATH=third_party/ffmpeg/build/lib/pkgconfig/
CGO_LDFLAGS_ALLOW=".*"

# 开启avflow server
go run server.go

# 另开一个终端，开始publish
third_party/ffmpeg/build/bin/ffmpeg -f avfoundation -framerate 30 -i "0"     -pix_fmt yuvj420p -ac 2 -codec:v mpeg1video -maxrate 2000k -bufsize 2000k -f mpegts "qrpc://localhost:8888?id=publisher&pass=abc&mode=publish"

# 访问
open http://localhost:8080/static/player.html
```

