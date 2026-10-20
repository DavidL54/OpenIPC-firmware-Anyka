#ifndef ANYKA_GATEWAY_H
#define ANYKA_GATEWAY_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include "vendor/cjson/cJSON.h"

#define HEADER_LIMIT 8192
#define TARGET_LIMIT 1024
#define HTTP_SLOTS 6
#define AUTH_SLOTS 2
#define SESSION_COUNT 8
#define NONCE_COUNT 32
#define REALM "IP Camera(AC813)"
#define AUTH_SOCKET "/conf/anyka-private/native-rtsp-auth.sock"
#define AUDIO_SOCKET "/conf/anyka-private/native-audio.sock"
#define PRIVACY_MARKER "/tmp/anyka-privacy-mode"

struct header { char *name, *value; };
struct request {
    char raw[HEADER_LIMIT + 1], target[TARGET_LIMIT], path[TARGET_LIMIT];
    char *method, *protocol, *query;
    struct header fields[48];
    unsigned fields_count;
    size_t content_length;
    int native;
};
struct session { char token[65]; uint64_t expires; };
struct nonce {
    char value[33], opaque[33], cnonce[129];
    uint32_t count;
    uint64_t expires;
};
struct worker { int fd, kind, active; };
struct state {
    pthread_mutex_t lock, password_lock, persist_lock, audio_lock, preview_lock;
    char password_hash[61], ha1[33], access_file[512], assets[512], setup_ip[64];
    struct session sessions[SESSION_COUNT];
    struct nonce nonces[NONCE_COUNT];
    struct worker workers[HTTP_SLOTS + AUTH_SLOTS];
    unsigned nonce_next, attempts;
    uint64_t attempt_window, generation, audio_generation, last_notification;
    int audio_fd, audio_client, preview_fd, preview_client, privacy, web_ui;
};
extern struct state gateway;
extern volatile sig_atomic_t stopping;

uint64_t monotonic_ms(void);
int transfer_read(int fd, void *data, size_t size, uint64_t deadline);
int transfer_write(int fd, const void *data, size_t size, uint64_t deadline);
int receive_header(int fd, char *buffer, size_t capacity, uint64_t deadline);
int parse_request(struct request *r, int native);
const char *header(const struct request *r, const char *name);
int connect_unix(const char *name, unsigned timeout_ms);
int random_bytes(void *data, size_t size);
int random_hex(char *output, size_t bytes);
int equal_secret(const char *a, const char *b);
int md5_hex(const char *data, char output[33]);
int decode_component(const char *source, size_t length, char *out, size_t capacity);
int form_value(const char *form, const char *name, char *out, size_t capacity);
int decimal(const char *text, long low, long high, long *out);
ssize_t read_small(const char *path, char *data, size_t limit, int fresh);
cJSON *json_parse(const char *text);
int json_unique_object(const cJSON *object);
int private_write(const char *path, const char *data);
void ptz_presets_http(int fd, const struct request *r);
int ptz_startup(void);
int private_save(const char *hash, const char *ha1);
int load_access(void);
int session_authorized(const struct request *r);
int session_token(const struct request *r, char token[65]);
int session_valid(const char *token);
void revoke_media_locked(void);
void cancel_audio_locked(void);
int privacy_enabled(void);
int digest_check(const struct request *r, char *extra, size_t size);
void native_serve(int fd);
void http_serve(int fd, const char *peer);
void http_reply(int fd, int status, const char *type, const void *body, size_t size, const char *extra);
void http_error(int fd, int status, const char *message);
int http_file(int fd, const char *name, const char *type, size_t limit, int fresh, const char *extra);
void access_post(int fd, const struct request *r, const char *peer);
void audio_live(int fd, const struct request *r);
void audio_post(int fd, const struct request *r);
int audio_session_alive(const char *token, uint64_t generation, uint64_t deadline);
void camera_metrics(int fd);
int websocket_send(int fd, int opcode, const void *data, size_t size);
int websocket_receive(int fd, unsigned char out[1281], size_t *size, uint64_t deadline);
int websocket_handshake(int fd, const struct request *r);
void preview_live(int fd, const struct request *r);
void cancel_preview_locked(void);
int webui_route(const char *path);
void webui_time(int fd, const struct request *r);
void webui_notification(int fd, const struct request *r);
#endif
