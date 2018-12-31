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
	writer, err := w.c.NextWriter(websocket.BinaryMessage)
	if err != nil {
		fmt.Println("NextWriter", err)
		return 0, err
	}
	_, err = writer.Write(bytes)
	if err != nil {
		fmt.Println("writer.Write", err)
		return 0, err
	}
	err = writer.Close()
	if err != nil {
		fmt.Println("writer.Close", err)
		return 0, err
	}

	return len(bytes), nil
}
