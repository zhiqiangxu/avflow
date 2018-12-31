package writer

import (
	"fmt"

	"github.com/gorilla/websocket"
)

// WSWriter wraps *websocket.Conn into io.Writer
type WSWriter struct {
	c *websocket.Conn
}

// NewWSWriter creates a WSWriter
func NewWSWriter(c *websocket.Conn) *WSWriter {
	return &WSWriter{c: c}
}

// Write implements io.Writer
func (w *WSWriter) Write(bytes []byte) (int, error) {
	fmt.Println("WSWriter called", len(bytes))
	err := w.c.WriteMessage(websocket.BinaryMessage, bytes)

	return len(bytes), err
}
