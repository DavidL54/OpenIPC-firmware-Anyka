/* Direct SDK frames -> authenticated RTSP/TCP. No RTSP client, transcoder,
 * video relay, unbounded queue or credentials in the SDK process.
 * Built without libc and resolved entirely against the SDK's uClibc runtime.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>
#include "ak_thread.h"
#include "ak_rtsp_server.h"
#include "ak_common.h"
#include "ak_venc.h"
#include "ak_ai.h"
#include "ak_aenc.h"

// Recent musl headers redirect dlsym to __dlsym_time64. This interposer uses
// the vendor uClibc loader, whose ABI exports the original dlsym symbol.
extern void *uclibc_dlsym(void *, const char *) __asm__("dlsym");
#define dlsym uclibc_dlsym

#define FRAME_LIMIT (512U * 1024U)
#define HEADER_LIMIT 8192
#define AUTH_PATH "/conf/anyka-private/native-rtsp-auth.sock"
#define CLIENT_LIMIT 2
#define RTP_BATCH_LIMIT 8192
#define PRIVACY_MARKER "/tmp/anyka-privacy-mode"
#define PRIVACY_MAIN "/mnt/openipc-persist/sdk-rtsp/privacy-slate-vs0.h264"
#define PRIVACY_SUB "/mnt/openipc-persist/sdk-rtsp/privacy-slate-vs1.h264"

static RTSP_SERVER_GET_VIDEO_PARAM_CALLBACK video_param;
static RTSP_SERVER_GET_AUDIO_PARAM_CALLBACK audio_param;
static RTSP_SERVER_PLAY_CALLBACK play_callback;
static RTSP_SERVER_TEARDOWN_CALLBACK teardown_callback;
static RTSP_SERVER_GET_VIDEO_CALLBACK video_callback;
static RTSP_SERVER_GET_AUDIO_CALLBACK audio_callback;
static void *video_user, *audio_user;
static pthread_mutex_t callbacks_lock;
static pthread_mutex_t clients_lock;
static unsigned int active_clients;

/* The sample polls these APIs with block=0 and sleeps between empty reads.
 * Use the documented SDK wait for the next frame. NR/AGC/AEC and the sample's
 * ownership/release of frames are unchanged. Set ANYKA_AUDIO_BLOCKING=0 for
 * a diagnostic comparison or rollback without rebuilding the module.
 */
static int audio_blocking = 1;
static int (*sdk_ai_frame)(int, struct frame *, unsigned char);
static int (*sdk_aenc_stream)(int, struct audio_stream *, unsigned char);
static pthread_once_t audio_wait_once = PTHREAD_ONCE_INIT;

static void resolve_audio_wait(void)
{
	const char *setting = getenv("ANYKA_AUDIO_BLOCKING");
	audio_blocking = !setting || strcmp(setting, "0");
	sdk_ai_frame = dlsym(RTLD_NEXT, "ak_ai_get_frame");
	sdk_aenc_stream = dlsym(RTLD_NEXT, "ak_aenc_get_stream");
	if (!sdk_ai_frame || !sdk_aenc_stream) _exit(1);
	fprintf(stderr, "native SDK audio blocking wait=%d\n", audio_blocking);
}

int ak_ai_get_frame(int handle, struct frame *frame, unsigned char block)
{
	pthread_once(&audio_wait_once, resolve_audio_wait);
	return sdk_ai_frame(handle, frame, block ? block : (unsigned char)audio_blocking);
}

int ak_aenc_get_stream(int handle, struct audio_stream *stream, unsigned char block)
{
	pthread_once(&audio_wait_once, resolve_audio_wait);
	return sdk_aenc_stream(handle, stream, block ? block : (unsigned char)audio_blocking);
}

static struct venc_param native_encoder_parameters(const struct venc_param *input)
{
	struct venc_param parameters = *input;
	if (parameters.enc_out_type == H264_ENC_TYPE && parameters.fps > 0 &&
	    parameters.fps <= 60 && parameters.goplen > parameters.fps)
		parameters.goplen = parameters.fps;
	return parameters;
}

int ak_venc_open(const struct venc_param *input, int *handle)
{
	int (*original)(const struct venc_param *, int *) = dlsym(RTLD_NEXT, "ak_venc_open");
	if (!original) _exit(1);
	const char *enabled = getenv("ANYKA_NATIVE_RTSP");
	if (!input || !enabled || strcmp(enabled, "1")) return original(input, handle);
	struct venc_param parameters = native_encoder_parameters(input);
	if (parameters.goplen != input->goplen)
		fprintf(stderr, "native SDK H264 %ux%u GOP %u -> %u, fps=%u\n", input->width, input->height, input->goplen, parameters.goplen, input->fps);
	return original(&parameters, handle);
}

