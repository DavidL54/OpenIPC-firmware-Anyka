/* The shipped sample leaves audio reader registration commented out in PLAY:
 * all stStreamHandle[].iHandleA are zero. Give each SDK session its own reader
 * using the official circular-buffer API, while retaining the SDK callback
 * and its audio-buffer lock. No ring size/layout or sample memory is modified.
 * Both native and vendor callbacks pass through these wrappers.
 */
#include "ak_circular_buffer.h"

#define SDK_READER_LIMIT 8 /* RTSP_MAX_CLIENT in the supplied sample. */
struct sdk_audio_reader {
	int active, id, closing;
	CIRBUF_HANDLE ring;
};
static struct sdk_audio_reader sdk_audio_readers[SDK_READER_LIMIT];
static CIRBUF_HANDLE sdk_audio_ring;
static pthread_key_t audio_reader_key;
static pthread_once_t audio_reader_once = PTHREAD_ONCE_INIT;
static int (*sdk_reader_new)(CIRBUF_HANDLE, char *);
static int (*sdk_reader_delete)(CIRBUF_HANDLE, unsigned int);
static int (*sdk_ring_get)(CIRBUF_HANDLE, unsigned int, unsigned char **, unsigned int *, unsigned int);

static void resolve_audio_readers(void)
{
	sdk_reader_new = dlsym(RTLD_NEXT, "cirbuf_reader_new");
	sdk_reader_delete = dlsym(RTLD_NEXT, "cirbuf_reader_delete");
	sdk_ring_get = dlsym(RTLD_NEXT, "cirbuf_get_buff_data");
	if (!sdk_reader_new || !sdk_reader_delete || !sdk_ring_get || pthread_key_create(&audio_reader_key, NULL)) _exit(1);
}

int cirbuf_get_buff_data(CIRBUF_HANDLE ring, unsigned int id, unsigned char **data,
			unsigned int *size, unsigned int timeout)
{
	pthread_once(&audio_reader_once, resolve_audio_readers);
	struct sdk_audio_reader *reader = pthread_getspecific(audio_reader_key);
	if (reader && !reader->closing) {
		// Callback context and sdk_audio_ring are protected by callbacks_lock.
		if (sdk_audio_ring && sdk_audio_ring != ring) return -1;
		sdk_audio_ring = ring;
		if (!reader->ring) {
			char name[16];
			snprintf(name, sizeof(name), "SDKaudio%u", (unsigned int)(reader - sdk_audio_readers));
			int allocated = sdk_reader_new(ring, name);
			if (allocated < 0) return -1;
			reader->ring = ring; reader->id = allocated;
		}
		id = (unsigned int)reader->id;
	}
	return sdk_ring_get(ring, id, data, size, timeout);
}

int cirbuf_reader_delete(CIRBUF_HANDLE ring, unsigned int id)
{
	pthread_once(&audio_reader_once, resolve_audio_readers);
	struct sdk_audio_reader *reader = pthread_getspecific(audio_reader_key);
	// The sample's teardown still attempts to delete its stale audio reader 0.
	// Release the actual session reader ourselves after its callback returns.
	if (reader && reader->closing && ring == sdk_audio_ring) return 0;
	return sdk_reader_delete(ring, id);
}

static int serialized_audio(int handle, rtsp_frame_info *frame, void *user)
{
	if (handle < 0 || handle >= SDK_READER_LIMIT) return -1;
	pthread_once(&audio_reader_once, resolve_audio_readers);
	pthread_mutex_lock(&callbacks_lock);
	struct sdk_audio_reader *reader = &sdk_audio_readers[handle];
	int result = -1;
	if (reader->active && !pthread_setspecific(audio_reader_key, reader)) {
		result = audio_callback(handle, frame, user);
		if (pthread_setspecific(audio_reader_key, NULL)) _exit(1);
	}
	pthread_mutex_unlock(&callbacks_lock);
	return result;
}
