/* Private, credit-based PCM -> SDK AO. The authenticated web gateway alone
 * can connect; no password, public listener or additional audio process.
 * One frame is acknowledged only after AO accepts it, so no application queue
 * grows. The listener supervises and cancels a blocked SDK writer.
 */
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include "ak_ao.h"
#include "ak_ai.h"
#include "ak_adec.h"
#include "ak_vqe.h"

#define NATIVE_AUDIO_PATH "/conf/anyka-private/native-audio.sock"
#define PCM_LIMIT 1280
#define PCM_RATE 16000UL

struct native_ao_api {
	int (*open)(const struct ak_audio_out_param *, int *);
	int (*gain)(int, int);
	int (*volume)(int, int);
	int (*send)(int, unsigned char *, int, int *);
	int (*status)(int, struct ak_dev_buf_status *);
	int (*flush)(int);
	int (*cancel)(int);
	int (*close)(int);
	int (*nr)(int, int);
};
static struct native_ao_api native_ao;

static int native_ao_load(void)
{
	static void *library;
	static int ready = -1;
	if (library) return ready;
	// LD_LIBRARY_PATH is the validated SDK/uClibc library directory.
	library = dlopen("libplat_ao.so", RTLD_NOW | RTLD_LOCAL);
	if (!library) { fprintf(stderr, "native AO load: %s\n", dlerror()); return -1; }
#define AO_SYMBOL(member, name) do { native_ao.member = dlsym(library, name); if (!native_ao.member) return -1; } while (0)
	AO_SYMBOL(open, "ak_ao_open"); AO_SYMBOL(gain, "ak_ao_set_gain");
	AO_SYMBOL(volume, "ak_ao_set_volume");
	AO_SYMBOL(send, "ak_ao_send_frame"); AO_SYMBOL(status, "ak_ao_get_buf_status");
	AO_SYMBOL(flush, "ak_ao_flush"); AO_SYMBOL(cancel, "ak_ao_cancel"); AO_SYMBOL(close, "ak_ao_close");
	AO_SYMBOL(nr, "ak_ao_enable_nr");
#undef AO_SYMBOL
	ready = 0; return 0;
}

