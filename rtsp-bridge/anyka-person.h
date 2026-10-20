/* One reusable 320x180 DMA image, copied from the existing SDK substream.
 * Inference never retains a VI frame or runs in the encoder thread. */
#include <pthread.h>
#include "ak_mem.h"
#include "ak_npu.h"
#include "ak_svp2.h"
static int person_enabled, person_ready, person_detected, person_count;
static unsigned int person_frames, person_ms;
static const char *person_error = "";
static ak_pthread_t person_thread;
static int person_started, person_running, person_pending;
static unsigned long person_next, person_until;
static unsigned char *person_pixels;
static unsigned long person_physical;
static pthread_mutex_t person_lock = PTHREAD_MUTEX_INITIALIZER;
static int (*person_npu_init)(void), (*person_npu_close)(void);
static int (*person_create)(int, const AK_SVP2_CHN_ATTR_T *, AK_SVP2_CONF_INFO *);
static int (*person_destroy)(int), (*person_release)(AK_SVP2_OUTPUT_T *);
static int (*person_process)(int, AK_SVP2_IMG_INFO_T *, AK_SVP2_OUTPUT_T *);
static int person_channel, person_npu;
static void *person_sdk_library;
static char person_error_detail[96];
static void person_redact(unsigned char *);

static int person_prepare_driver(void)
{
    const char *helper = getenv("ANYKA_NPU_HELPER");
    if (!helper) return 0;
    pid_t child = fork();
    if (child < 0) return 0;
    if (!child) {
        char *args[] = {"/bin/sh", (char *)helper, NULL};
        char *env[] = {"PATH=/bin:/sbin:/usr/bin:/usr/sbin", NULL};
        execve(args[0], args, env); _exit(127);
    }
    int status; pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static unsigned long person_milliseconds(void)
{
    struct { long seconds; long nanoseconds; } stamp;
    if (syscall(SYS_clock_gettime32, CLOCK_MONOTONIC, &stamp)) return 0;
    return (unsigned long)stamp.seconds*1000UL+stamp.nanoseconds/1000000;
}

static unsigned int sdk_available_memory(void)
{
    FILE *input = fopen("/proc/meminfo", "r");
    unsigned int available = 0; char line[128];
    if (input) { while (fgets(line, sizeof(line), input)) if (sscanf(line, "MemAvailable: %u kB", &available) == 1) break; fclose(input); }
    return available;
}

static void person_cleanup(void)
{
    if (person_channel) person_destroy(0);
    if (person_npu) person_npu_close();
    if (person_pixels) ak_mem_dma_free(person_pixels);
    person_pixels = NULL; person_channel = person_npu = person_ready = person_detected = 0;
}

static int person_initialize(void)
{
    if (sdk_available_memory() < 5120) { person_error = "Insufficient memory for native person detection"; return 0; }
    if (!person_prepare_driver()) { person_error = "Compatible NPU RAM driver unavailable"; return 0; }
    /* Load the mutually dependent NNE/NPU libraries as one ELF dependency
     * group. Their absolute data relocations cannot be deferred with LAZY. */
    if (!person_sdk_library) person_sdk_library = dlopen("anyka-person-sdk.so", RTLD_NOW | RTLD_GLOBAL);
    void *svp = person_sdk_library;
    if (!svp) goto missing;
    person_npu_init = dlsym(svp, "ak_npu_init"); person_npu_close = dlsym(svp, "ak_npu_deinit");
    person_create = dlsym(svp, "ak_svp2_create_chn"); person_destroy = dlsym(svp, "ak_svp2_destroy_chn");
    person_process = dlsym(svp, "ak_svp2_process"); person_release = dlsym(svp, "ak_svp2_release");
    if (!person_npu_init || !person_npu_close || !person_create || !person_destroy || !person_process || !person_release) goto missing;
    if (person_npu_init()) { person_error = "Native NPU initialization failed"; return 0; }
    person_npu = 1;
    struct { int model_num; AK_SVP2_MODEL_ATTR_T model; } attr = {0};
    attr.model_num = 1; attr.model.model_type = AK_SVP2_MODEL_HD_180P;
    attr.model.model_param.param.hd_param.classify_threshold = 700;
    attr.model.model_param.param.hd_param.IoU_threshold = 3;
    attr.model.model_param.param.hd_param.target_type = AK_SVP2_HUMAN_SHAPE;
    char path[512]; const char *dir = getenv("ANYKA_MOTION_DIR");
    if (!dir || snprintf(path, sizeof(path), "%s/models/model_0x0A000000_V1.1.0.bin", dir) >= (int)sizeof(path)) goto failed;
    AK_SVP2_CONF_INFO config = {0}; config.conf_type.conf = path;
    if (person_create(0, (AK_SVP2_CHN_ATTR_T *)&attr, &config)) goto failed;
    person_channel = 1;
    person_pixels = ak_mem_dma_alloc(MODULE_ID_APP, 320*180*3/2);
    if (!person_pixels || ak_mem_dma_vaddr2paddr(person_pixels, &person_physical) || sdk_available_memory() < 4096) goto failed;
    person_ready = 1; return 1;
missing:
    fprintf(stderr, "person: SDK dynamic loader: %s\n", dlerror());
    person_error = "Native SVP2 libraries unavailable"; return 0;
failed:
    person_error = "Native model allocation failed or memory reserve too low"; person_cleanup(); return 0;
}

static void *person_worker(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&person_lock);
    int initialized = person_initialize();
    pthread_mutex_unlock(&person_lock);
    if (!initialized) return NULL;
    for (;;) {
        pthread_mutex_lock(&person_lock);
        if (!person_running) { pthread_mutex_unlock(&person_lock); break; }
        if (privacy_active()) {
            person_pending = 0; person_detected = 0; person_count = 0; person_until = 0;
        } else if (person_pending) {
            AK_SVP2_IMG_INFO_T input = {0}; AK_SVP2_OUTPUT_T result = {0};
            input.img_type = AK_SVP2_IMG_YUV420SP; input.width = 320; input.height = 180;
            input.pos_info.width = 320; input.pos_info.height = 180;
            input.vir_addr = person_pixels; input.phy_addr = person_physical;
            unsigned long before = person_milliseconds();
            int status = person_process(0, &input, &result);
            person_ms = person_milliseconds()-before; person_frames++;
            person_count = 0;
            if (!status) {
                int count = result.result.hd_result.total_num;
                if (count < 0 || count > 64) person_error = "Invalid SDK person result";
                else for (int i = 0; i < count; i++) if (result.result.hd_result.target_boxes[i].label == AK_SVP2_HUMAN_SHAPE) person_count++;
                person_release(&result);
            } else { snprintf(person_error_detail, sizeof(person_error_detail), "Native NPU inference failed (%d)", status); person_error = person_error_detail; }
            if (person_count) person_until = monotonic_seconds()+5;
            person_detected = person_until > monotonic_seconds();
            person_pending = 0;
            if (sdk_available_memory() < 4096 || person_ms > 1500) person_error = "Native inference exceeded resource limit";
        }
        int failed = *person_error != 0;
        pthread_mutex_unlock(&person_lock);
        if (failed) break;
        ak_sleep_ms(100);
    }
    pthread_mutex_lock(&person_lock); person_cleanup(); pthread_mutex_unlock(&person_lock);
    return NULL;
}

