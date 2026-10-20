package main

import (
	"bytes"
	"io"
	"net/http"
	"strings"
	"time"
)

// Retain the authenticated camera API when the OpenIPC pages and browser
// media bridge are unused. Existing handlers still enforce verbs and Origin.
func headlessRoute(path, method string) bool {
	if path == "/cgi-bin/access.cgi" {
		return method == "POST"
	}
	switch path {
	case "/logout", "/cgi-bin/metrics.cgi", "/cgi-bin/isp.cgi", "/cgi-bin/gpio.cgi", "/cgi-bin/ptz.cgi", "/cgi-bin/snapshot.cgi", "/image.jpg",
		"/api/audio/live", "/api/audio/play", "/api/audio/stop", "/api/privacy", "/api/time", "/api/ntfy":
		return true
	}
	return false
}

// Only page markup is cached (64 KiB total), never assets, snapshots,
// credentials or settings. Live settings still come from authenticated APIs.
func preloadPages(backend string) map[string][]byte {
	pages := make(map[string][]byte)
	client := &http.Client{Timeout: 3 * time.Second}
	defer client.CloseIdleConnections()
	total := 0
	for _, key := range []string{"/cgi-bin/dashboard.cgi", "/cgi-bin/live.cgi", "/cgi-bin/camera.cgi", "/cgi-bin/camera.cgi?tab=image", "/cgi-bin/camera.cgi?tab=lights", "/cgi-bin/stream-urls.cgi"} {
		request, err := http.NewRequest("GET", strings.TrimRight(backend, "/")+key, nil)
		if err != nil {
			continue
		}
		request.Header.Set("X-Anyka-Configured", "true")
		request.Header.Set("X-Anyka-Logged-In", "true")
		request.Header.Set("X-Anyka-Address", "__ANYKA_ADDRESS__")
		request.Header.Set("X-Anyka-Gateway", "__ANYKA_GATEWAY__")
		request.Header.Set("X-Anyka-RTSP-State", "__ANYKA_RTSP_STATE__")
		response, err := client.Do(request)
		if err != nil {
			continue
		}
		body, err := io.ReadAll(io.LimitReader(response.Body, 32769))
		response.Body.Close()
		if err != nil || response.StatusCode != 200 || !strings.HasPrefix(response.Header.Get("Content-Type"), "text/html") || len(body) > 32768 || total+len(body) > 65536 {
			continue
		}
		pages[key] = body
		total += len(body)
	}
	return pages
}

func pageKey(request *http.Request) string {
	key := request.URL.Path
	if key == "/cgi-bin/camera.cgi" {
		query := request.URL.Query()
		if len(query) > 1 || (len(query) == 1 && len(query["tab"]) != 1) {
			return ""
		}
		switch query.Get("tab") {
		case "image", "lights":
			return key + "?tab=" + query.Get("tab")
		}
		return key
	}
	if request.URL.RawQuery != "" {
		return ""
	}
	return key
}

func cachedPage(writer http.ResponseWriter, request *http.Request, pages map[string][]byte) bool {
	if request.Method != "GET" && request.Method != "HEAD" {
		return false
	}
	body, ok := pages[pageKey(request)]
	if !ok {
		return false
	}
	body = bytes.ReplaceAll(body, []byte("__ANYKA_ADDRESS__"), []byte(request.Header.Get("X-Anyka-Address")))
	body = bytes.ReplaceAll(body, []byte("__ANYKA_GATEWAY__"), []byte(request.Header.Get("X-Anyka-Gateway")))
	body = bytes.ReplaceAll(body, []byte("__ANYKA_RTSP_STATE__"), []byte(request.Header.Get("X-Anyka-RTSP-State")))
	writer.Header().Set("Content-Type", "text/html; charset=UTF-8")
	writer.Header().Set("Cache-Control", "no-store")
	if request.Method == "GET" {
		_, _ = writer.Write(body)
	}
	return true
}
