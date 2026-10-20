package main

import (
	"encoding/binary"
	"io"
	"net"
	"testing"
	"time"
)

func TestNativeSpeakerCreditAndBounds(t *testing.T) {
	for _, size := range []int{1, 1281, 1282} {
		if nativeSpeakerFrame(nil, make([]byte, size)) == nil {
			t.Fatal("invalid PCM accepted", size)
		}
	}
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()
	pcm := make([]byte, 640)
	pcm[0] = 123
	finished := make(chan error, 1)
	go func() { finished <- nativeSpeakerFrame(client, pcm) }()
	var prefix [2]byte
	if _, err := io.ReadFull(server, prefix[:]); err != nil {
		t.Fatal(err)
	}
	if binary.BigEndian.Uint16(prefix[:]) != 640 {
		t.Fatal("unbounded or incorrect length")
	}
	data := make([]byte, 640)
	if _, err := io.ReadFull(server, data); err != nil {
		t.Fatal(err)
	}
	if data[0] != 123 {
		t.Fatal("PCM changed")
	}
	select {
	case <-finished:
		t.Fatal("frame returned before SDK credit")
	case <-time.After(10 * time.Millisecond):
	}
	server.Write([]byte{0})
	if err := <-finished; err != nil {
		t.Fatal(err)
	}
}

func TestNativeSpeakerCancellation(t *testing.T) {
	client, server := net.Pipe()
	defer server.Close()
	finished := make(chan error, 1)
	go func() { finished <- nativeSpeakerFrame(client, make([]byte, 640)) }()
	client.Close()
	select {
	case err := <-finished:
		if err == nil {
			t.Fatal("closed speaker acknowledged")
		}
	case <-time.After(time.Second):
		t.Fatal("cancelled frame blocked")
	}
}

// Compressed chunks may have an odd byte count. EOF must still wait for the
// SDK drain acknowledgement; a failed drain must never report completion.
func TestNativeSpeakerCompressedAndDrain(t *testing.T) {
	for _, payload := range [][]byte{{1, 2, 3}, nil} {
		for _, status := range []byte{0, 1} {
			client, server := net.Pipe()
			finished := make(chan error, 1)
			go func() {
				var packet [1282]byte
				finished <- nativeSpeakerWrite(client, &packet, payload)
			}()
			wire := make([]byte, 2+len(payload))
			if _, err := io.ReadFull(server, wire); err != nil { t.Fatal(err) }
			if int(binary.BigEndian.Uint16(wire[:2])) != len(payload) { t.Fatal("wrong chunk or EOF size") }
			select {
			case <-finished: t.Fatal("completed without SDK acknowledgement")
			case <-time.After(10*time.Millisecond):
			}
			server.Write([]byte{status})
			if err := <-finished; (err != nil) != (status != 0) { t.Fatal("failed SDK acknowledgement accepted", err) }
			client.Close(); server.Close()
		}
	}
}

// A sustained microphone session must not allocate a new PCM packet on each
// write: that previously caused periodic GC pressure on the 26 MiB kernel.
type nativeAudioSink struct{}

func (*nativeAudioSink) Read(data []byte) (int, error)    { data[0] = 0; return 1, nil }
func (*nativeAudioSink) Write(data []byte) (int, error)   { return len(data), nil }
func (*nativeAudioSink) Close() error                     { return nil }
func (*nativeAudioSink) LocalAddr() net.Addr              { return nil }
func (*nativeAudioSink) RemoteAddr() net.Addr             { return nil }
func (*nativeAudioSink) SetDeadline(time.Time) error      { return nil }
func (*nativeAudioSink) SetReadDeadline(time.Time) error  { return nil }
func (*nativeAudioSink) SetWriteDeadline(time.Time) error { return nil }
func TestNativeSpeakerReusesPCMBuffer(t *testing.T) {
	sink := &nativeAudioSink{}
	var packet [1282]byte
	var pcm [640]byte
	allocations := testing.AllocsPerRun(100, func() {
		if err := nativeSpeakerWrite(sink, &packet, pcm[:]); err != nil {
			t.Fatal(err)
		}
	})
	if allocations != 0 {
		t.Fatalf("per-frame allocations: %v", allocations)
	}
}
