package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"net"
	"net/textproto"
	"os"
	"runtime/debug"
	"strings"
	"time"

	"github.com/AlexxIT/go2rtc/pkg/tcp"
)

// The SDK receives only allow/deny decisions. Neither credentials nor media
// cross this socket. A password change closes it and the native media peer.
const nativeAuthSocket = "/conf/anyka-private/native-rtsp-auth.sock"

func nativeRequest(data []byte) (*tcp.Request, error) {
	if len(data) == 0 || len(data) > 8192 || !bytes.HasSuffix(data, []byte("\r\n\r\n")) {
		return nil, fmt.Errorf("invalid native request")
	}
	reader := textproto.NewReader(bufio.NewReader(bytes.NewReader(data)))
	line, err := reader.ReadLine()
	if err != nil || !strings.HasSuffix(line, " RTSP/1.0") {
		return nil, fmt.Errorf("invalid native protocol")
	}
	request, err := tcp.ReadRequest(bufio.NewReader(bytes.NewReader(data)))
	if err != nil || request.URL.Scheme != "rtsp" || request.Header.Get("Transfer-Encoding") != "" ||
		len(request.Header.Values("CSeq")) != 1 || len(request.Header.Get("CSeq")) > 10 ||
		len(request.Header.Values("Authorization")) > 1 || len(request.Header.Values("Content-Length")) > 1 {
		return nil, fmt.Errorf("invalid native headers")
	}
	for _, digit := range request.Header.Get("CSeq") {
		if digit < '0' || digit > '9' {
			return nil, fmt.Errorf("invalid native sequence")
		}
	}
	if request.Header.Get("CSeq") == "" {
		return nil, fmt.Errorf("missing native sequence")
	}
	return request, nil
}

func (gateway *webGateway) nativeAuth(connection net.Conn) {
	defer func() {
		connection.Close()
		gateway.mu.Lock()
		_, authenticated := gateway.mediaPeers[connection]
		delete(gateway.mediaPeers, connection)
		last := authenticated && len(gateway.mediaPeers) == 0
		gateway.mu.Unlock()
		if last {
			debug.FreeOSMemory()
		}
	}()
	var prefix [4]byte
	for {
		// Idle streaming does not create any auth traffic or periodic GC.
		if _, err := io.ReadFull(connection, prefix[:]); err != nil {
			return
		}
		size := binary.BigEndian.Uint32(prefix[:])
		if size == 0 || size > 8192 {
			return
		}
		connection.SetDeadline(time.Now().Add(5 * time.Second))
		data := make([]byte, size)
		if _, err := io.ReadFull(connection, data); err != nil {
			return
		}
		request, err := nativeRequest(data)
		if err != nil {
			return
		}
		response := gateway.rtspAccess(request, connection)
		var reply []byte
		if response != nil {
			var buffer bytes.Buffer
			fmt.Fprintf(&buffer, "RTSP/1.0 %s\r\nCSeq: %s\r\n", response.Status, request.Header.Get("CSeq"))
			for key, values := range response.Header {
				for _, value := range values {
					fmt.Fprintf(&buffer, "%s: %s\r\n", key, value)
				}
			}
			buffer.WriteString("Content-Length: 0\r\n\r\n")
			reply = buffer.Bytes()
		}
		binary.BigEndian.PutUint32(prefix[:], uint32(len(reply)))
		if _, err = connection.Write(prefix[:]); err != nil {
			return
		}
		if len(reply) != 0 {
			if _, err = connection.Write(reply); err != nil {
				return
			}
		}
		connection.SetDeadline(time.Time{})
	}
}

func (gateway *webGateway) startNativeAuth() {
	if err := os.Remove(nativeAuthSocket); err != nil && !os.IsNotExist(err) {
		log.Fatal("Cannot remove obsolete native auth socket")
	}
	listener, err := net.Listen("unix", nativeAuthSocket)
	if err != nil || os.Chmod(nativeAuthSocket, 0600) != nil {
		log.Fatal("Cannot start private native RTSP authentication")
	}
	go func() {
		slots := make(chan struct{}, 2)
		for {
			connection, err := listener.Accept()
			if err != nil {
				return
			}
			select {
			case slots <- struct{}{}:
			default:
				connection.Close()
				continue
			}
			go func() {
				defer func() { <-slots }()
				gateway.nativeAuth(connection)
			}()
		}
	}()
	log.Print("[anyka-web] native SDK RTSP authentication ready")
}
