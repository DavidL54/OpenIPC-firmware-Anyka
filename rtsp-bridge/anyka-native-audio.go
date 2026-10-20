package main

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"runtime/debug"
	"time"

	"github.com/gorilla/websocket"
)

const nativeAudioSocket = "/conf/anyka-private/native-audio.sock"

// Only this authenticated gateway sends PCM to the SDK. Per-frame credit
// keeps at most one bounded frame in flight; cancellation closes the socket.
func openNativeSpeaker(ctx context.Context, volume int) (net.Conn, error) {
	return openNativeSpeakerOptions(ctx, volume, 8000, false, false, 8000, 1)
}

func openNativeSpeakerOptions(ctx context.Context, volume, rate int, nr, mp3 bool, sourceRate, sourceChannels int) (net.Conn, error) {
	connection, err := (&net.Dialer{Timeout: time.Second}).DialContext(ctx, "unix", nativeAudioSocket)
	if err != nil {
		return nil, err
	}
	var header [24]byte
	copy(header[:], "AO02")
	length := 16
	if mp3 {
		copy(header[:], "MP02"); length = 24
		binary.BigEndian.PutUint32(header[16:], uint32(sourceRate)); binary.BigEndian.PutUint32(header[20:], uint32(sourceChannels))
	}
	binary.BigEndian.PutUint32(header[4:], uint32(int32(volume)))
	binary.BigEndian.PutUint32(header[8:], uint32(rate))
	if nr { binary.BigEndian.PutUint32(header[12:], 1) }
	stop := context.AfterFunc(ctx, func() { connection.Close() })
	defer stop()
	connection.SetDeadline(time.Now().Add(3 * time.Second))
	if _, err = connection.Write(header[:length]); err == nil {
		err = nativeSpeakerAck(connection)
	}
	if err != nil {
		connection.Close()
		return nil, err
	}
	return connection, nil
}

func nativeSpeakerAck(connection net.Conn) error {
	var status [1]byte
	if _, err := io.ReadFull(connection, status[:]); err != nil {
		return err
	}
	if status[0] != 0 {
		return fmt.Errorf("native speaker refused frame")
	}
	return nil
}

func nativeSpeakerFrame(connection net.Conn, pcm []byte) error {
    if len(pcm)%2 != 0 { return fmt.Errorf("invalid native PCM frame") }
	var frame [1282]byte
	return nativeSpeakerWrite(connection, &frame, pcm)
}

func nativeSpeakerWrite(connection net.Conn, frame *[1282]byte, pcm []byte) error {
	return nativeSpeakerWriteTimeout(connection, frame, pcm, time.Second)
}

func nativeSpeakerWriteTimeout(connection net.Conn, frame *[1282]byte, pcm []byte, deadline time.Duration) error {
	if len(pcm) > 1280 {
		return fmt.Errorf("invalid native PCM frame")
	}
	binary.BigEndian.PutUint16(frame[:2], uint16(len(pcm)))
	copy(frame[2:], pcm)
	if len(pcm) == 0 { deadline = 4*time.Second }
	connection.SetDeadline(time.Now().Add(deadline))
	if _, err := connection.Write(frame[:2+len(pcm)]); err != nil {
		return err
	}
	// Reuse the frame buffer for the credit byte too.
	if _, err := io.ReadFull(connection, frame[:1]); err != nil {
		return err
	}
	if frame[0] != 0 {
		return fmt.Errorf("native speaker refused frame")
	}
	return nil
}

func (gateway *webGateway) nativeAudioContext(parent context.Context, duration time.Duration) (context.Context, func()) {
	ctx, cancel := context.WithTimeout(parent, duration)
	gateway.mu.Lock()
	gateway.audioCancel = cancel
	private := gateway.privacy
	gateway.mu.Unlock()
	if private { cancel() }
	return ctx, func() {
		cancel()
		gateway.mu.Lock()
		gateway.audioCancel = nil
		gateway.mu.Unlock()
		debug.FreeOSMemory()
	}
}