static int native_speaker_power(int enabled)
{
	int lock = open("/tmp/anyka-gpio.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (lock < 0) { fprintf(stderr, "native GPIO lock open errno=%d\n", errno); return -1; }
	unsigned long deadline = now_ms() + 250;
	while (flock(lock, LOCK_EX | LOCK_NB)) {
		if (errno != EWOULDBLOCK || (long)(now_ms()-deadline) >= 0) { fprintf(stderr, "native GPIO flock errno=%d\n", errno); close(lock); return -1; }
		poll(NULL, 0, 10);
	}
	int fd = open("/dev/gv-gpio", O_RDWR | O_CLOEXEC);
	struct { int pin, direction, value, force; } gpio = {67, 1, 0, 0};
	int result = -1;
	if (fd >= 0 && ioctl(fd, 0x40044771UL, &gpio) >= 0 &&
	    ioctl(fd, 0x4004476eUL, &gpio) >= 0) {
		gpio.value = enabled ? 0 : 1;
		int wanted = gpio.value;
		if (ioctl(fd, 0x40044770UL, &gpio) >= 0 && ioctl(fd, 0x40044771UL, &gpio) >= 0 && gpio.value == wanted) result = 0;
	}
	if (result) fprintf(stderr, "native GPIO ioctl errno=%d fd=%d value=%d\n", errno, fd, gpio.value);
	if (fd >= 0) close(fd);
	close(lock); return result;
}

static int native_pcm_length(unsigned int length)
{
	return length > 0 && length <= PCM_LIMIT && !(length & 1);
}

struct native_audio_client {
	pthread_mutex_t lock;
	int fd, handle, finished, stopped;
	unsigned long progress;
};

static void native_audio_progress(struct native_audio_client *client)
{
	pthread_mutex_lock(&client->lock); client->progress = now_ms(); pthread_mutex_unlock(&client->lock);
}

struct native_dac_state {
    unsigned long started, received, frames, max_send_ms, checked_bytes, next_playback;
    int peak_queue;
};
static int native_dac_send(struct native_audio_client *client, int handle, unsigned char *pcm, unsigned int length, int rate, struct native_dac_state *dac)
{
    if (!native_pcm_length(length)) return -1;
		dac->received += length;
		if (dac->received > (uint64_t)(now_ms()-dac->started)*(rate*2UL)/1000UL + PCM_LIMIT*2) return -1;
		// Network/socket buffers can deliver several late packets at once.
		// Pace the DAC as well as bounding input rate, so those bursts do not
		// turn into a growing speaker queue. Allow one frame of timer jitter.
		unsigned long duration = length*1000UL/(rate*2UL);
		long delay = (long)(dac->next_playback-now_ms());
		if (delay > 0) poll(NULL,0,(int)delay);
		else if (delay < -(long)duration) dac->started = now_ms()-(dac->received-length)*1000UL/(rate*2UL);
		dac->next_playback = dac->started+dac->received*1000UL/(rate*2UL);
		int sent = 0; unsigned long before = now_ms();
		if (native_ao.send(handle, pcm, (int)length, &sent) || sent != (int)length) return -1;
		unsigned long sent_at = now_ms();
		// Status getters themselves emit several SDK warning lines. Sampling
		// once per 320 ms of PCM preserves every log without spending most
		// of the ARM926's CPU formatting per-frame diagnostics.
		if (dac->received-dac->checked_bytes >= (unsigned long)rate*2*320/1000) {
			dac->checked_bytes = dac->received;
			struct ak_dev_buf_status buffer = {0};
			if (native_ao.status(handle, &buffer)) return -1;
			int queue = buffer.buf_remain_len > buffer.devbuf_remain_len ? buffer.buf_remain_len : buffer.devbuf_remain_len;
			if (queue > dac->peak_queue) dac->peak_queue = queue;
			if (queue > 8192) { fprintf(stderr, "native speaker backlog stalled: vqe=%d driver=%d\n", buffer.buf_remain_len, buffer.devbuf_remain_len); return -1; }

		}
		dac->frames++; unsigned long elapsed = sent_at-before;
		if (elapsed > dac->max_send_ms) dac->max_send_ms = elapsed;
		native_audio_progress(client);
    return 0;
}
struct native_mp3_api {
    int (*open)(const struct adec_param *, int *);
    int (*set_length)(int, unsigned int);
    int (*send)(int, const unsigned char *, unsigned int, unsigned char);
    int (*get)(int, struct frame *, unsigned char);
    int (*release)(int, struct frame *);
    int (*end)(int);
    int (*close)(int);
    int (*res_open)(enum ak_vqe_type, int *);
    int (*res_attr)(int, struct ak_audio_data_attr *, struct ak_audio_data_attr *);
    int (*res_process)(int, unsigned char *, unsigned int, unsigned char **, unsigned int *);
    int (*res_close)(int);
};
static struct native_mp3_api native_mp3;
static int native_mp3_load(void)
{
    static int ready;
    if (ready) return 0;
    void *decoder = dlopen("libmpi_adec.so", RTLD_NOW | RTLD_GLOBAL);
    void *codec = dlopen("libakmp3.so", RTLD_NOW | RTLD_GLOBAL);
    if (!decoder || !codec) return -1;
    int (*registration)(adec_decode_s *) = dlsym(decoder, "ak_adec_registerdecoder");
    adec_decode_s attr = {0}; attr.enType = AK_AUDIO_TYPE_MP3;
    attr.pfnDecOpen = dlsym(codec, "ADEC_OpenMp3"); attr.pfnDecVersion = dlsym(codec, "ADEC_Mp3_GetVersionInfo");
    if (!registration || !attr.pfnDecOpen || !attr.pfnDecVersion || registration(&attr)) return -1;
#define MP3_SYMBOL(member, name) do { native_mp3.member = dlsym(decoder, name); if (!native_mp3.member) return -1; } while (0)
    MP3_SYMBOL(open, "ak_adec_open"); MP3_SYMBOL(set_length, "ak_adec_set_output_pcm_len");
    MP3_SYMBOL(send, "ak_adec_send_stream"); MP3_SYMBOL(get, "ak_adec_get_frame");
    MP3_SYMBOL(release, "ak_adec_release_frame"); MP3_SYMBOL(end, "ak_adec_send_stream_end"); MP3_SYMBOL(close, "ak_adec_close");
#undef MP3_SYMBOL
    void *effect = dlopen("libplat_audio_effect.so", RTLD_NOW | RTLD_GLOBAL);
    void *resample = dlopen("libakresample.so", RTLD_NOW | RTLD_GLOBAL);
    int (*registration_res)(void) = resample ? dlsym(resample, "ak_resample_register") : NULL;
    if (!effect || !registration_res || registration_res()) return -1;
    native_mp3.res_open = dlsym(effect, "ak_vqe_open"); native_mp3.res_attr = dlsym(effect, "ak_vqe_set_res_attr");
    native_mp3.res_process = dlsym(effect, "ak_vqe_process"); native_mp3.res_close = dlsym(effect, "ak_vqe_close");
    if (!native_mp3.res_open || !native_mp3.res_attr || !native_mp3.res_process || !native_mp3.res_close) return -1;
    ready = 1; return 0;
}

static int native_mp3_frames(struct native_audio_client *client, int handle, int decoder, int resampler, int channels, struct native_dac_state *dac, int ending)
{
    /* Every frame is decoded and played before the producer gets more credit.
     * The SDK decoder owns its bounded ring, never a whole MP3 in camera RAM. */
    unsigned long deadline = now_ms()+2000;
    for (int count = 0; count < 128; count++) {
        struct frame pcm = {0};
        int result = native_mp3.get(decoder, &pcm, 0);
        if (result) {
            if (result == ERROR_ADEC_STREAM_END) return 0;
            if (result != ERROR_ADEC_GET_FRAME_NO_DATA && result != ERROR_ADEC_NO_DECODE_DATA) return -1;
            if (!ending) return 0;
            if ((long)(now_ms()-deadline) >= 0) return -1;
            native_audio_progress(client); poll(NULL, 0, 20); continue;
        }
        int sent = -1;
        if (pcm.data && pcm.len <= PCM_LIMIT && pcm.len%(channels*2) == 0) {
            int16_t mono[PCM_LIMIT/2]; unsigned char *samples = pcm.data;
            unsigned int length = pcm.len;
            if (channels == 2) {
                int16_t *stereo = (int16_t *)pcm.data;
                length /= 2;
                for (unsigned int i = 0; i < length/2; i++) mono[i] = ((int)stereo[i*2]+stereo[i*2+1])/2;
                samples = (unsigned char *)mono;
            }
            unsigned char *converted = resampler >= 0 ? NULL : samples;
            unsigned int bytes = resampler >= 0 ? 0 : length;
            if (resampler >= 0 && native_mp3.res_process(resampler, samples, length, &converted, &bytes)) sent = -1;
            else if (bytes > PCM_LIMIT || (bytes && !converted)) sent = -1;
            else sent = bytes ? native_dac_send(client, handle, converted, bytes, 8000, dac) : 0;
        }
        native_mp3.release(decoder, &pcm);
        if (sent) return -1;
    }
    return -1;
}

static void *native_audio_play(void *argument)
{
	struct native_audio_client *client = argument;
	unsigned char header[8], status = 1;
	int handle = -1, decoder = -1, mp3 = 0, resampler = -1, source_rate = 8000, source_channels = 1;
	struct native_dac_state dac = {0}; dac.started = now_ms();
    unsigned long compressed = 0;
	int rate = 8000, reduce_noise = 0;
	if (read_exact(client->fd, header, sizeof(header))) goto done;
    mp3 = !memcmp(header, "MP02", 4);
    if (!memcmp(header, "AO02", 4) || mp3) {
		uint32_t options[2];
		if (read_exact(client->fd, options, sizeof(options))) goto done;
		rate = ntohl(options[0]); reduce_noise = ntohl(options[1]);
		if ((rate != 8000) || (reduce_noise != 0 && reduce_noise != 1)) goto done;
		if (mp3) {
			if (read_exact(client->fd, options, sizeof(options))) goto done;
			source_rate = ntohl(options[0]); source_channels = ntohl(options[1]);
			if (source_channels != 1 && source_channels != 2) goto done;
			if (source_rate != 8000 && source_rate != 11025 && source_rate != 12000 && source_rate != 16000 && source_rate != 22050 && source_rate != 24000 && source_rate != 32000 && source_rate != 44100 && source_rate != 48000) goto done;
		}
	} else if (memcmp(header, "AO01", 4)) goto done;
	uint32_t encoded; memcpy(&encoded, header+4, 4);
	int32_t volume = (int32_t)ntohl(encoded);
	if (volume < -30 || volume > 0 || native_ao_load() || native_speaker_power(0)) goto done;

	struct ak_audio_out_param parameters = {{AK_AUDIO_SAMPLE_RATE_8000, AK_AUDIO_SAMPLE_BIT_16, AUDIO_CHANNEL_MONO}, DEV_DAC};
	parameters.pcm_data_attr.sample_rate = rate;
	if (native_ao.open(&parameters, &handle)) goto done;
	pthread_mutex_lock(&client->lock);
	client->handle = handle;
	int stopped = client->stopped;
	pthread_mutex_unlock(&client->lock);
	if (stopped || native_ao.gain(handle, DAC_DEV_MAX_GAIN) || native_ao.volume(handle, volume) ||
	    (reduce_noise && native_ao.nr(handle, 1)) || native_speaker_power(1)) goto done;
	if (mp3) {
        if (native_mp3_load()) goto done;
        struct adec_param parameters = {{AK_AUDIO_SAMPLE_RATE_8000, AK_AUDIO_SAMPLE_BIT_16, AUDIO_CHANNEL_MONO}, AK_AUDIO_TYPE_MP3};
        if (native_mp3.open(&parameters, &decoder) || native_mp3.set_length(decoder, PCM_LIMIT)) goto done;
        if (source_rate != 8000) {
            struct ak_audio_data_attr input = {source_rate, AK_AUDIO_SAMPLE_BIT_16, AUDIO_CHANNEL_MONO};
            struct ak_audio_data_attr output = {8000, AK_AUDIO_SAMPLE_BIT_16, AUDIO_CHANNEL_MONO};
            if (native_mp3.res_open(AK_VQE_RESAMPLE, &resampler) || native_mp3.res_attr(resampler, &input, &output)) goto done;
        }
    }
    status = 0;
    if (write_all(client->fd, -1, &status, 1)) goto done;
	fprintf(stderr, "native SDK speaker ready, mono%d PCM, NR=%d, paced frames\n", rate, reduce_noise);
	dac.next_playback = now_ms();
	native_audio_progress(client);
	for (;;) {
		uint16_t prefix;
		if (read_exact(client->fd, &prefix, 2)) break;
		unsigned int length = ntohs(prefix);
		if (!length) {
            if (mp3 && (native_mp3.end(decoder) || native_mp3_frames(client, handle, decoder, resampler, source_channels, &dac, 1))) break;
			if (native_ao.flush(handle)) break;
			unsigned long deadline = now_ms()+1000;
			struct ak_dev_buf_status remaining = {0};
			int result;
			do {
				result = native_ao.status(handle, &remaining);
				if (result || (!remaining.buf_remain_len && !remaining.devbuf_remain_len)) break;
				poll(NULL, 0, 80);
			} while ((long)(now_ms()-deadline) < 0);
			status = result || remaining.buf_remain_len || remaining.devbuf_remain_len ? 1 : 0;
			write_all(client->fd, -1, &status, 1); break;
		}
		if (!length || length > PCM_LIMIT || (!mp3 && !native_pcm_length(length))) break;
		unsigned char pcm[PCM_LIMIT];
		if (read_exact(client->fd, pcm, length)) break;
        if (mp3) {
            compressed += length;
            if (compressed > 20UL*1024*1024 || native_mp3.send(decoder, pcm, length, 0) || native_mp3_frames(client, handle, decoder, resampler, source_channels, &dac, 0)) break;
        } else if (native_dac_send(client, handle, pcm, length, rate, &dac)) break;

		if (write_all(client->fd, -1, &status, 1)) break;
	}
done:
	// Mute first, including invalid protocol, cancel, EOF and SDK failures.

	if (native_speaker_power(0)) fprintf(stderr, "native SDK speaker mute failed\n");
    if (decoder >= 0) native_mp3.close(decoder);
    if (resampler >= 0) native_mp3.res_close(resampler);
	pthread_mutex_lock(&client->lock);
	if (handle >= 0) { native_ao.cancel(handle); native_ao.close(handle); }
	client->handle = -1;
	fprintf(stderr, "native speaker stopped: frames=%lu bytes=%lu peak_queue=%d max_send_ms=%lu\n", dac.frames, dac.received, dac.peak_queue, dac.max_send_ms);
	client->finished = 1;
	pthread_mutex_unlock(&client->lock);
	shutdown(client->fd, SHUT_RDWR);
	return NULL;
}

static void *listen_native_audio(void *unused)
{
	(void)unused;
	// start.sh and the web gateway prepare this root-owned 0700 RAM directory.
	// Avoid musl's time64 stat ABI in the vendor uClibc process.
	int directory = open("/conf/anyka-private", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (directory < 0) return NULL;
	close(directory);
	int listener = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listener < 0) return NULL;
	struct sockaddr_un address = {0}; address.sun_family = AF_UNIX;
	strcpy(address.sun_path, NATIVE_AUDIO_PATH);
	unlink(NATIVE_AUDIO_PATH);
	if (bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
	    chmod(NATIVE_AUDIO_PATH, 0600) || listen(listener, 1)) { close(listener); unlink(NATIVE_AUDIO_PATH); return NULL; }
	fprintf(stderr, "native SDK speaker private socket ready\n");
	for (;;) {
		int fd = accept(listener, NULL, NULL);
		if (fd < 0) { if (errno == EINTR) continue; break; }
		struct ucred credentials; socklen_t size = sizeof(credentials);
		if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) || credentials.uid != 0) { close(fd); continue; }
		int capacity = 2048; setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &capacity, sizeof(capacity));
		struct native_audio_client client = {0}; client.fd = fd; client.handle = -1; client.progress = now_ms();
		pthread_mutex_init(&client.lock, NULL);
		ak_pthread_t thread;
		if (!ak_thread_create(&thread, native_audio_play, &client, 64*1024, -1)) {
			unsigned long start = now_ms();
			for (;;) {
				struct pollfd event = {fd, 0, 0}; poll(&event, 1, 100);
				pthread_mutex_lock(&client.lock);
				int finished = client.finished;
				if (!finished && !client.stopped && ((event.revents & (POLLHUP | POLLERR | POLLNVAL)) ||
				    now_ms()-client.progress > 3000 || now_ms()-start > 300000)) {
					client.stopped = 1; shutdown(fd, SHUT_RDWR);
					if (client.handle >= 0) native_ao.cancel(client.handle);
				}
				pthread_mutex_unlock(&client.lock);
				if (finished) break;
			}
			ak_thread_join(thread);
		}
		pthread_mutex_destroy(&client.lock); close(fd);
	}
	close(listener); unlink(NATIVE_AUDIO_PATH); return NULL;
}
