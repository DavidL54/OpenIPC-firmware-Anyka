package rtsp

import (
	"testing"
	"github.com/AlexxIT/go2rtc/internal/streams"
)

func TestInternalNativePreviewClient(t *testing.T) {
	t.Setenv("ANYKA_NATIVE_RTSP", "1")
	Init()
	if Port != "" { t.Fatal("native mode opened a second public RTSP listener") }
	if !streams.HasProducer("rtsp://127.0.0.1:8555/vs1#backchannel=0") {
		t.Fatal("native mode lost the vendor-loopback MSE source")
	}
}