static int person_set(int enabled)
{
    if (enabled != 0 && enabled != 1) return 0;
    if (person_started) {
        pthread_mutex_lock(&person_lock); person_running = 0; pthread_mutex_unlock(&person_lock);
        ak_thread_join(person_thread); person_started = 0;
    }
    pthread_mutex_lock(&person_lock);
    person_enabled = enabled; person_error = ""; person_count = 0; person_pending = 0; person_until = 0;
    person_detected = 0;
    if (enabled) {
        person_running = 1;
        person_started = !ak_thread_create(&person_thread, person_worker, NULL, 64*1024, -1);
        if (!person_started) { person_error = "Native inference thread unavailable"; pthread_mutex_unlock(&person_lock); return 0; }
    }
    pthread_mutex_unlock(&person_lock);
    return 1;
}

static void person_frame(const struct video_input_frame *frame)
{
    if (privacy_active()) return;
    unsigned long now = monotonic_seconds();
    if (pthread_mutex_trylock(&person_lock)) return;
    if (!person_enabled || !person_ready || *person_error || now < person_next || !frame->vi_frame.data || frame->vi_frame.len < 640*360*3/2) {
        pthread_mutex_unlock(&person_lock); return;
    }
    if (!person_pending && person_pixels && person_running) {
        const unsigned char *source = frame->vi_frame.data;
        for (int y = 0; y < 180; y++) for (int x = 0; x < 320; x++) person_pixels[y*320+x] = source[y*2*640+x*2];
        for (int y = 0; y < 90; y++) for (int x = 0; x < 160; x++) {
            person_pixels[320*180+y*320+x*2] = source[640*360+y*2*640+x*4];
            person_pixels[320*180+y*320+x*2+1] = source[640*360+y*2*640+x*4+1];
        }
        person_redact(person_pixels);
        person_pending = 1; person_next = now+1;
    }
    pthread_mutex_unlock(&person_lock);
}

static int person_publish(FILE *output)
{
    pthread_mutex_lock(&person_lock);
    int result = fprintf(output, ",\"person_enabled\":%s,\"person_ready\":%s,\"person_detected\":%s,\"person_count\":%d,\"person_frames\":%u,\"person_inference_ms\":%u,\"person_error\":\"%s\"",
        person_enabled ? "true" : "false", person_ready ? "true" : "false", !privacy_active() && person_detected ? "true" : "false", privacy_active() ? 0 : person_count, person_frames, person_ms, person_error) < 0;
    pthread_mutex_unlock(&person_lock); return result;
}
