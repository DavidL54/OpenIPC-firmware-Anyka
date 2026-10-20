"""Derive a native RTSP/API-only gateway from the reviewed camera sources."""
from pathlib import Path


def replace(text, old, new, count=1):
    assert text.count(old) == count, repr(old[:100])
    return text.replace(old, new)


def between(text, first, after, replacement=''):
    start = text.index(first)
    end = text.index(after, start)
    return text[:start] + replacement + text[end:]


def prepare(work: Path):
    p = work / 'go2rtc-anyka.go'
    text = p.read_text()
    for name in ['internal/api', 'internal/api/ws', 'internal/app', 'internal/mp4',
                 'internal/rtsp', 'internal/streams', 'pkg/shell']:
        text = replace(text, '\t"github.com/AlexxIT/go2rtc/' + name + '"\n', '')
    text = replace(text, '\tmediaRTSP "github.com/AlexxIT/go2rtc/pkg/rtsp"\n', '')
    text = replace(text, '"github.com/AlexxIT/go2rtc/pkg/tcp"', 'tcp "github.com/AlexxIT/go2rtc/internal/nativeproto"')
    text = replace(text, '\t"context"\n', '\t"context"\n\t"flag"\n\t"fmt"\n\t"os/signal"\n')
    for field in ['\tpreviewCancel    map[uint64]context.CancelFunc\n', '\tnextPreviewID    uint64\n',
                  '\tvideo            *httputil.ReverseProxy\n', '\tpages            map[string][]byte\n', '\tuiDisabled       bool\n']:
        text = replace(text, field, '')
    text = replace(text, '\tlastNotification time.Time\n', '')
    text = replace(text, '\tvar previews []context.CancelFunc\n', '')
    text = replace(text, '\t\tfor _, cancel := range gateway.previewCancel { previews = append(previews, cancel) }\n', '')
    text = replace(text, '\t\tfor _, cancel := range previews { cancel() }\n', '')
    text = between(text, 'type audioPlaybackOutput struct {', 'func speakerMemoryAvailable()')
    text = between(text, 'func speakerPower(', 'func (gateway *webGateway) audioLive(')
    text = replace(text, '    if envDefault("ANYKA_NATIVE_RTSP", "0") != "1" && (rate != 8000 || nr || mp3) { valid = false }\n', '')
    text = between(text, '\tif envDefault("ANYKA_NATIVE_RTSP", "0") == "1" {\n\t\tgateway.nativeAudioLive', '\nfunc (gateway *webGateway) audioPost(',
                   '\tgateway.nativeAudioLive(request, connection, volume, rate, nr, mp3, sourceRate, sourceChannels, report)\n}\n')
    text = between(text, '\tif envDefault("ANYKA_NATIVE_RTSP", "0") == "1" {\n\t\tgateway.nativeAudioFile', '\nfunc (gateway *webGateway) ServeHTTP(',
                   '\tgateway.nativeAudioFile(writer, request, file.Name(), volume)\n}\n')
    text = between(text, 'func notificationPost(', 'func speakerMemoryAvailable(')
    text = replace(text, 'if gateway.uiDisabled && !headlessRoute(', 'if !headlessRoute(')
    # Keep the previous headless API's authentication and same-origin checks.
    line = next(l for l in text.splitlines(keepends=True) if '\tpublic := request.Method' in l)
    text = replace(text, line, '')
    start = '\tif !authorized && !public {\n'
    text = between(text, start, '\tif request.URL.Path == "/api/privacy" {',
                   '\tif !authorized {\n\t\thttp.Error(writer, "Unauthorized", 401)\n\t\treturn\n\t}\n')
    text = between(text, '\tif public && (strings.HasPrefix(', '\tif request.Method == "GET" && request.URL.Path == "/cgi-bin/metrics.cgi" {')
    text = between(text, '\tif request.URL.Path == \"/api/ntfy\" || request.URL.Path == \"/api/time\" {', '\trequest.Header.Del(\"Authorization\")')
    text = between(text, '\tif !gateway.uiDisabled {\n\t\tpageHeaders(', '\tallowed := public ||', '')
    text = replace(text, '\tallowed := public || request.URL.Path == "/" || request.URL.Path == "/index.html"', '\tallowed := false')
    old = next(l for l in text.splitlines(keepends=True) if l.startswith('\tcase "/cgi-bin/dashboard.cgi"'))
    text = replace(text, old, '\tcase "/cgi-bin/ptz.cgi", "/cgi-bin/gpio.cgi", "/cgi-bin/isp.cgi", "/cgi-bin/snapshot.cgi":\n')
    text = '\n'.join(l for l in text.split('\n') if 'writer.Header().Set("Content-Security-Policy"' not in l)
    text = replace(text, '\tvideoURL, _ := url.Parse("http://127.0.0.1:1984")\n', '')
    text = replace(text, '\t\tuiDisabled: os.Getenv("ANYKA_WEB_UI") == "0",\n', '')
    text = replace(text, 'web: httputil.NewSingleHostReverseProxy(webURL), video: httputil.NewSingleHostReverseProxy(videoURL),', 'web: httputil.NewSingleHostReverseProxy(webURL),')
    text = between(text, '\tmediaRTSP.ServerAuthenticator = gateway.rtspAccess\n', '\tlistener, err := net.Listen("tcp",',
                   '\tgateway.startNativeAuth()\n\tdebug.FreeOSMemory()\n')
    text = replace(text, '\tlog.Printf("[anyka-web] listening on %s, interface enabled=%t", listener.Addr(), !gateway.uiDisabled)',
                   '\tlog.Printf("[anyka-api] listening on %s; native RTSP authentication ready", listener.Addr())')
    text = text[:text.index('func main() {')] + '''func main() {
    version := flag.Bool("version", false, "Print version")
    flag.String("config", "", "Accepted for compatibility; native API uses environment settings")
    flag.Parse()
    if *version { fmt.Println("anyka-gateway headless linux/arm"); return }
    if envDefault("ANYKA_NATIVE_RTSP", "1") != "1" { log.Fatal("Headless internal profile requires native SDK RTSP") }
    if err := os.Setenv("ANYKA_NATIVE_RTSP", "1"); err != nil { log.Fatal(err) }
    startGateway()
    signals := make(chan os.Signal, 1)
    signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
    <-signals
}
'''
    assert not any(n in text for n in ['mediaRTSP.', 'gateway.uiDisabled', 'cachedPage(', 'gateway.previewCancel', 'gateway.video'])
    p.write_text(text)
    # Retain the bounded, body-free RTSP parser exactly; omit the unrelated
    # generic TCP client, RTSP relay and media packages from the dependency tree.
    source = (work / 'pkg/tcp/textproto.go').read_text()
    request = source[source.index('type Request struct {'):source.index('func (r *Request) String()')]
    response = source[source.index('type Response struct {'):source.index('func (r Response) String()')]
    parser = source[source.index('func ReadRequest('):]
    native = work / 'internal/nativeproto'; native.mkdir()
    (native / 'request.go').write_text('''// Request parsing retained from go2rtc pkg/tcp/textproto.go.
package nativeproto
import ("bufio"; "bytes"; "errors"; "fmt"; "net/textproto"; "net/url"; "strings")
''' + request + response + parser)
    p = work / 'anyka-native-auth.go'
    p.write_text(replace(p.read_text(), '"github.com/AlexxIT/go2rtc/pkg/tcp"', 'tcp "github.com/AlexxIT/go2rtc/internal/nativeproto"'))
    pages = (work / 'anyka-pages.go').read_text()
    (work / 'headless-routes.go').write_text('package main\n\n' + pages[pages.index('func headlessRoute('):pages.index('// Only page markup')].replace(', "/api/time", "/api/ntfy"', ''))
    p = work / 'anyka-metrics.go'
    p.write_text(between(p.read_text(), 'func defaultGateway(', 'func cameraMetrics(').replace('\t"time"\n', ''))
    return {'profile': 'native-headless', 'removed': ['OpenIPC HTML/static assets/page cache', 'browser MSE/MP4/WebSocket preview',
            'go2rtc media relay and API/admin listeners', 'legacy non-native speaker helper path', 'UI-only notification test and manual clock buttons', 'unused HTTP/2 support'],
            'preserved': ['native RTSP Digest', 'JPEG Digest and sessions', 'HA login/password/session API', 'PTZ/ISP/GPIO and detection controls',
                          'privacy mode', 'native PCM/MP3 speaker and audio WebSocket', 'metrics', 'NTP synchronization outside Go'],
            'rtsp_parser': 'same bounded ReadRequest body, isolated from unused TCP client/media packages',
            'legacy_recovery': 'validated SD bundle unchanged'}