int ak_venc_open_ex(const struct venc_param *input, const struct venc_rc_param *rate, int *handle)
{
	int (*original)(const struct venc_param *, const struct venc_rc_param *, int *) = dlsym(RTLD_NEXT, "ak_venc_open_ex");
	if (!original) _exit(1);
	const char *enabled = getenv("ANYKA_NATIVE_RTSP");
	if (!input || !enabled || strcmp(enabled, "1")) return original(input, rate, handle);
	struct venc_param parameters = native_encoder_parameters(input);
	if (parameters.goplen != input->goplen)
		fprintf(stderr, "native SDK H264 %ux%u GOP %u -> %u, fps=%u\n", input->width, input->height, input->goplen, parameters.goplen, input->fps);
	return original(&parameters, rate, handle);
}

static unsigned long now_ms(void)
{
	struct { long sec, nsec; } value;
	if (syscall(SYS_clock_gettime32, 1, &value)) return 0;
	return (unsigned long)value.sec * 1000UL + (unsigned long)value.nsec / 1000000UL;
}

static uint32_t random_word(void)
{
	uint32_t value = 0;
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, &value, sizeof(value)) != sizeof(value)) _exit(1);
	close(fd);
	return value;
}

#include "anyka-native-audio-readers.h"

static int serialized_play(char *source)
{
	pthread_mutex_lock(&callbacks_lock);
	int result = play_callback(source);
	if (result >= 0 && result < SDK_READER_LIMIT) {
		struct sdk_audio_reader *reader = &sdk_audio_readers[result];
		memset(reader, 0, sizeof(*reader)); reader->active = 1;
	} else if (result >= 0) {
		teardown_callback(result); result = -1;
	}
	pthread_mutex_unlock(&callbacks_lock);
	return result;
}

static int serialized_teardown(int handle)
{
	if (handle < 0 || handle >= SDK_READER_LIMIT) return -1;
	pthread_once(&audio_reader_once, resolve_audio_readers);
	pthread_mutex_lock(&callbacks_lock);
	struct sdk_audio_reader *reader = &sdk_audio_readers[handle];
	reader->closing = 1;
	if (pthread_setspecific(audio_reader_key, reader)) _exit(1);
	int result = teardown_callback(handle);
	if (pthread_setspecific(audio_reader_key, NULL)) _exit(1);
	if (reader->ring) sdk_reader_delete(reader->ring, (unsigned int)reader->id);
	memset(reader, 0, sizeof(*reader));
	pthread_mutex_unlock(&callbacks_lock);
	return result;
}

