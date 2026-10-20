package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestAnykaCPU(t *testing.T) {
	total, idle, err := cpuCounters("cpu  10 2 3 40 5 6 7 8 99 99\ncpu0 ignored\n")
	if err != nil || total != 81 || idle != 45 {
		t.Fatalf("Guest double-counted or idle incorrect: %d %d %v", total, idle, err)
	}
	for _, input := range []string{"cpu 1 2", "cpu 1 2 3 4 5 6 7 -8", "cpu0 1 2 3 4 5 6 7 8"} {
		if _, _, err := cpuCounters(input); err == nil {
			t.Fatal("Invalid counters accepted")
		}
	}
}

func TestAnykaGateway(t *testing.T) {
	data := "Iface Destination Gateway Flags\nwlan0 00000000 0100000A 0003 0 0\n"
	if got := defaultGateway(data); got != "192.0.2.1" {
		t.Fatal(got)
	}
	for _, input := range []string{"wlan0 00000000 0100000A 0001", "wlan0 00000000 invalid 0003", "eth0 00000000 0100000A 0003"} {
		if defaultGateway(input) != "" {
			t.Fatal("Non-default gateway accepted")
		}
	}
}

func TestAnykaSmallFile(t *testing.T) {
	file := filepath.Join(t.TempDir(), "state")
	if err := os.WriteFile(file, []byte(strings.Repeat("x", 8193)), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := smallFile(file); err == nil {
		t.Fatal("Unbounded status accepted")
	}
}
