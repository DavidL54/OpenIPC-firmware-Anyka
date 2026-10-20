#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "ak_common.h"
#include "ak_vi.h"
#include "ak_thread.h"
#include "ak_vpss.h"
#include "ak_venc.h"
#include "anyka-osd-clock.h"

static volatile int monitor_running;
static int monitor_started;
static ak_pthread_t monitor;
static const enum vpss_effect_type image_types[] = {
	VPSS_EFFECT_BRIGHTNESS, VPSS_EFFECT_CONTRAST, VPSS_EFFECT_SATURATION,
	VPSS_EFFECT_SHARP, VPSS_EFFECT_HUE
};
static const char *image_names[] = {"brightness", "contrast", "saturation", "sharpness", "hue"};
static int image_overrides[5];
static int image_changed[5];
#include "anyka-contrast.h"
static int orientation_changed;
static int vertical_override;
static int horizontal_override;
static int motion_publish(FILE *);
static int advanced_publish(FILE *);
static int person_publish(FILE *);
static int person_set(int);
static int advanced_set(const char *, const char *);
static void advanced_start(void);
static void advanced_restore(void);

static void image_reply(unsigned long token, int success)
{
	FILE *output = fopen("/tmp/anyka-image-response.tmp", "w");
	if (!output)
		return;
	int failed = fprintf(output, "%lu %s\n", token, success ? "ok" : "error") < 0;
	if (fclose(output))
		failed = 1;
	if (!failed)
		rename("/tmp/anyka-image-response.tmp", "/tmp/anyka-image-response");
}

static void publish_image_settings(void)
{
	FILE *output = fopen("/tmp/anyka-image.json.tmp", "w");
	if (!output)
		return;
	int failed = fputs("{\"ok\":true", output) == EOF;
	for (int index = 0; index < 5; index++) {
		int value = 0;
		if (index == 1 && image_changed[index])
			failed |= fprintf(output, ",\"%s\":%d", image_names[index], image_overrides[index]) < 0;
		else if (!ak_vpss_effect_get(0, image_types[index], &value) && value >= -50 && value <= 50)
			failed |= fprintf(output, ",\"%s\":%d", image_names[index], value) < 0;
		else
			failed |= fprintf(output, ",\"%s\":null", image_names[index]) < 0;
	}
	int vertical = 0, horizontal = 0;
	if (!ak_vpss_get_flip_mirror(0, &vertical, &horizontal) &&
		(vertical == 0 || vertical == 1) && (horizontal == 0 || horizontal == 1))
		failed |= fprintf(output, ",\"flip_vertical\":%s,\"flip_horizontal\":%s",
			vertical ? "true" : "false", horizontal ? "true" : "false") < 0;
	else
		failed |= fputs(",\"flip_vertical\":null,\"flip_horizontal\":null", output) == EOF;
	failed |= motion_publish(output);
	failed |= advanced_publish(output);
	failed |= person_publish(output);
	failed |= clock_publish(output);
	failed |= fputs("}\n", output) == EOF;
	if (fclose(output))
		failed = 1;
	if (!failed)
		rename("/tmp/anyka-image.json.tmp", "/tmp/anyka-image.json");
}

static unsigned long monotonic_seconds(void)
{
	struct { long seconds; long nanoseconds; } kernel_time;
	if (syscall(SYS_clock_gettime32, CLOCK_MONOTONIC, &kernel_time))
		return 0;
	return (unsigned long)kernel_time.seconds;
}

#include "anyka-motion.h"
#include "anyka-person.h"
#include "anyka-sdk-controls.h"

