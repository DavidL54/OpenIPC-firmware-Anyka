#include "gateway.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <crypt.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int hex_string(const char *s, size_t size)
{
    if (strlen(s) != size) return 0;
    for (; *s; s++) if (!(*s >= '0' && *s <= '9') && !(*s >= 'a' && *s <= 'f')) return 0;
    return 1;
}
static int valid_hash(const char *hash)
{
    if (strlen(hash) != 60 || strncmp(hash, "$2", 2) || !strchr("aby", hash[2]) ||
        hash[3] != '$' || hash[6] != '$' || !isdigit((unsigned char)hash[4]) || !isdigit((unsigned char)hash[5])) return 0;
    int cost = 10 * (hash[4]-'0') + hash[5]-'0';
    if (cost < 8 || cost > 12) return 0;
    for (unsigned i = 7; i < 60; i++) if (!isalnum((unsigned char)hash[i]) && hash[i] != '.' && hash[i] != '/') return 0;
    return 1;
}
int load_access(void)
{
    char data[1026];
    ssize_t n = read_small(gateway.access_file, data, 1024, 0);
    if (n < 0) return errno == ENOENT ? 0 : -1;
    cJSON *json = json_parse(data);
    const cJSON *hash = cJSON_GetObjectItemCaseSensitive(json, "password_hash");
    const cJSON *ha1 = cJSON_GetObjectItemCaseSensitive(json, "digest_ha1");
    int ok = json_unique_object(json) && cJSON_IsString(hash) && valid_hash(hash->valuestring) &&
        (!ha1 || (cJSON_IsString(ha1) && (!*ha1->valuestring || hex_string(ha1->valuestring, 32))));
    if (ok) {
        strcpy(gateway.password_hash, hash->valuestring);
        if (ha1) strcpy(gateway.ha1, ha1->valuestring);
    }
    cJSON_Delete(json); memset(data, 0, sizeof(data)); return ok ? 0 : -1;
}
/* Serialized durable replacement, and a read-only remount on every write path.
 * Only the gateway owns credentials. Neither the SDK nor command arguments see them. */
int private_write(const char *path, const char *data)
{
    char temporary[528], parent[512]; size_t length = strlen(data);
    if (strlen(path) >= sizeof(parent) || length > 8192 ||
        snprintf(temporary,sizeof(temporary),"%s.new",path) >= (int)sizeof(temporary)) return -1;
    strcpy(parent,path);char *slash=strrchr(parent,'/');if(!slash) return -1;*slash=0;
    pthread_mutex_lock(&gateway.persist_lock);
    int lock=open("/tmp/anyka-persist.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    int remount=!strncmp(path,"/mnt/",5),result=-1,fd=-1;
    if(lock<0 || flock(lock,LOCK_EX)) goto done;
    if(remount && mount(NULL,"/mnt",NULL,MS_REMOUNT,NULL)) goto done;
    struct stat st;
    if(!lstat(path,&st) && !S_ISREG(st.st_mode)) goto cleanup;
    fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0) goto cleanup;
    size_t used=0;
    while(used<length) {
        ssize_t n=write(fd,data+used,length-used);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) goto close_file;
        used+=(size_t)n;
    }
    if(!fsync(fd) && !rename(temporary,path)) {
        int directory=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(directory>=0) {result=fsync(directory);close(directory);}
    }
close_file:
    close(fd);unlink(temporary);
cleanup:
    if(remount) {sync();if(mount(NULL,"/mnt",NULL,MS_REMOUNT|MS_RDONLY,NULL)) result=-1;}
