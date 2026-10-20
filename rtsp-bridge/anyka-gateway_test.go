package main

import (
	"context"
	"net"
	"net/http"
	"net/http/httptest"
	"net/http/httputil"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// Exercise the public router, including its authentication boundary: hiding
// markup must not accidentally bypass HA control authentication or Origin.
func TestHeadlessGateway(t *testing.T) {
	backendCalls := 0
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		backendCalls++
		if r.Header.Get("X-Anyka-Logged-In") != "true" {
			t.Error("Unauthenticated backend context")
		}
		w.WriteHeader(http.StatusNoContent)
	}))
	defer backend.Close()
	backendURL, _ := url.Parse(backend.URL)
	gateway := &webGateway{
		uiDisabled: true, access: webAccess{PasswordHash: "configured"},
		sessions: map[string]time.Time{"test-session": time.Now().Add(time.Hour)},
		web: httputil.NewSingleHostReverseProxy(backendURL),
	}
	for _, item := range []struct {
		path, method string
		session, origin bool
		want int
	}{
		{"/", "GET", false, false, 404},
		{"/cgi-bin/access.cgi", "GET", false, false, 404},
		{"/cgi-bin/dashboard.cgi", "GET", true, false, 404},
		{"/cgi-bin/live.cgi", "GET", true, false, 404},
		{"/a/style.css", "GET", false, false, 404},
		{"/api/ws?src=preview", "GET", true, true, 404},
		{"/cgi-bin/isp.cgi", "GET", false, false, 401},
		{"/cgi-bin/isp.cgi?brightness=0", "POST", false, true, 401},
		{"/cgi-bin/isp.cgi?brightness=0", "POST", true, false, 403},
		{"/cgi-bin/isp.cgi?brightness=0", "POST", true, true, 204},
		{"/cgi-bin/gpio.cgi?action=status", "GET", true, false, 204},
		{"/cgi-bin/ptz.cgi?action=stop", "POST", true, true, 204},
		{"/api/audio/live", "GET", false, false, 401},
		{"/image.jpg", "GET", false, false, 401},
	} {
		t.Run(item.method+item.path, func(t *testing.T) {
			r := httptest.NewRequest(item.method, "http://192.0.2.10"+item.path, nil)
			if item.session { r.AddCookie(&http.Cookie{Name:"anyka_session", Value:"test-session"}) }
			if item.origin { r.Header.Set("Origin", "http://192.0.2.10") }
			w := httptest.NewRecorder()
			gateway.ServeHTTP(w, r)
			if w.Code != item.want { t.Fatalf("status %d, want %d", w.Code, item.want) }
			if item.path == "/image.jpg" && !strings.HasPrefix(w.Header().Get("WWW-Authenticate"), "Digest ") {
				t.Fatal("JPEG Digest challenge lost")
			}
		})
	}
	if backendCalls != 3 { t.Fatalf("Unexpected backend calls: %d", backendCalls) }
}

