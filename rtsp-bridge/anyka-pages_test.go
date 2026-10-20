package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestAnykaPagesBounded(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("X-Anyka-Logged-In") != "true" {
			t.Error("Wrong page context")
		}
		w.Header().Set("Content-Type", "text/html")
		_, _ = w.Write([]byte(strings.Repeat("x", 20000)))
	}))
	defer server.Close()
	pages := preloadPages(server.URL)
	total := 0
	for _, body := range pages {
		total += len(body)
	}
	if total > 65536 || len(pages) == 0 {
		t.Fatal("Unbounded or empty page cache")
	}
}

func TestAnykaPageContextAndQueries(t *testing.T) {
	pages := map[string][]byte{"/cgi-bin/camera.cgi?tab=image": []byte("__ANYKA_ADDRESS__ __ANYKA_GATEWAY__ __ANYKA_RTSP_STATE__")}
	request := httptest.NewRequest("GET", "http://192.0.2.10/cgi-bin/camera.cgi?tab=image", nil)
	request.Header.Set("X-Anyka-Address", "192.0.2.10")
	request.Header.Set("X-Anyka-Gateway", "192.0.2.1")
	request.Header.Set("X-Anyka-RTSP-State", "running")
	response := httptest.NewRecorder()
	if !cachedPage(response, request, pages) || response.Body.String() != "192.0.2.10 192.0.2.1 running" {
		t.Fatal("Stale network or SDK state in page")
	}
	request.Method = "POST"
	if cachedPage(httptest.NewRecorder(), request, pages) {
		t.Fatal("POST was cached")
	}
	request.Method = "GET"
	request.URL.RawQuery = "tab=image&action=reset"
	if cachedPage(httptest.NewRecorder(), request, pages) {
		t.Fatal("Action query was cached")
	}
}