done:
    if(lock>=0) close(lock);
    pthread_mutex_unlock(&gateway.persist_lock);return result;
}
int private_save(const char *hash,const char *ha1)
{
    char data[256];int n=snprintf(data,sizeof(data),"{\"password_hash\":\"%s\",\"digest_ha1\":\"%s\"}\n",hash,ha1);
    int result=n>0 && (size_t)n<sizeof(data)?private_write(gateway.access_file,data):-1;
    memset(data,0,sizeof(data));return result;
}
int session_token(const struct request *r, char out[65])
{
    const char *p = header(r, "Cookie"); int found = 0;
    while (*p) {
        while (*p == ' ' || *p == ';') p++;
        const char *end = strchr(p, ';'); if (!end) end = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end-p));
        if (eq && (size_t)(eq-p) == 13 && !memcmp(p, "anyka_session", 13)) {
            if (found++ || end-eq-1 != 64) return -1;
            memcpy(out, eq+1, 64); out[64] = 0;
            if (!hex_string(out, 64)) return -1;
        }
        p = end;
    }
    return found ? 0 : -1;
}
int session_valid(const char *token)
{
    int ok = 0; uint64_t now = monotonic_ms();
    pthread_mutex_lock(&gateway.lock);
    for (unsigned i = 0; i < SESSION_COUNT; i++)
        if (gateway.sessions[i].expires > now && equal_secret(gateway.sessions[i].token, token)) ok = 1;
    pthread_mutex_unlock(&gateway.lock); return ok;
}
int session_authorized(const struct request *r)
{
    char token[65]; return !session_token(r, token) && session_valid(token);
}
void cancel_audio_locked(void)
{
    gateway.audio_generation++;
    if (gateway.audio_fd >= 0) shutdown(gateway.audio_fd, SHUT_RDWR);
    if (gateway.audio_client >= 0) shutdown(gateway.audio_client, SHUT_RDWR);
}
void revoke_media_locked(void)
{
    gateway.generation++;
    memset(gateway.nonces, 0, sizeof(gateway.nonces));
    for (unsigned i = HTTP_SLOTS; i < HTTP_SLOTS + AUTH_SLOTS; i++)
        if (gateway.workers[i].active) shutdown(gateway.workers[i].fd, SHUT_RDWR);
    cancel_audio_locked();
    cancel_preview_locked();
}
static int blocked_locked(void)
{
    uint64_t now = monotonic_ms();
    if (now - gateway.attempt_window >= 60000) { gateway.attempt_window = now; gateway.attempts = 0; }
    return gateway.attempts >= 5;
}
struct digest_fields {
    char username[64], realm[64], nonce[64], opaque[64], uri[TARGET_LIMIT];
    char response[64], algorithm[16], qop[16], nc[16], cnonce[129];
};
static int digest_fields(const char *p, struct digest_fields *d)
{
    struct { const char *name; char *value; size_t size; } map[] = {
        {"username", d->username, sizeof(d->username)}, {"realm",d->realm,sizeof(d->realm)},
        {"nonce",d->nonce,sizeof(d->nonce)}, {"opaque",d->opaque,sizeof(d->opaque)},
        {"uri",d->uri,sizeof(d->uri)}, {"response",d->response,sizeof(d->response)},
        {"algorithm",d->algorithm,sizeof(d->algorithm)}, {"qop",d->qop,sizeof(d->qop)},
        {"nc",d->nc,sizeof(d->nc)}, {"cnonce",d->cnonce,sizeof(d->cnonce)}
    };
    if (strncmp(p, "Digest ", 7)) return -1;
    p += 7; unsigned seen = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        const char *name = p;
        while (isalnum((unsigned char)*p) || *p == '_') p++;
        size_t name_size = (size_t)(p-name);
        while (*p == ' ') p++;
        if (!name_size || *p++ != '=') return -1;
        while (*p == ' ') p++;
        int quoted = *p == '"'; if (quoted) p++;
        const char *value = p;
        while (*p && (quoted ? *p != '"' : *p != ',' && *p != ' ')) {
            if ((unsigned char)*p < 33 || (unsigned char)*p > 126 || *p == '\\') {
                if (!(quoted && *p == ' ')) return -1;
            }
            p++;
        }
        size_t length = (size_t)(p-value);
        if (quoted && *p++ != '"') return -1;
        unsigned i;
        for (i = 0; i < sizeof(map)/sizeof(map[0]); i++) if (strlen(map[i].name) == name_size && !memcmp(name,map[i].name,name_size)) break;
        if (i == sizeof(map)/sizeof(map[0]) || seen & (1u << i) || length >= map[i].size) return -1;
        memcpy(map[i].value,value,length); map[i].value[length] = 0; seen |= 1u << i;
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p++ != ',') return -1;
        if (!*p && p[-1] == ',') return -1;
    }
    return 0;
}
/* Shared HTTP/RTSP Digest state, qop=auth, bounded nonces, strict nonce-count
 * replay rejection. Authentication is checked on every native RTSP request. */