static int capture_jpeg(const struct video_input_frame *frame)
{
	FILE *memory = fopen("/proc/meminfo", "r");
	unsigned int available = 0;
	char line[128];
	if (memory) {
		while (fgets(line, sizeof(line), memory))
			if (sscanf(line, "MemAvailable: %u kB", &available) == 1)
				break;
		fclose(memory);
	}
	VI_CHN_ATTR_EX attributes = {0};
	int attributes_result = ak_vi_get_chn_attr_ex(1, &attributes);
	fprintf(stderr, "snapshot: memory=%u attr=%d size=%dx%d type=%d mode=%d frame=%u\n",
		available, attributes_result, attributes.res.width, attributes.res.height,
		attributes.data_type, attributes.mode, frame->vi_frame.len);
	if (available < 6144 || attributes_result ||
		attributes.res.width < 16 || attributes.res.width > 640 ||
		attributes.res.height < 16 || attributes.res.height > 480 ||
		!frame->vi_frame.data || attributes.data_type != VI_DATA_TYPE_YUV420SP ||
		attributes.mode != FRAME_MODE)
		return -1;
	unsigned int expected = attributes.res.width * attributes.res.height * 3 / 2;
	if (frame->vi_frame.len < expected)
		return -1;
	struct venc_param parameters = {0};
	parameters.width = attributes.res.width;
	parameters.height = attributes.res.height;
	parameters.fps = 20;
	parameters.goplen = 40;
	parameters.target_kbps = 400;
	parameters.max_kbps = 400;
	parameters.profile = PROFILE_JPEG;
	parameters.chroma_mode = CHROMA_4_2_0;
	parameters.minqp = 20;
	parameters.maxqp = 48;
	parameters.initqp = 50;
	parameters.enc_level = HEVC_AVC_LEVEL_3;
	parameters.jpeg_qlevel = JPEG_QLEVEL_LOW;
	parameters.enc_out_type = MJPEG_ENC_TYPE;
	int encoder = -1;
	int opened = ak_venc_open(&parameters, &encoder);
	if (opened) {
		fprintf(stderr, "snapshot: encoder open failed=%d\n", opened);
		return -1;
	}
	struct video_stream stream = {0};
	int result = -1;
	int encoded = ak_venc_encode_frame(encoder, frame->vi_frame.data, expected, frame->mdinfo, &stream);
	fprintf(stderr, "snapshot: encode=%d bytes=%u\n", encoded, stream.len);
	if (!encoded) {
		if (stream.data && stream.len >= 4 && stream.len <= 512 * 1024 &&
			stream.data[0] == 0xff && stream.data[1] == 0xd8 &&
			stream.data[stream.len - 2] == 0xff && stream.data[stream.len - 1] == 0xd9) {
			FILE *output = fopen("/tmp/anyka-snapshot.jpg.tmp", "wb");
			if (output) {
				int failed = fwrite(stream.data, 1, stream.len, output) != stream.len;
				if (fclose(output))
					failed = 1;
				if (!failed)
					result = rename("/tmp/anyka-snapshot.jpg.tmp", "/tmp/anyka-snapshot.jpg");
			}
		}
		ak_venc_release_stream(encoder, &stream);
	}
	ak_venc_close(encoder);
	unlink("/tmp/anyka-snapshot.jpg.tmp");
	return result;
}