func (gateway *webGateway) nativeAudioLive(request *http.Request, browser *websocket.Conn, volume, rate int, nr, mp3 bool, sourceRate, sourceChannels int, report func(string, string)) {
	cookie, err := request.Cookie("anyka_session")
	if err != nil {
		report("error", "Session expired")
		return
	}
	if tcp, ok := browser.NetConn().(*net.TCPConn); ok {
		_ = tcp.SetReadBuffer(2048)
	}
	ctx, release := gateway.nativeAudioContext(request.Context(), 5*time.Minute)
	defer release()
	connection, err := openNativeSpeakerOptions(ctx, volume, rate, nr, mp3, sourceRate, sourceChannels)
	if err != nil {
		report("error", "Native speaker unavailable")
		return
	}
	defer connection.Close()
	stop := context.AfterFunc(ctx, func() { connection.Close(); browser.Close() })
	defer stop()
	report("ready", fmt.Sprintf("PCM mono %d Hz signed 16-bit little-endian (native SDK)", rate))
	started := time.Now()
	var received int64
	var pcm [1281]byte
	var packet [1282]byte
	for {
		browser.SetReadDeadline(time.Now().Add(3 * time.Second))
		kind, reader, err := browser.NextReader()
		if err != nil {
			return
		}
		count, err := io.ReadFull(reader, pcm[:])
		if err != nil && err != io.EOF && err != io.ErrUnexpectedEOF {
			return
		}
		if kind == websocket.TextMessage && string(pcm[:count]) == "stop" {
			return
		}
		gateway.mu.Lock()
		expires, found := gateway.sessions[cookie.Value]
		authorized := found && time.Now().Before(expires)
		gateway.mu.Unlock()
		if !authorized {
			report("error", "Session expired")
			return
		}
		if kind == websocket.TextMessage && string(pcm[:count]) == "end" {
			if nativeSpeakerWrite(connection, &packet, nil) != nil { report("error", "Speaker drain failed") } else { report("ended", "Playback completed") }
			return
		}
		if kind != websocket.BinaryMessage || count == 0 || count > 1280 || (!mp3 && count%2 != 0) {
			report("error", "Invalid PCM packet")
			return
		}
		received += int64(count)
		if (mp3 && received > 20*1024*1024) || (!mp3 && received > int64(time.Since(started))*int64(rate*2)/int64(time.Second)+2560) {
			report("error", "Audio arrives faster than real time")
			return
		}
		timeout := time.Second
		if mp3 { timeout = 3*time.Second }
		if err = nativeSpeakerWriteTimeout(connection, &packet, pcm[:count], timeout); err != nil {
			report("error", "Native speaker stopped or stalled")
			return
		}
		if mp3 { report("credit", "Next MP3 chunk") }
	}
}

func (gateway *webGateway) nativeAudioFile(writer http.ResponseWriter, request *http.Request, filename string, volume int) {
	ctx, release := gateway.nativeAudioContext(request.Context(), 40*time.Second)
	defer release()
	connection, err := openNativeSpeaker(ctx, volume)
	if err != nil {
		http.Error(writer, "Native speaker unavailable", 503)
		return
	}
	defer connection.Close()
	stop := context.AfterFunc(ctx, func() { connection.Close() })
	defer stop()
	file, err := os.Open(filename)
	if err != nil {
		http.Error(writer, "Cannot open PCM", 503)
		return
	}
	defer file.Close()
	var pcm [640]byte
	var packet [1282]byte
	started := time.Now()
	var sent int64
	for {
		count, readErr := file.Read(pcm[:])
		if count > 0 {
			if err = nativeSpeakerWrite(connection, &packet, pcm[:count]); err != nil {
				break
			}
			sent += int64(count)
			wait := time.Until(started.Add(time.Duration(sent) * time.Second / 16000))
			if wait > 0 {
				timer := time.NewTimer(wait)
				select {
				case <-ctx.Done():
					timer.Stop()
					err = ctx.Err()
				case <-timer.C:
				}
				if err != nil {
					break
				}
			}
		}
		if readErr == io.EOF {
			err = nativeSpeakerWrite(connection, &packet, nil)
			break
		}
		if readErr != nil {
			err = readErr
			break
		}
	}
	if ctx.Err() != nil {
		http.Error(writer, "Playback stopped or timed out", 408)
		return
	}
	if err != nil {
		http.Error(writer, "Native speaker playback failed", 503)
		return
	}
	writer.WriteHeader(http.StatusNoContent)
}