int digest_check(const struct request *r, char *extra, size_t capacity)
{
    struct digest_fields d = {0};
    const char *authorization = header(r,"Authorization");
    int parsed = !digest_fields(authorization, &d), accepted = 0;
    char ha2[33], expected[33], work[TARGET_LIMIT+256];
    pthread_mutex_lock(&gateway.lock);
    if (blocked_locked()) {
        snprintf(extra,capacity,"Retry-After: 60\r\n"); pthread_mutex_unlock(&gateway.lock); return 429;
    }
    uint64_t now = monotonic_ms();
    if (parsed && !strcmp(d.username,"admin") && !strcmp(d.realm,REALM) &&
        !strcmp(d.uri,r->target) && !strcmp(d.qop,"auth") && (!*d.algorithm || !strcmp(d.algorithm,"MD5")) &&
        hex_string(d.response,32) && hex_string(d.nc,8) && *d.cnonce && hex_string(gateway.ha1,32)) {
        uint32_t count = (uint32_t)strtoul(d.nc,NULL,16);
        for (unsigned i = 0; i < NONCE_COUNT; i++) {
            struct nonce *n = &gateway.nonces[i];
            if (n->expires <= now || !equal_secret(n->value,d.nonce) || !equal_secret(n->opaque,d.opaque) ||
                !count || count <= n->count) continue;
            /* Clients such as FFmpeg generate a fresh cnonce per request.
             * The signed response and strictly increasing nonce-count still
             * reject replays for this server nonce. */
            snprintf(work,sizeof(work),"%s:%s",r->method,r->target);
            if (md5_hex(work,ha2)) break;
            snprintf(work,sizeof(work),"%s:%s:%s:%s:auth:%s",gateway.ha1,d.nonce,d.nc,d.cnonce,ha2);
            if (md5_hex(work,expected) || !equal_secret(expected,d.response)) break;
            n->count = count; strcpy(n->cnonce,d.cnonce); accepted = 1;
            snprintf(work,sizeof(work),":%s",r->target); md5_hex(work,ha2);
            snprintf(work,sizeof(work),"%s:%s:%s:%s:auth:%s",gateway.ha1,d.nonce,d.nc,d.cnonce,ha2); md5_hex(work,expected);
            snprintf(extra,capacity,"Authentication-Info: qop=auth, cnonce=\"%s\", nc=%s, rspauth=\"%s\"\r\n",d.cnonce,d.nc,expected);
            break;
        }
    }
    if (!accepted) {
        if (*authorization) gateway.attempts++;
        struct nonce *n = &gateway.nonces[gateway.nonce_next++ % NONCE_COUNT];
        memset(n,0,sizeof(*n));
        if (random_hex(n->value,16) || random_hex(n->opaque,16)) {
            pthread_mutex_unlock(&gateway.lock); return 503;
        }
        n->expires = now + 300000;
        snprintf(extra,capacity,"WWW-Authenticate: Digest realm=\"%s\", nonce=\"%s\", opaque=\"%s\", algorithm=MD5, qop=\"auth\"\r\n",REALM,n->value,n->opaque);
    }
    pthread_mutex_unlock(&gateway.lock);
    memset(expected,0,sizeof(expected)); memset(work,0,sizeof(work));
    return accepted ? 200 : 401;
}
void native_serve(int fd)
{
    struct request r;
    while (!stopping) {
        uint32_t size;
        if (transfer_read(fd,&size,4,monotonic_ms()+120000)) return;
        size = ntohl(size);
        if (!size || size > HEADER_LIMIT || transfer_read(fd,r.raw,size,monotonic_ms()+1500) || memchr(r.raw,0,size)) return;
        r.raw[size] = 0;
        if (parse_request(&r,1)) return;
        char extra[512] = {0}, reply[2048];
        int status = digest_check(&r,extra,sizeof(extra));
        if (status == 200) {
            if (strcmp(r.method,"OPTIONS") && strcmp(r.method,"DESCRIBE") && strcmp(r.method,"SETUP") &&
                strcmp(r.method,"PLAY") && strcmp(r.method,"TEARDOWN")) status = 405;
            else if (*r.query || (strcmp(r.path,"/vs0") && strcmp(r.path,"/vs1") &&
                strcmp(r.path,"/vs0/") && strcmp(r.path,"/vs1/") &&
                strcmp(r.path,"/vs0/trackID=0") && strcmp(r.path,"/vs1/trackID=0") &&
                strcmp(r.path,"/vs0/trackID=1") && strcmp(r.path,"/vs1/trackID=1"))) status = 404;
        }
        int length = status == 200 ? 0 : snprintf(reply,sizeof(reply),
            "RTSP/1.0 %d %s\r\nCSeq: %s\r\n%sContent-Length: 0\r\n\r\n",status,
            status==401?"Unauthorized":status==429?"Too Many Requests":status==404?"Not Found":status==405?"Method Not Allowed":"Service Unavailable",header(&r,"CSeq"),extra);
        if (length < 0 || length >= (int)sizeof(reply)) return;
        size = htonl((uint32_t)length);
        if (transfer_write(fd,&size,4,monotonic_ms()+500) || (length && transfer_write(fd,reply,(size_t)length,monotonic_ms()+500))) return;
    }
}
void access_post(int fd, const struct request *r, const char *peer)
{
    char body[2049], user[64], action[32], password[73], confirmation[73];
    if (!r->content_length || r->content_length > 2048 ||
        strncmp(header(r,"Content-Type"),"application/x-www-form-urlencoded",33) ||
        transfer_read(fd,body,r->content_length,monotonic_ms()+5000)) { http_error(fd,400,"Invalid form"); return; }
    body[r->content_length] = 0;
    if (memchr(body,0,r->content_length) || form_value(body,"ui_username",user,sizeof(user)) || strcmp(user,"admin") ||
        form_value(body,"action",action,sizeof(action)) || (strcmp(action,"password") && strcmp(action,"login")) ||
        form_value(body,"password_default",password,sizeof(password))) { memset(body,0,sizeof(body)); http_error(fd,400,"Invalid login form"); return; }
    int setting = !strcmp(action,"password"), status = 500;
    char extra[256] = {0}, hash[61], ha1[33], digest_input[128], token[65];
    pthread_mutex_lock(&gateway.password_lock);
    pthread_mutex_lock(&gateway.lock);
    int configured = *gateway.password_hash;
    int blocked = blocked_locked();
    strcpy(hash,gateway.password_hash);
    pthread_mutex_unlock(&gateway.lock);
    if (!configured && strcmp(peer,gateway.setup_ip)) { status = 403; goto done; }
    if (setting) {
        if (configured && !session_authorized(r)) { status=401; goto done; }
        if (strlen(password)<12 || form_value(body,"password_confirm",confirmation,sizeof(confirmation)) || !equal_secret(password,confirmation)) {
            status=303; strcpy(extra,"Location: /cgi-bin/access.cgi?error=password\r\n"); goto done;
        }
    } else {
        if (!configured) { status=403; goto done; }
        if (blocked) { status=429; strcpy(extra,"Retry-After: 60\r\n"); goto done; }
    }
    struct crypt_data crypt_state = {0};
    char salt[30];
    if (setting) {
        unsigned char entropy[22]; const char *alphabet="./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        if (random_bytes(entropy,sizeof(entropy))) goto done;
        memcpy(salt,"$2b$08$",7);
        for (unsigned i=0;i<22;i++) salt[7+i]=alphabet[entropy[i]&63];
        salt[28]=alphabet[(entropy[21]&3)<<4]; salt[29]=0;
    }
    char *result = crypt_r(password,setting?salt:hash,&crypt_state);
    if (!result || !valid_hash(result) || (!setting && !equal_secret(result,hash))) {
        pthread_mutex_lock(&gateway.lock); gateway.attempts++; pthread_mutex_unlock(&gateway.lock);
        status=303; strcpy(extra,"Location: /cgi-bin/access.cgi?error=login\r\n"); memset(&crypt_state,0,sizeof(crypt_state)); goto done;
    }
    if (setting) strcpy(hash,result);
    memset(&crypt_state,0,sizeof(crypt_state));
    snprintf(digest_input,sizeof(digest_input),"admin:%s:%s",REALM,password);
    if (md5_hex(digest_input,ha1) || random_hex(token,32)) goto done;
    pthread_mutex_lock(&gateway.lock); int changed = setting || !equal_secret(gateway.ha1,ha1); pthread_mutex_unlock(&gateway.lock);
    if (changed && private_save(hash,ha1)) goto done;
    pthread_mutex_lock(&gateway.lock);
    if (changed) {
        strcpy(gateway.password_hash,hash); strcpy(gateway.ha1,ha1); revoke_media_locked();
        if (setting) memset(gateway.sessions,0,sizeof(gateway.sessions));
    }
    uint64_t now=monotonic_ms(); unsigned slot;
    for(slot=0;slot<SESSION_COUNT;slot++) if(gateway.sessions[slot].expires<=now) break;
    if(slot==SESSION_COUNT) { memset(gateway.sessions,0,sizeof(gateway.sessions)); slot=0; }
    strcpy(gateway.sessions[slot].token,token); gateway.sessions[slot].expires=now+28800000;
    pthread_mutex_unlock(&gateway.lock);
    snprintf(extra,sizeof(extra),"Set-Cookie: anyka_session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=28800\r\nLocation: /cgi-bin/dashboard.cgi\r\n",token);
    /* Confirm the first HA login without growing a RAM log on every login. */
    static int session_logged;
    if(!session_logged) {fputs("anyka-gateway: authenticated session issued\n",stderr);session_logged=1;}
    status=303;
done:
    pthread_mutex_unlock(&gateway.password_lock);
    memset(body,0,sizeof(body)); memset(password,0,sizeof(password)); memset(confirmation,0,sizeof(confirmation));
    memset(hash,0,sizeof(hash)); memset(ha1,0,sizeof(ha1)); memset(digest_input,0,sizeof(digest_input));
    const char *reply=status==303?"":"{\"ok\":false,\"error\":\"Authentication unavailable\"}";
    http_reply(fd,status,"application/json",reply,strlen(reply),extra);
}