func TestPrivacyRevokesMediaAndResets(t *testing.T) {
	t.Setenv("ANYKA_NATIVE_RTSP", "0")
	if _, err := os.Stat(privacyPath); err == nil { t.Skip("Privacy marker already present") }
	t.Cleanup(func() { _ = os.Remove(privacyPath) })
	peer, remote := net.Pipe()
	defer remote.Close()
	preview, cancelPreview := context.WithCancel(context.Background())
	defer cancelPreview()
	audioStopped := false
	gateway := &webGateway{
		uiDisabled: true, access: webAccess{PasswordHash: "configured"},
		sessions: map[string]time.Time{"test-session": time.Now().Add(time.Hour)},
		mediaPeers: map[net.Conn]struct{}{peer: {}},
		previewCancel: map[uint64]context.CancelFunc{1: cancelPreview},
		audioCancel: func() { audioStopped = true },
	}
	call := func(method, body string, authenticated, origin bool) *httptest.ResponseRecorder {
		t.Helper()
		r := httptest.NewRequest(method, "http://192.0.2.10/api/privacy", strings.NewReader(body))
		if authenticated { r.AddCookie(&http.Cookie{Name: "anyka_session", Value: "test-session"}) }
		if origin { r.Header.Set("Origin", "http://192.0.2.10") }
		w := httptest.NewRecorder()
		gateway.ServeHTTP(w, r)
		return w
	}
	if w := call("POST", `{"enabled":true}`, false, true); w.Code != 401 { t.Fatalf("anonymous status %d", w.Code) }
	if w := call("POST", `{"enabled":true}`, true, false); w.Code != 403 { t.Fatalf("cross-origin status %d", w.Code) }
	if w := call("POST", `{"enabled":"true"}`, true, true); w.Code != 400 { t.Fatalf("malformed status %d", w.Code) }
	if w := call("POST", `{"enabled":true}`, true, true); w.Code != 200 { t.Fatalf("enable status %d: %s", w.Code, w.Body) }
	if _, err := os.Stat(privacyPath); err != nil { t.Fatalf("missing RAM marker: %v", err) }
	select { case <-preview.Done(): default: t.Fatal("preview was not cancelled") }
	if !audioStopped { t.Fatal("speaker was not cancelled") }
	_ = remote.SetReadDeadline(time.Now().Add(time.Second))
	var byteBuffer [1]byte
	if _, err := remote.Read(byteBuffer[:]); err == nil { t.Fatal("RTSP peer was not closed") }
	if w := call("GET", "", true, false); w.Code != 200 || !strings.Contains(w.Body.String(), `"enabled":true`) { t.Fatalf("status: %d %s", w.Code, w.Body) }
	r := httptest.NewRequest("GET", "http://192.0.2.10/image.jpg", nil)
	r.AddCookie(&http.Cookie{Name: "anyka_session", Value: "test-session"})
	w := httptest.NewRecorder(); gateway.ServeHTTP(w, r)
	if w.Code != 503 { t.Fatalf("private snapshot status %d", w.Code) }
	if w := call("POST", `{"enabled":false}`, true, true); w.Code != 200 { t.Fatalf("disable status %d", w.Code) }
	if _, err := os.Stat(privacyPath); !os.IsNotExist(err) { t.Fatalf("RAM marker remains: %v", err) }
}

func TestPrivacySlateKeepsNativeMediaPeer(t *testing.T) {
	t.Setenv("ANYKA_NATIVE_RTSP", "1")
	if _, err := os.Stat(privacyPath); err == nil { t.Skip("Privacy marker already present") }
	t.Cleanup(func() { _ = os.Remove(privacyPath) })
	assets := t.TempDir()
	for _, name := range []string{"privacy-slate-vs0.h264", "privacy-slate-vs1.h264", "privacy-slate.jpg"} {
		if err := os.WriteFile(filepath.Join(assets, name), []byte("slate-image"), 0600); err != nil { t.Fatal(err) }
	}
	peer, remote := net.Pipe()
	defer peer.Close(); defer remote.Close()
	audioStopped := false
	gateway := &webGateway{
		uiDisabled: true, access: webAccess{PasswordHash: "configured"}, privacyAssets: assets,
		sessions: map[string]time.Time{"test-session": time.Now().Add(time.Hour)},
		mediaPeers: map[net.Conn]struct{}{peer: {}},
		audioCancel: func() { audioStopped = true },
	}
	call := func(path, method, body string) *httptest.ResponseRecorder {
		t.Helper()
		r := httptest.NewRequest(method, "http://192.0.2.10"+path, strings.NewReader(body))
		r.AddCookie(&http.Cookie{Name:"anyka_session", Value:"test-session"})
		r.Header.Set("Origin", "http://192.0.2.10")
		w := httptest.NewRecorder()
		gateway.ServeHTTP(w, r)
		return w
	}
	if w := call("/api/privacy", "POST", `{"enabled":true}`); w.Code != 200 { t.Fatalf("enable: %d %s", w.Code, w.Body) }
	if !audioStopped { t.Fatal("speaker was not stopped") }
	if _, err := os.Stat(privacyPath); err != nil { t.Fatalf("missing privacy marker: %v", err) }
	_ = remote.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
	var byteBuffer [1]byte
	if _, err := remote.Read(byteBuffer[:]); err == nil {
		t.Fatal("unexpected native peer data")
	} else if netError, ok := err.(net.Error); !ok || !netError.Timeout() {
		t.Fatalf("native media peer was closed: %v", err)
	}
	if w := call("/image.jpg", "GET", ""); w.Code != 200 || w.Body.String() != "slate-image" {
		t.Fatalf("private JPEG: %d %q", w.Code, w.Body.String())
	}
	if w := call("/api/audio/live", "GET", ""); w.Code != 503 { t.Fatalf("private audio: %d", w.Code) }
	if w := call("/api/privacy", "POST", `{"enabled":false}`); w.Code != 200 { t.Fatalf("disable: %d", w.Code) }
	if _, err := os.Stat(privacyPath); !os.IsNotExist(err) { t.Fatalf("privacy marker remains: %v", err) }
}