#define REGISTER_PARAM(name, type, stored) \
int name(type callback) { \
	int (*original)(type) = dlsym(RTLD_NEXT, #name); \
	if (!original) _exit(1); \
	stored = callback; \
	return original(callback); \
}
REGISTER_PARAM(rtsp_server_set_video_param_callback, RTSP_SERVER_GET_VIDEO_PARAM_CALLBACK, video_param)
REGISTER_PARAM(rtsp_server_set_audio_param_callback, RTSP_SERVER_GET_AUDIO_PARAM_CALLBACK, audio_param)

int rtsp_server_set_play_callback(RTSP_SERVER_PLAY_CALLBACK callback)
{
	int (*original)(RTSP_SERVER_PLAY_CALLBACK) = dlsym(RTLD_NEXT, "rtsp_server_set_play_callback");
	if (!original) _exit(1);
	play_callback = callback;
	return original(serialized_play);
}

int rtsp_server_set_teardown_callback(RTSP_SERVER_TEARDOWN_CALLBACK callback)
{
	int (*original)(RTSP_SERVER_TEARDOWN_CALLBACK) = dlsym(RTLD_NEXT, "rtsp_server_set_teardown_callback");
	if (!original) _exit(1);
	teardown_callback = callback;
	return original(serialized_teardown);
}

int rtsp_server_set_video_callback(RTSP_SERVER_GET_VIDEO_CALLBACK callback, void *user)
{
	int (*original)(RTSP_SERVER_GET_VIDEO_CALLBACK, void *) = dlsym(RTLD_NEXT, "rtsp_server_set_video_callback");
	if (!original) _exit(1);
	video_callback = callback; video_user = user;
	return original(callback, user);
}

int rtsp_server_set_audio_callback(RTSP_SERVER_GET_AUDIO_CALLBACK callback, void *user)
{
	int (*original)(RTSP_SERVER_GET_AUDIO_CALLBACK, void *) = dlsym(RTLD_NEXT, "rtsp_server_set_audio_callback");
	if (!original) _exit(1);
	audio_callback = callback; audio_user = user;
	return original(serialized_audio, user);
}

struct track {
	int enabled, channel;
	uint16_t sequence;
	uint32_t ssrc, packets, octets, timestamp;
};

struct client {
	int fd, auth, handle, stream, playing, video_ready, audio_supported, private_mode;
	char session[16];
	unsigned char *frame;
	struct track tracks[2];
	unsigned long last_control, last_report;
	uint64_t timestamp_origin, ntp_origin;
	unsigned long clock_origin;
	int timestamp_started, audio_clock_started;
	uint32_t audio_next_timestamp;
	unsigned char rtp_batch[RTP_BATCH_LIMIT];
	size_t rtp_batch_length;
	size_t slate_length;
	unsigned long next_slate_ms;
	unsigned long next_silence_ms;
};

static int privacy_enabled(void)
{
	if (!access(PRIVACY_MARKER, F_OK)) return 1;
	return errno != ENOENT; // A marker read failure must never reveal live media.
}

static size_t load_slate(struct client *client)
{
	const char *path = client->stream == 0 ? PRIVACY_MAIN : PRIVACY_SUB;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return 0;
	size_t used = 0;
	while (used < FRAME_LIMIT) {
		ssize_t count = read(fd, client->frame + used, FRAME_LIMIT - used);
		if (count < 0 && errno == EINTR) continue;
		if (count <= 0) break;
		used += (size_t)count;
	}
	unsigned char extra;
	ssize_t overflow = read(fd, &extra, 1);
	close(fd);
	if (used < 5 || overflow != 0 || memcmp(client->frame, "\0\0\0\1", 4)) return 0;
	return used;
}

static void start_timeline(struct client *client, uint64_t sdk_time)
{
	struct { long sec, nsec; } clock;
	if (syscall(SYS_clock_gettime32, 0, &clock)) { client->ntp_origin=0; }
	else client->ntp_origin = ((uint64_t)((uint32_t)clock.sec+2208988800U)<<32) |
		((uint64_t)clock.nsec<<32)/1000000000U;
	client->clock_origin=now_ms();
	client->timestamp_origin=sdk_time; client->timestamp_started=1;
}

/* A slow reader is disconnected rather than retaining a growing backlog.
 * A closed private auth channel revokes an already playing media socket.
 */
static int write_all(int fd, int revoke, const void *input, size_t length)
{
	const unsigned char *data = input;
	unsigned long deadline = now_ms() + 500;
	while (length) {
		struct pollfd events[2] = {{fd, POLLOUT, 0}, {revoke, POLLIN, 0}};
		int result = poll(events, revoke >= 0 ? 2 : 1, 100);
		if (result < 0 && errno == EINTR) continue;
		if (result < 0 || (revoke >= 0 && events[1].revents) ||
		    events[0].revents & (POLLERR | POLLHUP | POLLNVAL) ||
		    (long)(now_ms() - deadline) >= 0) return -1;
		if (!(events[0].revents & POLLOUT)) continue;
		ssize_t count = send(fd, data, length, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
		if (count <= 0) return -1;
		data += count; length -= (size_t)count;
	}
	return 0;
}

static int read_exact(int fd, void *input, size_t length)
{
	unsigned char *data = input;
	unsigned long deadline = now_ms() + 2000;
	while (length) {
		struct pollfd event = {fd, POLLIN, 0};
		int result = poll(&event, 1, 100);
		if (result < 0 && errno == EINTR) continue;
		if (result < 0 || event.revents & (POLLERR | POLLHUP | POLLNVAL) ||
		    (long)(now_ms() - deadline) >= 0) return -1;
		if (!(event.revents & POLLIN)) continue;
		ssize_t count = recv(fd, data, length, MSG_DONTWAIT);
		if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
		if (count <= 0) return -1;
		data += count; length -= (size_t)count;
	}
	return 0;
}

static int connect_auth(void)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_un address = {0};
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, AUTH_PATH);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
		close(fd); return -1;
	}
	return fd;
}

