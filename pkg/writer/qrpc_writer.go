package writer

import (
	"github.com/zhiqiangxu/qrpc"
)

// QrpcWriter wraps qrpc.FrameWriter into io.Writer
type QrpcWriter struct {
	writer  qrpc.FrameWriter
	frame   *qrpc.RequestFrame
	respCmd qrpc.Cmd
}

// NewQrpcWriter creates a QrpcWriter
func NewQrpcWriter(writer qrpc.FrameWriter, frame *qrpc.RequestFrame, respCmd qrpc.Cmd) *QrpcWriter {
	return &QrpcWriter{writer: writer, frame: frame, respCmd: respCmd}
}

// Write implements io.Writer
func (w *QrpcWriter) Write(bytes []byte) (int, error) {
	w.writer.StartWrite(w.frame.RequestID, w.respCmd, qrpc.StreamFlag)
	w.writer.WriteBytes(bytes)
	err := w.writer.EndWrite()
	if err != nil {
		return 0, err
	}
	return len(bytes), nil
}