static void image_command(const struct video_input_frame *frame)
{
	if (access("/tmp/anyka-image-request", F_OK))
		return;
	int lock = open("/tmp/anyka-image-sdk.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (lock < 0)
		return;
	if (flock(lock, LOCK_EX | LOCK_NB)) {
		close(lock);
		return;
	}
	FILE *input = fopen("/tmp/anyka-image-request", "r");
	unsigned long token = 0, deadline = 0;
	char operation[32] = "", extra = 0;
	int value = 0;
	char text[64] = "";
	int fields = input ? fscanf(input, "%lu %31s %63s %lu %c", &token, operation, text, &deadline, &extra) : 0;
	if (input)
		fclose(input);
	unsigned long now = monotonic_seconds();
	if (fields != 4 || !token || deadline < now || deadline > now + 8) {
		unlink("/tmp/anyka-image-request");
		image_reply(token, 0);
	} else if (!strcmp(operation, "snapshot")) {
		if (frame) {
			unlink("/tmp/anyka-image-request");
			image_reply(token, !capture_jpeg(frame));
		}
	} else if (!frame) {
		unlink("/tmp/anyka-image-request");
		int success = 0;
		success = advanced_set(operation, text);
		int scalar = parse_values(text, &value, 1);
		if (scalar && !strncmp(operation, "motion_", 7)) success = motion_set(operation, value) || success;
		if (scalar && !strcmp(operation, "osd_clock") && (value == 0 || value == 1)) {
			success = clock_set_enabled(value);
		}
		if (scalar && (!strcmp(operation, "flip_vertical") || !strcmp(operation, "flip_horizontal")) &&
			(value == 0 || value == 1)) {
			int vertical = 0, horizontal = 0;
			if (!ak_vpss_get_flip_mirror(0, &vertical, &horizontal) &&
				(vertical == 0 || vertical == 1) && (horizontal == 0 || horizontal == 1)) {
				if (!strcmp(operation, "flip_vertical"))
					vertical = value;
				else
					horizontal = value;
				success = !ak_vpss_switch_flip_mirror(0, vertical, horizontal);
				if (success) {
					orientation_changed = 1;
					vertical_override = vertical;
					horizontal_override = horizontal;
				}
			}
		}
		for (int index = 0; index < 5; index++) {
			if (scalar && !strcmp(operation, image_names[index]) && value >= -50 && value <= 50) {
				success = set_image_effect(index, value);
				if (success) {
					image_overrides[index] = value;
					image_changed[index] = 1;
				}
				break;
			}
		}
		if (success)
			unlink("/tmp/anyka-snapshot.jpg");
		publish_image_settings();
		image_reply(token, success);
	}
	close(lock);
}

static void read_mode(char mode[16])
{
	strcpy(mode, "auto");
	FILE *input = fopen("/tmp/anyka-daynight-mode", "r");
	if (!input)
		return;
	if (fgets(mode, 16, input)) {
		mode[strcspn(mode, "\r\n")] = 0;
		if (strcmp(mode, "auto") && strcmp(mode, "day") &&
			strcmp(mode, "night") && strcmp(mode, "manual"))
			strcpy(mode, "manual");
	}
	fclose(input);
}

static int gpio_command(const char *action)
{
	const char *helper = getenv("ANYKA_GPIO_HELPER");
	if (!helper)
		return -1;
	pid_t child = fork();
	if (child < 0)
		return -1;
	if (!child) {
		char *arguments[] = {"/media/lib/ld-musl-arm.so.1", "--library-path",
			"/media/lib:/media/usr/lib", (char *)helper, (char *)action, NULL};
		char *environment[] = {"PATH=/bin:/usr/bin", NULL};
		int output = open("/dev/null", O_WRONLY);
		if (output >= 0) {
			dup2(output, STDOUT_FILENO);
			close(output);
		}
		execve(arguments[0], arguments, environment);
		_exit(127);
	}
	int outcome;
	pid_t waited;
	do { waited = waitpid(child, &outcome, 0); } while (waited < 0 && errno == EINTR);
	return waited == child && WIFEXITED(outcome) && WEXITSTATUS(outcome) == 0 ? 0 : -1;
}

static int switch_mode(int night)
{
	motion_suppress(5);
	if (night) {
		if (gpio_command("auto-ir-on") || ak_vi_switch_mode(0, VI_MODE_NIGHTTIME) ||
			gpio_command("ircut-night"))
			return -1;
	} else {
		if (gpio_command("ircut-day") || ak_vi_switch_mode(0, VI_MODE_DAY_OUTDOOR) ||
			gpio_command("auto-ir-off"))
			return -1;
	}
	fprintf(stderr, "daynight: switched to %s\n", night ? "night" : "day");
	contrast_profile_saved = 0;
	for (int index = 0; index < 5; index++)
		if (image_changed[index])
			set_image_effect(index, image_overrides[index]);
	if (orientation_changed)
		ak_vpss_switch_flip_mirror(0, vertical_override, horizontal_override);
	advanced_restore();
	return 0;
}

static unsigned long daynight_poll_ms;

static void publish_status(const char *mode, int state, int lumi, int stable,
	const char *error, unsigned int switches, int detected, unsigned long cooldown)
{
	FILE *output = fopen("/tmp/anyka-daynight.json.tmp", "w");
	if (!output)
		return;
	const char *state_name = state < 0 ? "unknown" : state ? "night" : "day";
	int result = fprintf(output,
		"{\"ok\":%s,\"mode\":\"%s\",\"state\":\"%s\",\"lumi\":%d,"
		"\"stable\":%s,\"switches\":%u,\"error\":\"%s\","
		"\"detected\":%d,\"cooldown\":%lu,\"poll_ms\":%lu}\n",
		error ? "false" : "true", mode, state_name, lumi,
		stable == 1 ? "true" : "false", switches, error ? error : "",
		detected, cooldown, daynight_poll_ms) < 0;
	if (fclose(output))
		result = 1;
	if (!result)
		rename("/tmp/anyka-daynight.json.tmp", "/tmp/anyka-daynight.json");
}

static void *monitor_light(void *unused)
{
	(void)unused;
	clock_start();
	motion_start();
	advanced_start();
	const char *enabled = getenv("ANYKA_DAYNIGHT_CONTROL");
	int control = enabled && !strcmp(enabled, "1");
	struct ak_auto_day_night_threshold threshold = {0};
	threshold.day_to_night_lum = 6400;
	threshold.night_to_day_lum = 2048;
	threshold.lock_time = 15000;
	threshold.day2night_sleep_time = 500;
	threshold.night2day_sleep_time = 2000;
	for (int index = 0; index < NIGHT_ARRAY_NUM; index++)
		threshold.night_cnt[index] = 1200;
	for (int index = 0; index < DAY_ARRAY_NUM; index++)
		threshold.day_cnt[index] = 50000;
	int thresholds_ok = !control || !ak_vpss_set_auto_day_night_param(0, &threshold);
	int lock = open("/tmp/anyka-daynight.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (lock < 0) {
		publish_status("auto", -1, -1, 0, "Controller lock unavailable", 0, -1, 0);
		clock_stop();
		return NULL;
	}
	int state = -1;
	int failed = 0;
	unsigned int switches = 0;
	unsigned long last_switch = 0;
	char previous_mode[16] = "";
	while (monitor_running) {
		image_command(NULL);
		publish_image_settings();
		int lumi = -1;
		int stable = -1;
		int measured = !ak_vpss_get_cur_lumi(0, &lumi);
		ak_vpss_check_ae_stable(0, &stable);
		if (flock(lock, LOCK_EX | LOCK_NB)) {
			motion_wait();
			continue;
		}
		char mode[16];
		read_mode(mode);
		if (strcmp(previous_mode, mode)) {
			failed = 0;
			strcpy(previous_mode, mode);
		}
		int desired = state;
		int detected = -1;
		unsigned long elapsed = monotonic_seconds() - last_switch;
		const char *error = NULL;
		if (!measured)
			error = "ISP light measurement unavailable";
		if (control && !failed && strcmp(mode, "manual")) {
			if (!strcmp(mode, "day"))
				desired = 0;
			else if (!strcmp(mode, "night"))
				desired = 1;
			else if (!thresholds_ok)
				error = "SDK thresholds unavailable";
			else if (state < 0)
				desired = 0;
			else if (measured && elapsed >= 15) {
				detected = state;
				unsigned long poll_started = clock_milliseconds();
				int poll_result = ak_vpss_get_auto_day_night_level(0, state, &detected);
				daynight_poll_ms = clock_milliseconds() - poll_started;
				if (poll_result) {
					error = "SDK day/night detection unavailable";
				} else if (detected == 0 || detected == 1) {
					desired = detected;
				}
			}
			if (desired != state) {
				if (switch_mode(desired)) {
					failed = 1;
					state = -1;
				} else {
					state = desired;
					if (motion_scene(state)) motion_error = "SDK motion day/night mode failed";
					switches++;
					last_switch = monotonic_seconds();
					elapsed = 0;
				}
			}
		}
		if (failed)
			error = "GPIO or ISP transition failed; automatic switching paused";
		publish_status(mode, state, measured ? lumi : -1, stable, error, switches,
			detected, elapsed < 15 ? 15 - elapsed : 0);
		flock(lock, LOCK_UN);
		motion_wait();
	}
	close(lock);
	clock_stop();
	motion_stop();
	person_set(0);
	if (tracking_handle) ak_mt_destroy(tracking_handle);
	return NULL;
}

int ak_vi_enable_chn(int channel)
{
	static int (*enable_channel)(int);
	if (!enable_channel)
		enable_channel = dlsym(RTLD_NEXT, "ak_vi_enable_chn");
	if (!enable_channel)
		return -1;
	int result = enable_channel(channel);
	if (!result && channel == 1 && !monitor_started) {
		monitor_running = 1;
		monitor_started = !ak_thread_create(&monitor, monitor_light, NULL, 128 * 1024, -1);
		fprintf(stderr, "daynight: monitor started=%d\n", monitor_started);
	}
	return result;
}

int ak_vi_release_frame(int channel, struct video_input_frame *frame)
{
	static int (*release_frame)(int, struct video_input_frame *);
	if (!release_frame)
		release_frame = dlsym(RTLD_NEXT, "ak_vi_release_frame");
	if (!release_frame)
		return -1;
	if (channel == 1 && frame && monitor_started)
		{ image_command(frame); person_frame(frame); }
	return release_frame(channel, frame);
}

int ak_vi_close(int device)
{
	static int (*close_device)(int);
	if (!close_device)
		close_device = dlsym(RTLD_NEXT, "ak_vi_close");
	if (device == 0 && monitor_started) {
		monitor_running = 0;
		ak_thread_join(monitor);
		monitor_started = 0;
		unlink("/tmp/anyka-daynight.json");
		unlink("/tmp/anyka-image.json");
		unlink("/tmp/anyka-image-request");
	}
	return close_device ? close_device(device) : -1;
}