/* Return 1 when permitted, 0 after an explicit denial, -1 on any auth fault. */
static int authenticate(struct client *client, const char *request, size_t size)
{
	uint32_t length = htonl((uint32_t)size);
	if (write_all(client->auth, -1, &length, 4) || write_all(client->auth, -1, request, size) ||
	    read_exact(client->auth, &length, 4)) return -1;
	size = ntohl(length);
	if (size == 0) return 1;
	if (size > 2048) return -1;
	char reply[2048];
	if (read_exact(client->auth, reply, size) || write_all(client->fd, -1, reply, size)) return -1;
	return 0;
}

/* Headers are case insensitive; duplicate critical fields are rejected by
 * the auth parser or the single-value extraction here. */
static int field(const char *request, const char *name, char *output, size_t size)
{
	const char *line = strstr(request, "\r\n");
	int found = 0;
	if (!line || !size) return -1;
	output[0] = 0;
	while (line[2] && line[2] != '\r') {
		line += 2;
		const char *end = strstr(line, "\r\n");
		if (!end) return -1;
		size_t n = strlen(name);
		if ((size_t)(end - line) > n && !strncasecmp(line, name, n) && line[n] == ':') {
			if (found++) return -1;
			const char *value = line + n + 1;
			while (value < end && (*value == ' ' || *value == '\t')) value++;
			if ((size_t)(end - value) >= size) return -1;
			memcpy(output, value, (size_t)(end - value)); output[end-value] = 0;
		}
		line = end;
	}
	return found;
}

static int response(struct client *client, const char *sequence, const char *status,
		    const char *headers, const char *body)
{
	char reply[4096];
	size_t body_length = body ? strlen(body) : 0;
	int size = snprintf(reply, sizeof(reply), "RTSP/1.0 %s\r\nCSeq: %s\r\n%sContent-Length: %u\r\n\r\n%s",
		status, sequence, headers ? headers : "", (unsigned int)body_length, body ? body : "");
	if (size < 0 || (size_t)size >= sizeof(reply)) return -1;
	return write_all(client->fd, client->auth, reply, (size_t)size);
}

