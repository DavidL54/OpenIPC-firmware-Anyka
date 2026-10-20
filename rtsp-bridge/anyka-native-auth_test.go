package main

import (
	"crypto/md5"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"strings"
	"testing"
	"time"

	mediaAuth "github.com/abbot/go-http-auth"
)

func TestNativeRequestBounds(t *testing.T) {
	valid := "OPTIONS rtsp://camera/vs0 RTSP/1.0\r\nCSeq: 1\r\n\r\n"
	if _, err := nativeRequest([]byte(valid)); err != nil { t.Fatal(err) }
	for _, invalid := range []string{
		strings.Repeat("x", 8193), strings.Replace(valid, "RTSP/1.0", "HTTP/1.1", 1),
		strings.Replace(valid, "CSeq: 1", "CSeq: 1\r\nCSeq: 2", 1),
		strings.Replace(valid, "CSeq: 1", "CSeq: 1\r\nContent-Length: 10", 1),
		strings.Replace(valid, "CSeq: 1", "CSeq: 1\r\nTransfer-Encoding: chunked", 1),
		strings.Replace(valid, "CSeq: 1", "CSeq: 1\r\nAuthorization: a\r\nAuthorization: b", 1),
	} {
		if _, err := nativeRequest([]byte(invalid)); err == nil { t.Fatal("Invalid request accepted") }
	}
}

func TestNativeDigestAndRevocation(t *testing.T) {
	ha1 := fmt.Sprintf("%x", md5.Sum([]byte("admin:"+mediaRealm+":test-password")))
	gateway := &webGateway{access: webAccess{PasswordHash:"test", DigestHA1:ha1}}
	client, server := net.Pipe()
	defer client.Close()
	client.SetDeadline(time.Now().Add(3*time.Second))
	go gateway.nativeAuth(server)
	roundtrip := func(request string) string {
		t.Helper()
		var prefix [4]byte
		binary.BigEndian.PutUint32(prefix[:], uint32(len(request)))
		if _, err := client.Write(prefix[:]); err != nil { t.Fatal(err) }
		if _, err := client.Write([]byte(request)); err != nil { t.Fatal(err) }
		if _, err := io.ReadFull(client, prefix[:]); err != nil { t.Fatal(err) }
		reply := make([]byte, binary.BigEndian.Uint32(prefix[:]))
		if _, err := io.ReadFull(client, reply); err != nil { t.Fatal(err) }
		return string(reply)
	}
	uri := "rtsp://camera/vs0"
	response := roundtrip("OPTIONS "+uri+" RTSP/1.0\r\nCSeq: 1\r\n\r\n")
	if !strings.HasPrefix(response,"RTSP/1.0 401") || !strings.Contains(response,"qop=\"auth\"") { t.Fatal(response) }
	var challenge string
	for _, line := range strings.Split(response,"\r\n") {
		if strings.HasPrefix(strings.ToLower(line),"www-authenticate:") { challenge = strings.TrimSpace(strings.SplitN(line,":",2)[1]) }
	}
	params := mediaAuth.DigestAuthParams(challenge)
	if params == nil { t.Fatal("Missing challenge") }
	ha2 := fmt.Sprintf("%x", md5.Sum([]byte("OPTIONS:"+uri)))
	digest := fmt.Sprintf("%x", md5.Sum([]byte(ha1+":"+params["nonce"]+":00000001:test:auth:"+ha2)))
	request := fmt.Sprintf("OPTIONS %s RTSP/1.0\r\nCSeq: 2\r\nAuthorization: Digest username=\"admin\", realm=\"%s\", nonce=\"%s\", opaque=\"%s\", algorithm=MD5, uri=\"%s\", qop=auth, nc=00000001, cnonce=\"test\", response=\"%s\"\r\n\r\n",uri,mediaRealm,params["nonce"],params["opaque"],uri,digest)
	if reply := roundtrip(request); reply != "" { t.Fatal(reply) }
	if reply := roundtrip(request); !strings.HasPrefix(reply,"RTSP/1.0 401") { t.Fatal("Replay accepted",reply) }
	gateway.mu.Lock()
	if len(gateway.mediaPeers) != 1 { t.Fatal("Peer not registered") }
	for peer := range gateway.mediaPeers { peer.Close() }
	gateway.mu.Unlock()
	var prefix [4]byte
	if _, err := client.Read(prefix[:]); err == nil { t.Fatal("Revoked auth peer remained open") }
}
