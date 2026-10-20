package app

import "testing"

func TestInternalMediaConfig(t *testing.T) {
	var api struct { Mod struct {
		Listen string `yaml:"listen"`
		AllowPaths []string `yaml:"allow_paths"`
	} `yaml:"api"` }
	data := []byte(`{"api":{"listen":"127.0.0.1:1984","allow_paths":["/","/api/ws"]},"rtsp":{"listen":":554"},"streams":{"preview":"rtsp://127.0.0.1:8555/vs1#backchannel=0"}}`)
	if err := decodeInternal(data, &api); err != nil { t.Fatal(err) }
	if api.Mod.Listen != "127.0.0.1:1984" || len(api.Mod.AllowPaths) != 2 || api.Mod.AllowPaths[1] != "/api/ws" { t.Fatalf("wrong API configuration: %+v", api) }
	var streams struct { Streams map[string]any `yaml:"streams"` }
	if err := decodeInternal(data, &streams); err != nil { t.Fatal(err) }
	if streams.Streams["preview"] != "rtsp://127.0.0.1:8555/vs1#backchannel=0" { t.Fatal("preview source lost") }
	var rtsp struct { Mod struct {
		Listen string `yaml:"listen"`
		Password string `yaml:"password" json:"-"`
		PacketSize uint16 `yaml:"pkt_size"`
	} `yaml:"rtsp"` }
	rtsp.Mod.PacketSize = 1400
	if err := decodeInternal(data, &rtsp); err != nil { t.Fatal(err) }
	if rtsp.Mod.Listen != ":554" || rtsp.Mod.PacketSize != 1400 { t.Fatal("RTSP defaults changed") }
	if err := decodeInternal([]byte(`{"rtsp":{"pkt_size":999999}}`), &rtsp); err == nil { t.Fatal("integer overflow accepted") }
	if PatchConfig(nil, nil) == nil { t.Fatal("media configuration became writable") }
}