static int control(struct client *client, const char *request, size_t length)
{
	int allowed = authenticate(client, request, length);
	if (allowed <= 0) return allowed < 0 ? -1 : 0;
	char method[16], uri[1024], protocol[16], sequence[16], headers[3072];
	if (sscanf(request, "%15s %1023s %15s", method, uri, protocol) != 3 ||
	    strcmp(protocol, "RTSP/1.0") || field(request, "CSeq", sequence, sizeof(sequence)) != 1) return -1;
	const char *host = strstr(uri, "://");
	const char *path = host ? strchr(host + 3, '/') : NULL;
	if (!path || (strncmp(path, "/vs0", 4) && strncmp(path, "/vs1", 4))) return -1;
	int stream = path[3] - '0';
	if (client->stream >= 0 && client->stream != stream)
		return response(client, sequence, "459 Aggregate Operation Not Allowed", NULL, NULL);
	client->stream = stream;
	client->last_control = now_ms();
	if (!strcmp(method, "OPTIONS"))
		return response(client, sequence, "200 OK", "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n", NULL);
	if (!strcmp(method, "DESCRIBE")) {
		char source[8]; snprintf(source, sizeof(source), "vs%d", stream);
		rtsp_video_enc_param video = {0}; rtsp_audio_enc_param audio = {0};
		if (video_param(source, &video) || video.eCodeType != RTSP_VIDEO_H264)
			return response(client, sequence, "503 Service Unavailable", NULL, NULL);
		client->audio_supported = !audio_param(source, &audio) && audio.eCodeType == RTSP_AUDIO_TYPE_PCM_ALAW &&
			audio.iSampleRate == 8000 && audio.iChannelNum == 1;
		char sdp[1536];
		int size = snprintf(sdp, sizeof(sdp), "v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\ns=Anyka SDK direct\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\na=control:*\r\n"
			"m=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1\r\na=framesize:96 %d-%d\r\na=framerate:%d\r\na=control:trackID=0\r\n%s",
			video.iWidth, video.iHeight, video.iFps, client->audio_supported ?
			"m=audio 0 RTP/AVP 8\r\na=rtpmap:8 PCMA/8000/1\r\na=control:trackID=1\r\n" : "");
		if (size < 0 || (size_t)size >= sizeof(sdp)) return -1;
		snprintf(headers, sizeof(headers), "Content-Type: application/sdp\r\nContent-Base: %s/\r\n", uri);
		return response(client, sequence, "200 OK", headers, sdp);
	}
	if (!strcmp(method, "SETUP")) {
		if (client->playing) return response(client, sequence, "455 Method Not Valid in This State", NULL, NULL);
		int track = strstr(path, "/trackID=1") ? 1 : 0;
		char transport[256];
		if (field(request, "Transport", transport, sizeof(transport)) != 1 ||
		    strncasecmp(transport, "RTP/AVP/TCP;", 12) || strstr(transport, "multicast") || strchr(transport, ',') ||
		    (track && !client->audio_supported)) return response(client, sequence, "461 Unsupported Transport", NULL, NULL);
		const char *channels = strstr(transport, "interleaved=");
		unsigned int first, second; char end;
		if (!channels || sscanf(channels, "interleaved=%u-%u%c", &first, &second, &end) < 2 ||
		    first > 254 || second != first + 1 ||
		    (client->tracks[1-track].enabled && abs((int)first-client->tracks[1-track].channel) < 2))
			return response(client, sequence, "461 Unsupported Transport", NULL, NULL);
		client->tracks[track].enabled = 1; client->tracks[track].channel = (int)first;
		snprintf(headers, sizeof(headers), "Transport: RTP/AVP/TCP;unicast;interleaved=%u-%u\r\nSession: %s;timeout=60\r\n", first, second, client->session);
		return response(client, sequence, "200 OK", headers, NULL);
	}
	if (!strcmp(method, "PLAY") || !strcmp(method, "TEARDOWN")) {
		char session[128];
		if (field(request, "Session", session, sizeof(session)) != 1) return response(client, sequence, "454 Session Not Found", NULL, NULL);
		char *separator = strchr(session, ';'); if (separator) *separator = 0;
		if (strcmp(session, client->session)) return response(client, sequence, "454 Session Not Found", NULL, NULL);
		if (!strcmp(method, "TEARDOWN")) {
			response(client, sequence, "200 OK", NULL, NULL); return -1;
		}
		if ((!client->tracks[0].enabled && !client->tracks[1].enabled) || client->playing)
			return response(client, sequence, "455 Method Not Valid in This State", NULL, NULL);
		client->frame = malloc(FRAME_LIMIT);
		if (!client->frame) return response(client, sequence, "503 Service Unavailable", NULL, NULL);
		char source[8]; snprintf(source, sizeof(source), "vs%d", stream);
		client->handle = serialized_play(source); // SDK requests a fresh IDR here.
		if (client->handle < 0) return -1;
		client->playing = 1;
		client->video_ready = !client->tracks[0].enabled;
		const char *url_separator = uri[strlen(uri)-1] == '/' ? "" : "/";
		int size = snprintf(headers, sizeof(headers), "Session: %s\r\nRange: npt=0.000-\r\nRTP-Info: ", client->session);
		for (int i = 0; i < 2; i++) if (client->tracks[i].enabled) {
			int count = snprintf(headers + size, sizeof(headers) - (size_t)size,
				"%surl=%s%strackID=%d;seq=%u;rtptime=0", size > 0 && headers[size-1] != ' ' ? "," : "",
				uri, url_separator, i, client->tracks[i].sequence);
			if (count < 0 || (size_t)count >= sizeof(headers) - (size_t)size - 3) return -1;
			size += count;
		}
		strcpy(headers + size, "\r\n");
		return response(client, sequence, "200 OK", headers, NULL);
	}
	return response(client, sequence, "405 Method Not Allowed", NULL, NULL);
}

static int flush_rtp(struct client *client)
{
	if (!client->rtp_batch_length) return 0;
	int result = write_all(client->fd, client->auth, client->rtp_batch, client->rtp_batch_length);
	client->rtp_batch_length = 0;
	return result;
}

static int rtp(struct client *client, int index, const unsigned char *data, size_t length,
	       uint32_t timestamp, int marker, const unsigned char *fragment, size_t fragment_length)
{
	struct track *track = &client->tracks[index];
	size_t payload = length + fragment_length;
	if (payload > 1424 || payload < length) return -1;
	if (client->rtp_batch_length + 16 + payload > RTP_BATCH_LIMIT && flush_rtp(client)) return -1;
	unsigned char *packet = client->rtp_batch + client->rtp_batch_length;
	packet[0] = '$'; packet[1] = (unsigned char)track->channel;
	uint16_t size = htons((uint16_t)(12 + payload)); memcpy(packet + 2, &size, 2);
	packet[4] = 0x80; packet[5] = (unsigned char)((marker ? 0x80 : 0) | (index ? 8 : 96));
	size = htons(track->sequence++); memcpy(packet + 6, &size, 2);
	uint32_t value = htonl(timestamp); memcpy(packet + 8, &value, 4);
	value = htonl(track->ssrc); memcpy(packet + 12, &value, 4);
	if (fragment_length) memcpy(packet + 16, fragment, fragment_length);
	memcpy(packet + 16 + fragment_length, data, length);
	client->rtp_batch_length += 16 + payload;
	track->packets++; track->octets += (uint32_t)payload; track->timestamp = timestamp;
	// Audio has no added batching latency. Video is flushed at each frame end.
	return index ? flush_rtp(client) : 0;
}

