package main

import (
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

func smallFile(filename string) ([]byte, error) {
	f, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	data, err := io.ReadAll(io.LimitReader(f, 8193))
	if len(data) > 8192 {
		return nil, errors.New("status file too large")
	}
	return data, err
}

func cpuCounters(data string) (uint64, uint64, error) {
	line, _, _ := strings.Cut(data, "\n")
	fields := strings.Fields(line)
	if len(fields) < 9 || fields[0] != "cpu" {
		return 0, 0, errors.New("invalid CPU counters")
	}
	var total, idle uint64
	// guest and guest_nice are already included in user and nice.
	for i := 1; i <= 8; i++ {
		value, err := strconv.ParseUint(fields[i], 10, 64)
		if err != nil {
			return 0, 0, err
		}
		total += value
		if i == 4 || i == 5 {
			idle += value
		}
	}
	return total, idle, nil
}

func defaultGateway(data string) string {
	for _, line := range strings.Split(data, "\n") {
		fields := strings.Fields(line)
		if len(fields) < 4 || fields[0] != "wlan0" || fields[1] != "00000000" {
			continue
		}
		flags, err := strconv.ParseUint(fields[3], 16, 16)
		if err != nil || flags&3 != 3 {
			continue
		}
		value, err := strconv.ParseUint(fields[2], 16, 32)
		if err != nil {
			continue
		}
		parts := make([]string, 4)
		for i := range parts {
			parts[i] = strconv.FormatUint((value>>(8*i))&255, 10)
		}
		return strings.Join(parts, ".")
	}
	return ""
}

func pageHeaders(request *http.Request, host string) {
	request.Header.Set("X-Anyka-Address", host)
	route, _ := smallFile("/proc/net/route")
	request.Header.Set("X-Anyka-Gateway", defaultGateway(string(route)))
	state := "unavailable"
	if info, err := os.Stat("/tmp/anyka-image.json"); err == nil {
		age := time.Since(info.ModTime())
		if age >= 0 && age <= 10*time.Second {
			state = "running"
		}
	}
	request.Header.Set("X-Anyka-RTSP-State", state)
}

func cameraMetrics(writer http.ResponseWriter) {
	writer.Header().Set("Content-Type", "application/json")
	writer.Header().Set("Cache-Control", "no-store")
	values := make(map[string]any, 15)
	stat, err := smallFile("/proc/stat")
	if err != nil {
		http.Error(writer, "CPU counters unavailable", 503)
		return
	}
	total, idle, err := cpuCounters(string(stat))
	if err != nil {
		http.Error(writer, "CPU counters unavailable", 503)
		return
	}
	values["cpuTotal"], values["cpuIdle"] = total, idle
	memory, err := smallFile("/proc/meminfo")
	if err != nil {
		http.Error(writer, "Memory counters unavailable", 503)
		return
	}
	var kernel uint64
	for _, line := range strings.Split(string(memory), "\n") {
		fields := strings.Fields(line)
		if len(fields) != 3 || fields[2] != "kB" {
			continue
		}
		v, err := strconv.ParseUint(fields[1], 10, 64)
		if err != nil {
			continue
		}
		v *= 1024
		switch fields[0] {
		case "MemTotal:":
			values["memTotal"] = v
		case "MemAvailable:":
			values["memAvailable"] = v
		case "AnonPages:":
			values["memAnon"] = v
		case "Shmem:":
			values["memShmem"] = v
		case "SUnreclaim:", "KernelStack:", "PageTables:":
			kernel += v
		}
	}
	values["memKernel"] = kernel
	for key, filename := range map[string]string{"uptime": "/proc/uptime", "load": "/proc/loadavg", "rx": "/sys/class/net/wlan0/statistics/rx_bytes", "tx": "/sys/class/net/wlan0/statistics/tx_bytes"} {
		data, err := smallFile(filename)
		fields := strings.Fields(string(data))
		if err != nil || len(fields) == 0 {
			http.Error(writer, "System counters unavailable", 503)
			return
		}
		v, err := strconv.ParseFloat(fields[0], 64)
		if err != nil || v < 0 {
			http.Error(writer, "Invalid system counters", 503)
			return
		}
		values[key] = v
	}
	values["wifiRssi"], values["temperatureMilliC"] = nil, nil
	wireless, _ := smallFile("/proc/net/wireless")
	for _, line := range strings.Split(string(wireless), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 4 || fields[0] != "wlan0:" {
			continue
		}
		v, err := strconv.Atoi(strings.TrimSuffix(fields[3], "."))
		if err == nil && v > -120 && v <= 0 {
			values["wifiRssi"] = v
		}
	}
	paths, _ := filepath.Glob("/sys/class/thermal/thermal_zone*/temp")
	for _, filename := range paths {
		kind, _ := smallFile(filepath.Join(filepath.Dir(filename), "type"))
		s := strings.ToLower(string(kind))
		if !strings.Contains(s, "cpu") && !strings.Contains(s, "soc") && !strings.Contains(s, "ak39") {
			continue
		}
		data, _ := smallFile(filename)
		v, err := strconv.Atoi(strings.TrimSpace(string(data)))
		if err == nil && v >= -40000 && v <= 125000 {
			values["temperatureMilliC"] = v
			break
		}
	}
	_ = json.NewEncoder(writer).Encode(values)
}
