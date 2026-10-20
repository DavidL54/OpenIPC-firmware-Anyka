package main

import (
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestRuntimeStatus(t *testing.T) {
	file := filepath.Join(t.TempDir(), "status.json")
	for _, test := range []struct {
		name, content string
		age           time.Duration
		status        int
	}{
		{"fresh", `{"motion_detected":true}`, time.Second, 200},
		{"stale", `{"motion_detected":false}`, 11 * time.Second, 503},
		{"future", `{}`, -time.Minute, 503},
		{"invalid", `{"motion_detected":`, time.Second, 503},
		{"oversized", strings.Repeat(" ", 4096) + `{}`, time.Second, 503},
	} {
		t.Run(test.name, func(t *testing.T) {
			if err := os.WriteFile(file, []byte(test.content), 0600); err != nil {
				t.Fatal(err)
			}
			modified := time.Now().Add(-test.age)
			if err := os.Chtimes(file, modified, modified); err != nil {
				t.Fatal(err)
			}
			response := httptest.NewRecorder()
			runtimeStatus(response, file)
			if response.Code != test.status {
				t.Fatalf("HTTP %d, expected %d", response.Code, test.status)
			}
			if test.status == 200 && response.Body.String() != test.content {
				t.Fatal("SDK state changed")
			}
			if response.Header().Get("Content-Type") != "application/json" || response.Header().Get("Cache-Control") != "no-store" {
				t.Fatal("Incorrect JSON headers")
			}
		})
	}
	response := httptest.NewRecorder()
	runtimeStatus(response, file+".missing")
	if response.Code != 503 {
		t.Fatal("Missing state must be unavailable")
	}
}