static size_t start_code(const unsigned char *data, size_t length, size_t offset, size_t *prefix)
{
	while (offset < length && length - offset >= 3) {
		const unsigned char *zero = memchr(data + offset, 0, length - offset - 2);
		if (!zero) break;
		size_t i = (size_t)(zero - data);
		offset = i + 1;
		if (data[i+1]) continue;
		if (data[i+2] == 1) { *prefix = 3; return i; }
		if (i+4 <= length && data[i+2] == 0 && data[i+3] == 1) { *prefix = 4; return i; }
	}
	*prefix = 0; return length;
}

static int send_video(struct client *client, rtsp_frame_info *frame)
{
	unsigned char *data = client->frame;
	size_t length = frame->uiDataLen, prefix;
	size_t current = start_code(data, length, 0, &prefix);
	int idr = 0;
	// Inspect before sending: do not emit reference frames or audio before IDR.
	// Once started, the packetizer itself is the only required Annex-B scan.
	for (size_t i = current, p = prefix; !client->video_ready && i < length && p;) {
		size_t begin = i + p, next_prefix;
		size_t next = start_code(data, length, begin, &next_prefix);
		if (begin < next && (data[begin] & 31) == 5) { idr = 1; break; }
		i = next; p = next_prefix;
	}
	if (!client->video_ready && !idr) return 0;
	if (!client->timestamp_started) {
		start_timeline(client, frame->u64Time);
	}
	uint32_t timestamp = (uint32_t)((frame->u64Time - client->timestamp_origin) * 90ULL);
	while (current < length && prefix) {
		size_t begin = current + prefix, next_prefix;
		size_t next = start_code(data, length, begin, &next_prefix);
		size_t size = next - begin;
		while (size && data[begin + size - 1] == 0) size--;
		if (size) {
			int last = next == length;
			if (size <= 1400) {
				if (rtp(client, 0, data + begin, size, timestamp, last, NULL, 0)) return -1;
			} else {
				unsigned char fragment[2] = {(unsigned char)((data[begin] & 0xE0) | 28), (unsigned char)((data[begin] & 31) | 0x80)};
				for (size_t offset = 1; offset < size;) {
					size_t count = size - offset; if (count > 1398) count = 1398;
					int end = offset + count == size;
					if (end) fragment[1] |= 0x40;
					if (rtp(client, 0, data + begin + offset, count, timestamp, last && end, fragment, 2)) return -1;
					fragment[1] &= 31; offset += count;
				}
			}
		}
		current = next; prefix = next_prefix;
	}
	if (flush_rtp(client)) return -1;
	if (idr) client->video_ready = 1;
	return 0;
}

static int sender_report(struct client *client, int index)
{
	struct track *track = &client->tracks[index];
	if (!track->enabled || !track->packets) return 0;
	unsigned char packet[32] = {'$', 0, 0, 28, 0x80, 200, 0, 6};
	packet[1] = (unsigned char)(track->channel + 1);
	// Both tracks describe the same running clock, rather than pairing the
	// current NTP time with an old packet's RTP timestamp. This keeps A/V
	// synchronization stable when capture/AEC temporarily changes its delay.
	unsigned long elapsed = now_ms()-client->clock_origin;
	uint64_t ntp = client->ntp_origin+((uint64_t)elapsed<<32)/1000U;
	uint32_t stamp=(uint32_t)((uint64_t)elapsed*(index ? 8U : 90U));
	uint32_t words[6] = {track->ssrc, (uint32_t)(ntp>>32),
		(uint32_t)ntp, stamp, track->packets, track->octets};
	for (int i = 0; i < 6; i++) { uint32_t word = htonl(words[i]); memcpy(packet + 8 + i*4, &word, 4); }
	return write_all(client->fd, client->auth, packet, sizeof(packet));
}

