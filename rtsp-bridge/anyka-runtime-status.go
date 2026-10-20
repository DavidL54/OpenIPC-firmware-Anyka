package main

import (
	"encoding/json"
	"io"
	"net/http"
	"os"
	"time"
)

// The caller selects a fixed SDK status file after authenticating the request.
// Reading it in-process avoids launching shell/stat/date/cat for every HA poll.
func runtimeStatus(writer http.ResponseWriter, filename string) {
	writer.Header().Set("Content-Type", "application/json")
	writer.Header().Set("Cache-Control", "no-store")
	unavailable := func() {
		writer.WriteHeader(http.StatusServiceUnavailable)
		_, _ = writer.Write([]byte(`{"ok":false,"error":"Camera state unavailable or stale"}`))
	}
	file, err := os.Open(filename)
	if err != nil {
		unavailable()
		return
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil || !info.Mode().IsRegular() || info.Size() > 4096 {
		unavailable()
		return
	}
	age := time.Since(info.ModTime())
	if age < 0 || age > 10*time.Second {
		unavailable()
		return
	}
	data, err := io.ReadAll(io.LimitReader(file, 4097))
	if err != nil || len(data) > 4096 || !json.Valid(data) {
		unavailable()
		return
	}
	_, _ = writer.Write(data)
}
