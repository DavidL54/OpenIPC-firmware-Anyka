typedef unsigned int uint32_t;
typedef unsigned long pthread_t;

extern int printf(const char *, ...);
extern int fflush(void *);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern unsigned int sleep(unsigned int);
extern void **gv_plat_api(void);

typedef int (*video_control_fn)(int);
typedef int (*video_frame_fn)(int, void *);

static void dump_words(const char *label, const uint32_t *words, unsigned int count)
{
	unsigned int index;

	printf("frame-probe: %s=%p", label, words);
	for (index = 0; index < count; index++)
		printf(" %08x", words[index]);
	printf("\n");
}

static void *probe_main(void *unused)
{
	void **platform;
	video_control_fn start;
	video_frame_fn get;
	video_frame_fn release;
	video_control_fn stop;
	uint32_t frame[8];
	uint32_t *handle;
	unsigned int channel;
	unsigned int index;
	int result;

	(void)unused;
	sleep(15);
	platform = gv_plat_api();
	start = (video_control_fn)platform[7];
	get = (video_frame_fn)platform[8];
	release = (video_frame_fn)platform[9];
	stop = (video_control_fn)platform[10];

	printf("frame-probe: api start=%p get=%p release=%p stop=%p\n",
	       start, get, release, stop);
	for (channel = 0; channel < 2; channel++) {
		result = start(channel);
		printf("frame-probe: start(%u)=0x%08x\n",
		       channel, (unsigned int)result);
		if (result != 0) {
			printf("frame-probe: channel %u unavailable\n", channel);
			fflush(0);
			continue;
		}

		for (index = 0; index < 8; index++) {
			unsigned int word;

			for (word = 0; word < 8; word++)
				frame[word] = 0xcccccccc;

			result = get(channel, frame);
			printf("frame-probe: get(%u)=%d sample=%u\n",
			       channel, result, index);
			dump_words("descriptor", frame, 8);
			if (result != 0) {
				fflush(0);
				continue;
			}

			handle = (uint32_t *)frame[0];
			if (handle != 0)
				dump_words("handle", handle, 12);

			result = release(channel, frame);
			printf("frame-probe: release(%u)=%d\n", channel, result);
			fflush(0);
		}

		result = stop(channel);
		printf("frame-probe: stop(%u)=%d\n", channel, result);
	}
	fflush(0);
	return 0;
}

__attribute__((constructor)) static void schedule_probe(void)
{
	pthread_t thread;

	pthread_create(&thread, 0, probe_main, 0);
}