/* PCMA contains exactly one 8 kHz sample per byte. The SDK ADC timestamp
 * can jump when AO enables/disables AEC. Anchor the first audio frame to the
 * shared monotonic video origin, then advance by encoded samples rather than that clock.
 */
static int send_audio(struct client *client, const unsigned char *data, size_t length, uint64_t time_ms)
{
	if (!client->video_ready) return 0;
	if (!client->timestamp_started) {
		start_timeline(client, time_ms);
	}
	if (!client->audio_clock_started) {
		client->audio_next_timestamp = (uint32_t)((now_ms()-client->clock_origin)*8UL);
		client->audio_clock_started = 1;
	}
	if (rtp(client, 1, data, length, client->audio_next_timestamp, 0, NULL, 0)) return -1;
	client->audio_next_timestamp += (uint32_t)length;
	return 0;
}

static int media(struct client *client)
{
	int private = privacy_enabled();
	if (private != client->private_mode) {
		if (private) {
			// The SDK keeps its bounded rings drained, but only this static IDR
			// can be sent. Missing slate assets close the peer rather than leak.
			if (!client->tracks[0].enabled) return -1;
			client->slate_length = load_slate(client);
			if (!client->slate_length) return -1;
			client->next_slate_ms = 0;
			client->next_silence_ms = 0;
		} else {
			client->slate_length = 0;
		}
		client->private_mode = private;
		client->video_ready = 0; // Require a complete IDR after either switch.
		client->audio_clock_started = 0;
	}
	if (client->tracks[0].enabled) {
		rtsp_frame_info frame = {0};
		int result = video_callback(client->handle, &frame, video_user);
		if (!private && result >= 0 && frame.uiDataLen) {
			if (!frame.pData || frame.uiDataLen > FRAME_LIMIT) return -1;
			memcpy(client->frame, frame.pData, frame.uiDataLen);
			if (!privacy_enabled() && send_video(client, &frame)) return -1;
		}
		if (private && (!client->next_slate_ms || (long)(now_ms() - client->next_slate_ms) >= 0)) {
			unsigned long current = now_ms();
			rtsp_frame_info slate = {0};
			slate.uiDataLen = (unsigned int)client->slate_length;
			slate.u64Time = client->timestamp_started ?
				client->timestamp_origin + current - client->clock_origin : current;
			if (send_video(client, &slate)) return -1;
			client->next_slate_ms = current + 1000;
		}
	}
	if (private && client->tracks[1].enabled && client->video_ready &&
	    (!client->next_silence_ms || (long)(now_ms() - client->next_silence_ms) >= 0)) {
		rtsp_frame_info ignored = {0};
		serialized_audio(client->handle, &ignored, audio_user);
		unsigned char silence[160]; memset(silence, 0xD5, sizeof(silence));
		if (send_audio(client, silence, sizeof(silence), now_ms())) return -1;
		client->next_silence_ms = now_ms() + 20;
	}
	if (!private && client->tracks[1].enabled && client->video_ready) {
		// Consume historical frames promptly instead of spending several
		// seconds reading the 256 KiB SDK audio ring one frame per poll.
		for (unsigned int skipped=0; skipped<64; skipped++) {
			rtsp_frame_info frame = {0};
			int result = serialized_audio(client->handle, &frame, audio_user);
			if (result < 0 || !frame.uiDataLen) break;
			if (!frame.pData || frame.uiDataLen > 1400) return -1;
			if (!client->audio_clock_started && frame.u64Time < client->timestamp_origin) continue;
			unsigned char audio[1400]; memcpy(audio, frame.pData, frame.uiDataLen);
			if (!privacy_enabled() && send_audio(client, audio, frame.uiDataLen, frame.u64Time)) return -1;
			break;
		}
	}
	if (now_ms() - client->last_report >= 5000) {
		if (sender_report(client, 0) || sender_report(client, 1)) return -1;
		client->last_report = now_ms();
	}
	return 0;
}

