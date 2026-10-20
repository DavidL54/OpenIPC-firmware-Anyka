#include "gateway.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <sys/syscall.h>
#include <mbedtls/md5.h>

uint64_t monotonic_ms(void)
{
    /* Same kernel ABI as the validated native RTSP/SDK monitor. Avoid time64
     * syscall probing on the vendor 4.4 kernel. No SDK libc is loaded here. */
    struct { long seconds, nanoseconds; } t;
    if (syscall(SYS_clock_gettime32, CLOCK_MONOTONIC, &t) ||
        t.seconds < 0 || t.nanoseconds < 0 || t.nanoseconds >= 1000000000L) abort();
    return (uint64_t)t.seconds * 1000 + (unsigned long)t.nanoseconds / 1000000;
}
static int wait_fd(int fd, short events, uint64_t deadline)
{
    while (!stopping) {
        uint64_t now = monotonic_ms();
        if (now >= deadline) return -1;
        int timeout = deadline - now > 250 ? 250 : (int)(deadline - now);
        struct pollfd p = {fd, events, 0};
        int n = poll(&p, 1, timeout);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 || p.revents & (POLLERR | POLLNVAL)) return -1;
        if (p.revents & events) return 0;
        if (p.revents & POLLHUP) return -1;
    }
    return -1;
}
int transfer_read(int fd, void *data, size_t size, uint64_t deadline)
{
    unsigned char *p = data;
    while (size) {
        if (wait_fd(fd, POLLIN, deadline)) return -1;
        ssize_t n = recv(fd, p, size, MSG_DONTWAIT);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) return -1;
        p += n; size -= (size_t)n;
    }
    return 0;
}
int transfer_write(int fd, const void *data, size_t size, uint64_t deadline)
{
    const unsigned char *p = data;
    while (size) {
        if (wait_fd(fd, POLLOUT, deadline)) return -1;
        ssize_t n = send(fd, p, size, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) return -1;
        p += n; size -= (size_t)n;
    }
    return 0;
}
int receive_header(int fd, char *buffer, size_t capacity, uint64_t deadline)
{
    size_t have = 0;
    while (have + 1 < capacity) {
        if (wait_fd(fd, POLLIN, deadline)) return -1;
        ssize_t n = recv(fd, buffer + have, capacity - have - 1, MSG_PEEK | MSG_DONTWAIT);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) return -1;
        buffer[have + n] = 0;
        char *end = strstr(buffer, "\r\n\r\n");
        size_t take = end ? (size_t)(end + 4 - buffer) - have : (size_t)n;
        if (memchr(buffer + have, 0, take)) return -1;
        if (transfer_read(fd, buffer + have, take, deadline)) return -1;
        have += take; buffer[have] = 0;
        if (end) return (int)have;
    }
    return -1;
}
const char *header(const struct request *r, const char *name)
{
    for (unsigned i = 0; i < r->fields_count; i++)
        if (!strcasecmp(r->fields[i].name, name)) return r->fields[i].value;
    return "";
}
static int token_char(unsigned char c)
{
    return isalnum(c) || strchr("!#$%&'*+-.^_`|~", c) != NULL;
}
int parse_request(struct request *r, int native)
{
    r->native = native; r->fields_count = 0; r->content_length = 0;
    char *end = strstr(r->raw, "\r\n");
    if (!end) return -1;
    *end = 0;
    char *space = strchr(r->raw, ' ');
    if (!space || space == r->raw || space - r->raw > 15) return -1;
    *space++ = 0; r->method = r->raw;
    for (char *p = r->method; *p; p++) if (*p < 'A' || *p > 'Z') return -1;
    char *protocol = strchr(space, ' ');
    if (!protocol || protocol == space || (size_t)(protocol - space) >= sizeof(r->target)) return -1;
    *protocol++ = 0; r->protocol = protocol;
    if (strcmp(protocol, native ? "RTSP/1.0" : "HTTP/1.1") && (native || strcmp(protocol, "HTTP/1.0"))) return -1;
    strcpy(r->target, space);
    const char *path = space;
    if (native) {
        if (strncmp(path, "rtsp://", 7)) return -1;
        path = strchr(path + 7, '/'); if (!path) return -1;
    }
    if (*path != '/' || strlen(path) >= sizeof(r->path) || strstr(path, "//")) return -1;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p < 33 || *p > 126 || *p == '\\' || *p == '#') return -1;
    strcpy(r->path, path);
    r->query = strchr(r->path, '?');
    if (r->query) *r->query++ = 0; else r->query = r->path + strlen(r->path);
    if (strchr(r->path, '%') || strstr(r->path, "/../") || strstr(r->path, "/./") ||
        !strcmp(r->path, "/..") || !strcmp(r->path, "/.") ||
        (strlen(r->path) >= 3 && !strcmp(r->path + strlen(r->path) - 3, "/.."))) return -1;
    char *line = end + 2;
    while (*line && strcmp(line, "\r\n")) {
        end = strstr(line, "\r\n");
        if (!end || r->fields_count == 48) return -1;
        *end = 0;
        char *colon = strchr(line, ':');
        if (!colon || colon == line) return -1;
        for (char *p = line; p != colon; p++) if (!token_char((unsigned char)*p)) return -1;
        *colon++ = 0;
        while (*colon == ' ' || *colon == '\t') colon++;
        char *tail = colon + strlen(colon);
        while (tail > colon && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail = 0;
        for (char *p = colon; *p; p++) if ((unsigned char)*p < 32 && *p != '\t') return -1;
        for (unsigned i = 0; i < r->fields_count; i++)
            if (!strcasecmp(line, r->fields[i].name)) return -1;
        r->fields[r->fields_count++] = (struct header){line, colon};
        line = end + 2;
    }
    if (strcmp(line, "\r\n") || *header(r, "Transfer-Encoding") || *header(r, "Expect")) return -1;
    const char *length = header(r, "Content-Length");
    if (*length) {
        long value;
        if (decimal(length, 0, 480000, &value) || (native && value)) return -1;
        r->content_length = (size_t)value;
    }
    if (native) {
        const char *seq = header(r, "CSeq");
        if (!*seq || strlen(seq) > 10) return -1;
        for (; *seq; seq++) if (!isdigit((unsigned char)*seq)) return -1;
    } else if (!*header(r, "Host")) return -1;
    return 0;
}
int connect_unix(const char *name, unsigned timeout_ms)
{
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (strlen(name) >= sizeof(addr.sun_path)) return -1;
    strcpy(addr.sun_path, name);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) && errno != EINPROGRESS) { close(fd); return -1; }
    int error = 0; socklen_t size = sizeof(error);
    if (wait_fd(fd, POLLOUT, monotonic_ms() + timeout_ms) || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
        close(fd); return -1;
    }
    return fd;
}
int random_bytes(void *data, size_t size)
{
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    unsigned char *p = data;
    while (size) {
        ssize_t n = read(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return -1; }
        p += n; size -= (size_t)n;
    }
    close(fd); return 0;
}
int random_hex(char *out, size_t bytes)
{
    unsigned char data[32]; const char *hex = "0123456789abcdef";
    if (bytes > sizeof(data) || random_bytes(data, bytes)) return -1;
    for (size_t i = 0; i < bytes; i++) { out[2*i] = hex[data[i] >> 4]; out[2*i+1] = hex[data[i] & 15]; }
    out[2*bytes] = 0; return 0;
}
int equal_secret(const char *a, const char *b)
{
    size_t x = strlen(a), y = strlen(b); unsigned diff = x != y;
    for (size_t i = 0; i < x; i++) diff |= (unsigned char)a[i] ^ (i < y ? (unsigned char)b[i] : 0);
    return diff == 0;
}
int md5_hex(const char *data, char out[33])
{
    unsigned char digest[16]; const char *hex = "0123456789abcdef";
    if (mbedtls_md5((const unsigned char *)data, strlen(data), digest)) return -1;
    for (unsigned i = 0; i < 16; i++) { out[2*i] = hex[digest[i] >> 4]; out[2*i+1] = hex[digest[i] & 15]; }
    out[32] = 0; return 0;
}
static int hexval(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
int decode_component(const char *source, size_t length, char *out, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)source[i];
        if (c == '%') {
            if (i + 2 >= length || hexval(source[i+1]) < 0 || hexval(source[i+2]) < 0) return -1;
            c = (unsigned char)((hexval(source[i+1]) << 4) | hexval(source[i+2])); i += 2;
        } else if (c == '+') c = ' ';
        if (!c || c < 32 || c == 127 || used + 1 >= capacity) return -1;
        out[used++] = (char)c;
    }
    out[used] = 0; return 0;
}
int form_value(const char *form, const char *name, char *out, size_t capacity)
{
    int found = 0; out[0] = 0;
    for (const char *p = form; *p;) {
        const char *end = strchr(p, '&'); if (!end) end = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end - p));
        if (!eq) return -1;
        char key[64];
        if (decode_component(p, (size_t)(eq-p), key, sizeof(key))) return -1;
        if (!strcmp(key, name)) {
            if (found++ || decode_component(eq+1, (size_t)(end-eq-1), out, capacity)) return -1;
        }
        p = *end ? end + 1 : end;
    }
    return found ? 0 : -1;
}
int decimal(const char *text, long low, long high, long *out)
{
    if (!text || !*text) return -1;
    char *end; errno = 0; long n = strtol(text, &end, 10); char canonical[32];
    snprintf(canonical, sizeof(canonical), "%ld", n);
    if (errno || *end || strcmp(text, canonical) || n < low || n > high) return -1;
    *out = n; return 0;
}
ssize_t read_small(const char *path, char *data, size_t limit, int fresh)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return -1;
    struct stat st; time_t now = time(NULL);
    if (fstat(fd, &st)) { close(fd); return -1; }
    if (!S_ISREG(st.st_mode) || st.st_size > (off_t)limit ||
        (fresh && (st.st_mtime > now || now - st.st_mtime > 10))) { close(fd); errno=EINVAL; return -1; }
    size_t used = 0;
    while (used <= limit) {
        ssize_t n = read(fd, data + used, limit + 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 || (size_t)n + used > limit) { close(fd); return -1; }
        if (!n) { data[used] = 0; close(fd); return (ssize_t)used; }
        used += (size_t)n;
    }
    close(fd); return -1;
}
cJSON *json_parse(const char *text)
{
    /* cJSON's legacy error pointer is global even with return_parse_end.
     * Serialize parsing; every returned tree remains owned by its worker. */
    static pthread_mutex_t parse_lock=PTHREAD_MUTEX_INITIALIZER;
    const char *end = NULL;
    pthread_mutex_lock(&parse_lock);
    cJSON *json=cJSON_ParseWithOpts(text, &end, 1);
    pthread_mutex_unlock(&parse_lock);
    return json;
}
int json_unique_object(const cJSON *object)
{
    if (!cJSON_IsObject(object)) return 0;
    for (const cJSON *a = object->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) return 0;
    return 1;
}