static void *serve_client(void *argument)
{
	struct client *client = argument;
	char input[HEADER_LIMIT + 1]; size_t used = 0;
	client->last_control = now_ms();
	client->auth = connect_auth();
	if (client->auth < 0) goto done;
	for (;;) {
		struct pollfd events[2] = {{client->fd, POLLIN, 0}, {client->auth, POLLIN, 0}};
		int result = poll(events, 2, client->playing ? 5 : 100);
		if (result < 0 && errno == EINTR) continue;
		if (result < 0 || events[1].revents || events[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
		if (events[0].revents & POLLIN) {
			if (used == HEADER_LIMIT) break;
			ssize_t count = recv(client->fd, input + used, HEADER_LIMIT - used, MSG_DONTWAIT);
			if (count < 0 && errno == EAGAIN) continue;
			if (count <= 0) break;
			used += (size_t)count; input[used] = 0;
		}
		int failed = 0;
		while (used) {
			size_t length;
			if (input[0] == '$') {
				if (used < 4) break;
				length = 4 + ((unsigned char)input[2] * 256U + (unsigned char)input[3]);
				if (length > 2048) { failed = 1; break; }
				if (used < length) break;
			} else {
				char *end = strstr(input, "\r\n\r\n");
				if (!end) break;
				length = (size_t)(end - input) + 4;
				if (memchr(input, 0, length)) { failed = 1; break; }
				char saved = input[length]; input[length] = 0;
				int status = control(client, input, length); input[length] = saved;
				if (status < 0) { failed = 1; break; }
			}
			used -= length; memmove(input, input + length, used); input[used] = 0;
		}
		if (failed || used == HEADER_LIMIT) break;
		unsigned long timeout = client->playing ? 120000 : 5000;
		if (now_ms() - client->last_control > timeout) break;
		if (client->playing && media(client)) break;
	}
done:
	if (client->handle >= 0) serialized_teardown(client->handle);
	if (client->auth >= 0) close(client->auth);
	close(client->fd); free(client->frame); free(client);
	pthread_mutex_lock(&clients_lock); active_clients--; pthread_mutex_unlock(&clients_lock);
	return NULL;
}

static void *listen_native(void *unused)
{
	(void)unused;
	int listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) return NULL;
	int one = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in address = {0}; address.sin_family = AF_INET;
	address.sin_port = htons(554); address.sin_addr.s_addr = htonl(INADDR_ANY);
	int (*original_bind)(int, const struct sockaddr *, socklen_t) = dlsym(RTLD_NEXT, "bind");
	if (!original_bind || original_bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 2)) {
		fprintf(stderr, "native RTSP listener unavailable\n"); close(listener); return NULL;
	}
	fprintf(stderr, "native SDK RTSP/TCP listening on :554 (private auth required)\n");
	for (;;) {
		int fd = accept(listener, NULL, NULL);
		if (fd < 0) { if (errno == EINTR) continue; break; }
		pthread_mutex_lock(&clients_lock);
		int full = active_clients >= CLIENT_LIMIT;
		if (!full) active_clients++;
		pthread_mutex_unlock(&clients_lock);
		if (full) { close(fd); continue; }
		struct client *client = calloc(1, sizeof(*client));
		if (!client) { close(fd); pthread_mutex_lock(&clients_lock); active_clients--; pthread_mutex_unlock(&clients_lock); continue; }
		client->fd = fd; client->auth = -1; client->handle = -1; client->stream = -1;
		snprintf(client->session, sizeof(client->session), "%08x", random_word());
		for (int i = 0; i < 2; i++) { client->tracks[i].ssrc = random_word(); client->tracks[i].sequence = (uint16_t)random_word(); }
		int buffer = 32768;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
		ak_pthread_t thread;
		if (ak_thread_create(&thread, serve_client, client, 64*1024, -1)) {
			close(fd); free(client); pthread_mutex_lock(&clients_lock); active_clients--; pthread_mutex_unlock(&clients_lock);
		} else ak_thread_detach(thread);
	}
	close(listener); return NULL;
}

#include "anyka-native-audio.h"

int rtsp_server_start(void)
{
	int (*original)(void) = dlsym(RTLD_NEXT, "rtsp_server_start");
	if (!original) _exit(1);
	// Runtime initialization, rather than a musl mutex initializer, preserves
	// uClibc's mutex layout. No musl library is loaded into the SDK.
	pthread_mutex_init(&callbacks_lock, NULL); pthread_mutex_init(&clients_lock, NULL);
	int result = original();
	const char *enabled = getenv("ANYKA_NATIVE_RTSP");
	if (result == 0 && enabled && !strcmp(enabled, "1") && video_param && audio_param &&
	    play_callback && teardown_callback && video_callback && audio_callback) {
		ak_pthread_t thread;
		if (!ak_thread_create(&thread, listen_native, NULL, 64*1024, -1)) ak_thread_detach(thread);
		if (!ak_thread_create(&thread, listen_native_audio, NULL, 64*1024, -1)) ak_thread_detach(thread);
	}
	return result;
}
