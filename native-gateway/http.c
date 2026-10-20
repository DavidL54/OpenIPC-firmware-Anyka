#include "gateway.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <glob.h>
#include <poll.h>

static const char *reason(int status)
{
    switch(status) {
    case 101:return "Switching Protocols"; case 200:return "OK"; case 204:return "No Content";
    case 303:return "See Other"; case 400:return "Bad Request"; case 401:return "Unauthorized";
    case 403:return "Forbidden"; case 404:return "Not Found"; case 405:return "Method Not Allowed";
    case 408:return "Request Timeout"; case 409:return "Conflict"; case 413:return "Content Too Large";
    case 429:return "Too Many Requests"; case 431:return "Request Header Fields Too Large";
    case 500:return "Internal Server Error"; case 503:return "Service Unavailable";
    case 504:return "Gateway Timeout"; default:return "Bad Gateway";
    }
}
static int response_header(int fd,int status,const char *type,size_t length,int known,const char *extra)
{
    char text[2048],size[64]="";
    if(known) snprintf(size,sizeof(size),"Content-Length: %zu\r\n",length);
    int n=snprintf(text,sizeof(text),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\n%sConnection: close\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: SAMEORIGIN\r\n"
        "Referrer-Policy: same-origin\r\n"
        "Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; font-src 'self' https://fonts.gstatic.com; img-src 'self' data:; media-src 'self' blob:; connect-src 'self'; frame-ancestors 'self'; form-action 'self'; base-uri 'self'\r\n%s\r\n",status,reason(status),type,size,extra?extra:"");
    if(n<=0 || n>=(int)sizeof(text)) return -1;
    return transfer_write(fd,text,(size_t)n,monotonic_ms()+2000);
}
void http_reply(int fd,int status,const char *type,const void *body,size_t length,const char *extra)
{
    if(!response_header(fd,status,type,length,1,extra) && length)
        (void)transfer_write(fd,body,length,monotonic_ms()+2000);
}
void http_error(int fd,int status,const char *message)
{
    cJSON *json=cJSON_CreateObject();
    if(!json) return;
    cJSON_AddBoolToObject(json,"ok",0); cJSON_AddStringToObject(json,"error",message);
    char *text=cJSON_PrintUnformatted(json);
    if(text) http_reply(fd,status,"application/json",text,strlen(text),NULL);
    free(text);cJSON_Delete(json);
}
int http_file(int fd,const char *path,const char *type,size_t limit,int fresh,const char *extra)
{
    int input=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(input<0) {http_error(fd,503,"Camera state unavailable");return -1;}
    struct stat st;time_t now=time(NULL);
    if(fstat(input,&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>(off_t)limit ||
        (fresh && (now<st.st_mtime || now-st.st_mtime>10))) {
        close(input);http_error(fd,503,"Camera state unavailable or stale");return -1;
    }
    if(!strcmp(type,"application/json")) {
        char text[4098];
        if(st.st_size>4096) {close(input);http_error(fd,503,"Invalid camera state");return -1;}
        ssize_t n=read(input,text,(size_t)st.st_size);
        if(n!=st.st_size) {close(input);http_error(fd,503,"Invalid camera state");return -1;}
        text[n]=0; cJSON *json=json_parse(text);
        if(!json || memchr(text,0,(size_t)n)) {cJSON_Delete(json);close(input);http_error(fd,503,"Invalid camera state");return -1;}
        cJSON_Delete(json);close(input);http_reply(fd,200,type,text,(size_t)n,extra);return 0;
    }
    int result=response_header(fd,200,type,(size_t)st.st_size,1,extra);
    char buffer[4096];off_t remaining=st.st_size;uint64_t deadline=monotonic_ms()+5000;
    while(!result && remaining) {
        ssize_t n=read(input,buffer,remaining>(off_t)sizeof(buffer)?sizeof(buffer):(size_t)remaining);
        if(n<0 && errno==EINTR) continue;
        if(n<=0 || transfer_write(fd,buffer,(size_t)n,deadline)) {result=-1;break;}
        remaining-=n;
    }
    close(input);return result;
}
int privacy_enabled(void)
{
    if(!access(PRIVACY_MARKER,F_OK)) return 1;
    if(errno!=ENOENT) return 1;
    pthread_mutex_lock(&gateway.lock);int enabled=gateway.privacy;pthread_mutex_unlock(&gateway.lock);
    return enabled;
}
static int privacy_assets_ready(void)
{
    const char *names[]={"privacy-slate-vs0.h264","privacy-slate-vs1.h264","privacy-slate.jpg"};
    char path[600];struct stat st;
    for(unsigned i=0;i<3;i++) {
        snprintf(path,sizeof(path),"%s/%s",gateway.assets,names[i]);
        if(lstat(path,&st) || !S_ISREG(st.st_mode) || st.st_size<5 || st.st_size>512*1024) return 0;
    }
    return 1;
}
static void privacy_http(int fd,const struct request *r)
{
    if(*r->query) {http_error(fd,400,"Unexpected query");return;}
    if(!strcmp(r->method,"POST")) {
        char data[129];
        if(!r->content_length || r->content_length>128 || transfer_read(fd,data,r->content_length,monotonic_ms()+3000)) {
            http_error(fd,400,"Expected one enabled boolean");return;
        }
        data[r->content_length]=0;cJSON *json=json_parse(data);
        const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(json,"enabled");
        int valid=!memchr(data,0,r->content_length) && json_unique_object(json) &&
            cJSON_GetArraySize(json)==1 && cJSON_IsBool(enabled);
        int value=cJSON_IsTrue(enabled);cJSON_Delete(json);
        if(!valid) {http_error(fd,400,"Expected one enabled boolean");return;}
        if(value && !privacy_assets_ready()) {http_error(fd,503,"Privacy slate unavailable");return;}
        pthread_mutex_lock(&gateway.lock);
        if(value!=gateway.privacy) {
            int error=0;
            if(value) {
                int marker=open(PRIVACY_MARKER,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
                if(marker<0) error=1; else if(close(marker)) error=1;
            } else if(unlink(PRIVACY_MARKER)) error=1;
            if(error) {pthread_mutex_unlock(&gateway.lock);http_error(fd,503,"Cannot change privacy mode");return;}
            gateway.privacy=value;
        }
        if(value) {cancel_audio_locked();cancel_preview_locked();}
        pthread_mutex_unlock(&gateway.lock);
        if(value) {unlink("/tmp/anyka-snapshot.jpg");usleep(100000);}
    } else if(strcmp(r->method,"GET")) {http_error(fd,405,"Use GET or POST");return;}
    const char *body=privacy_enabled()?"{\"enabled\":true}\n":"{\"enabled\":false}\n";
    http_reply(fd,200,"application/json",body,strlen(body),NULL);
}
static int valid_host(const char *input)
{
    char host[128];
    if(!*input || strlen(input)>=sizeof(host)) return 0;
    strcpy(host,input);
    char *port=strchr(host,':');
    if(port) {long n;*port++=0;if(decimal(port,1,65535,&n)) return 0;}
    struct in_addr address;
    return inet_pton(AF_INET,host,&address)==1 || !strcasecmp(host,"GP5_A10S14A5") || !strcmp(host,"localhost");
}
/* Only authenticated allowlisted CGI calls are forwarded to the existing
 * loopback backend. Client Authorization/Cookie headers never reach it. */
static int backend_header(int fd,char *out,size_t capacity,uint64_t deadline)
{
    /* haserl emits LF-only CGI headers, while BusyBox prepends a CRLF status
     * line. Accept that private backend format; public parsing stays strict. */
    char raw[4097];size_t have=0;
    while(have+1<sizeof(raw)) {
        struct pollfd p={fd,POLLIN,0};int ready=poll(&p,1,100);
        if(monotonic_ms()>=deadline || stopping) return -1;
        if(ready<0 && errno==EINTR) continue;
        if(ready<0 || p.revents&(POLLERR|POLLNVAL)) return -1;
        if(!(p.revents&(POLLIN|POLLHUP))) continue;
        ssize_t n=recv(fd,raw+have,sizeof(raw)-have-1,MSG_PEEK|MSG_DONTWAIT);
        if(n<0 && (errno==EAGAIN || errno==EINTR)) continue;
        if(n<=0) return -1;
        raw[have+n]=0;char *end=strstr(raw,"\r\n\r\n"),*lf=strstr(raw,"\n\n");
        size_t take=(size_t)n;if(end) take=(size_t)(end+4-raw)-have;
        if(lf && (!end || lf<end)) {end=lf;take=(size_t)(lf+2-raw)-have;}
        if(memchr(raw+have,0,take) || transfer_read(fd,raw+have,take,deadline)) return -1;
        have+=take;raw[have]=0;
        if(end) {
            size_t used=0;for(size_t i=0;i<have;i++) {
                if(raw[i]=='\r') {if(i+1==have || raw[i+1]!='\n') return -1;continue;}
                if(used+3>=capacity) return -1;
                if(raw[i]=='\n') out[used++]='\r';
                out[used++]=raw[i];
            }
            out[used]=0;return 0;
        }
    }
    return -1;
}
static void backend_proxy(int fd,const struct request *r,const char *extra)
{
    unsigned retries=0;uint64_t deadline=monotonic_ms()+10000;
again: ;
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(8080)};
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int backend=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(backend<0) {http_error(fd,503,"Camera backend unavailable");return;}
    if(connect(backend,(struct sockaddr *)&addr,sizeof(addr)) && errno!=EINPROGRESS) goto unavailable;
    const char *path=!strcmp(r->path,"/image.jpg")?"/cgi-bin/snapshot.cgi":r->path;
    char request[4096],body[4097],response[8193];
    if(r->content_length>4096) {close(backend);http_error(fd,413,"Request body too large");return;}
    if(r->content_length && transfer_read(fd,body,r->content_length,monotonic_ms()+5000)) {close(backend);return;}
    char host[128],route[4098],gw[64]="";snprintf(host,sizeof(host),"%s",header(r,"Host"));
    char *colon=strchr(host,':');if(colon) *colon=0;
    if(read_small("/proc/net/route",route,4096,0)>=0) {
        char *save=NULL;for(char *line=strtok_r(route,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
            unsigned long value,flags;
            if(sscanf(line,"wlan0 00000000 %lx %lx",&value,&flags)==2 && (flags&3)==3) {
                snprintf(gw,sizeof(gw),"%lu.%lu.%lu.%lu",value&255,(value>>8)&255,(value>>16)&255,(value>>24)&255);break;
            }
        }
    }
    struct stat st;time_t now=time(NULL);int running=!stat("/tmp/anyka-image.json",&st) && now>=st.st_mtime && now-st.st_mtime<=10;
    pthread_mutex_lock(&gateway.lock);int configured=*gateway.password_hash!=0;pthread_mutex_unlock(&gateway.lock);
    int n=snprintf(request,sizeof(request),"%s %s%s%s HTTP/1.0\r\nHost: %s\r\nOrigin: %s\r\n"
        "X-Anyka-Configured: %s\r\nX-Anyka-Logged-In: %s\r\nX-Anyka-Address: %s\r\nX-Anyka-Gateway: %s\r\nX-Anyka-RTSP-State: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        r->method,path,*r->query?"?":"",r->query,header(r,"Host"),header(r,"Origin"),configured?"true":"false",
        session_authorized(r)?"true":"false",host,gw,running?"running":"unavailable",r->content_length);
    if(n<=0 || n>=(int)sizeof(request) || transfer_write(backend,request,(size_t)n,deadline) ||
        (r->content_length && transfer_write(backend,body,r->content_length,deadline))) goto unavailable;
    if(backend_header(backend,response,sizeof(response),deadline)) goto unavailable;
    int status=0;
    if(sscanf(response,"HTTP/1.%*c %d",&status)!=1 || status<200 || status>599) goto unavailable;
    /* The SDK refuses JPEG below its existing 6144 KiB memory guard. A
     * short TLS allocation can cross it transiently. Retry this read once,
     * within the original deadline; never retry a control POST or lower the
     * SDK guard, and never retry through a privacy transition. */
    if(status==503 && !strcmp(path,"/cgi-bin/snapshot.cgi") && !strcmp(r->method,"GET") &&
        !retries++ && !privacy_enabled() && monotonic_ms()+2000<deadline) {
        close(backend);usleep(250000);goto again;
    }
    int jpeg=(!strcmp(path,"/cgi-bin/snapshot.cgi") && status==200);
    char type[128]="application/json";
    for(char *line=strstr(response,"\r\n");line && line[2];line=strstr(line+2,"\r\n")) {
        if(!strncasecmp(line+2,"Content-Type:",13)) {
            char *start=line+15;while(*start==' ') start++;
            char *end=strstr(start,"\r\n");if(!end || (size_t)(end-start)>=sizeof(type)) goto unavailable;
            for(char *v=start;v<end;v++) if((unsigned char)*v<32 || (unsigned char)*v>126) goto unavailable;
            memcpy(type,start,(size_t)(end-start));type[end-start]=0;break;
        }
    }
    if(jpeg) strcpy(type,"image/jpeg");
    if(response_header(fd,status,type,0,0,extra) || !strcmp(r->method,"HEAD")) {close(backend);return;}
    size_t total=0,limit=jpeg?512*1024:!strncmp(path,"/a/",3)?1024*1024:webui_route(path)?65536:8192;
    while(monotonic_ms()<deadline && total<limit) {
        struct pollfd pollfd={backend,POLLIN,0};
        int ready=poll(&pollfd,1,100);
        if(ready<0 && errno==EINTR) continue;
        if(ready<0 || pollfd.revents&(POLLERR|POLLNVAL)) break;
        if(!(pollfd.revents&(POLLIN|POLLHUP))) continue;
        ssize_t count=recv(backend,body,sizeof(body)-1,MSG_DONTWAIT);
        if(count<0 && (errno==EAGAIN || errno==EINTR)) continue;
        if(count<=0 || total+(size_t)count>limit) break;
        if(transfer_write(fd,body,(size_t)count,deadline)) break;
        total+=(size_t)count;
    }
    close(backend);return;
unavailable:
    close(backend);http_error(fd,503,"Camera backend unavailable");
}
static int allowed_route(const struct request *r)
{
    static const char *paths[]={"/logout","/cgi-bin/metrics.cgi","/cgi-bin/isp.cgi","/cgi-bin/gpio.cgi","/cgi-bin/ptz.cgi",
        "/cgi-bin/snapshot.cgi","/image.jpg","/api/privacy","/api/ptz/presets","/api/audio/live","/api/audio/play","/api/audio/stop"};
    if(gateway.web_ui && (webui_route(r->path) || !strcmp(r->path,"/api/ws") || !strcmp(r->path,"/api/time") || !strcmp(r->path,"/api/ntfy"))) return 1;
    if(!strcmp(r->path,"/cgi-bin/access.cgi")) return !strcmp(r->method,"POST");
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++) if(!strcmp(paths[i],r->path)) return 1;
    return 0;
}
void http_serve(int fd,const char *peer)
{
    struct request r;
    if(receive_header(fd,r.raw,sizeof(r.raw),monotonic_ms()+5000)<0 || parse_request(&r,0)) {http_error(fd,400,"Invalid request");return;}
    if(!valid_host(header(&r,"Host"))) {http_error(fd,400,"Unrecognized camera host");return;}
    if(!allowed_route(&r)) {http_error(fd,404,"Not found");return;}
    if(strcmp(r.method,"GET") && strcmp(r.method,"POST") && strcmp(r.method,"HEAD")) {http_error(fd,405,"Method not allowed");return;}
    if(!strcmp(r.method,"POST")) {
        char origin[160];snprintf(origin,sizeof(origin),"http://%s",header(&r,"Host"));
        if(strcmp(origin,header(&r,"Origin"))) {http_error(fd,403,"Same-origin POST required");return;}
    } else if(r.content_length) {http_error(fd,400,"Unexpected request body");return;}
    pthread_mutex_lock(&gateway.lock);int configured=*gateway.password_hash!=0;pthread_mutex_unlock(&gateway.lock);
    if(!configured && strcmp(peer,gateway.setup_ip)) {http_error(fd,403,"Initial setup restricted to installation computer");return;}
    if(!strcmp(r.path,"/cgi-bin/access.cgi") && !strcmp(r.method,"POST")) {access_post(fd,&r,peer);return;}
    int ui=webui_route(r.path);
    if(ui && strcmp(r.method,"GET") && strcmp(r.method,"HEAD")) {http_error(fd,405,"Use GET or HEAD");return;}
    if(ui && (*r.query && strcmp(r.path,"/cgi-bin/camera.cgi") && strcmp(r.path,"/cgi-bin/access.cgi") && strcmp(r.path,"/cgi-bin/logs-data.cgi"))) {http_error(fd,400,"Unexpected query");return;}
    int public=gateway.web_ui && (!strcmp(r.path,"/cgi-bin/access.cgi") || !strncmp(r.path,"/a/",3) || !strcmp(r.path,"/favicon.ico"));
    if(!strcmp(r.path,"/logout")) {
        if(strcmp(r.method,"POST")) {http_error(fd,405,"Use POST");return;}
        char token[65];
        if(!session_token(&r,token)) {
            pthread_mutex_lock(&gateway.lock);
            for(unsigned i=0;i<SESSION_COUNT;i++) if(equal_secret(token,gateway.sessions[i].token)) gateway.sessions[i].expires=0;
            pthread_mutex_unlock(&gateway.lock);
        }
        http_reply(fd,303,"application/json","",0,"Set-Cookie: anyka_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0\r\nLocation: /cgi-bin/access.cgi\r\n");return;
    }
    int snapshot=!strcmp(r.path,"/image.jpg") || !strcmp(r.path,"/cgi-bin/snapshot.cgi");
    char extra[512]="";
    if(!public && !session_authorized(&r)) {
        if(snapshot && !strcmp(r.method,"GET")) {
            int status=digest_check(&r,extra,sizeof(extra));
            if(status!=200) {http_reply(fd,status,"application/json","",0,extra);return;}
        } else {
            if(gateway.web_ui && ui && !*header(&r,"Upgrade")) http_reply(fd,303,"text/html","",0,"Location: /cgi-bin/access.cgi\r\n");
            else http_error(fd,401,"Unauthorized");
            return;
        }
    }
    if(ui) {
        if(!strcmp(r.path,"/") || !strcmp(r.path,"/index.html")) {http_reply(fd,303,"text/html","",0,"Location: /cgi-bin/dashboard.cgi\r\n");return;}
        backend_proxy(fd,&r,extra);return;
    }
    if(!strcmp(r.path,"/api/ptz/presets")) {ptz_presets_http(fd,&r);return;}
    if(!strcmp(r.path,"/api/time")) {webui_time(fd,&r);return;}
    if(!strcmp(r.path,"/api/ntfy")) {webui_notification(fd,&r);return;}
    if(!strcmp(r.path,"/api/privacy")) {privacy_http(fd,&r);return;}
    if(privacy_enabled()) {
        if(snapshot && !strcmp(r.method,"GET") && !*r.query) {
            char path[600];snprintf(path,sizeof(path),"%s/privacy-slate.jpg",gateway.assets);
            http_file(fd,path,"image/jpeg",512*1024,0,extra);return;
        }
        if(snapshot || !strcmp(r.path,"/api/ws") || !strcmp(r.path,"/api/audio/live") || !strcmp(r.path,"/api/audio/play")) {http_error(fd,503,"Camera privacy mode is enabled");return;}
    }
    if(!strcmp(r.path,"/api/ws")) {preview_live(fd,&r);return;}
    if(!strcmp(r.path,"/api/audio/live")) {audio_live(fd,&r);return;}
    if(!strcmp(r.path,"/api/audio/play") || !strcmp(r.path,"/api/audio/stop")) {audio_post(fd,&r);return;}
    if(!strcmp(r.method,"GET") && !strcmp(r.path,"/cgi-bin/metrics.cgi")) {
        if(*r.query) http_error(fd,400,"Unexpected query");else camera_metrics(fd);return;
    }
    if(!strcmp(r.method,"GET") && !strcmp(r.path,"/cgi-bin/isp.cgi")) {
        if(*r.query) http_error(fd,400,"Unexpected query");else http_file(fd,"/tmp/anyka-image.json","application/json",4096,1,extra);return;
    }
    if(!strcmp(r.method,"GET") && !strcmp(r.path,"/cgi-bin/gpio.cgi") && !strcmp(r.query,"action=daynight-status")) {
        http_file(fd,"/tmp/anyka-daynight.json","application/json",4096,1,extra);return;
    }
    if(snapshot && (strcmp(r.method,"GET") || *r.query)) {http_error(fd,405,"Use GET without query");return;}
    if(!strcmp(r.path,"/cgi-bin/metrics.cgi")) {http_error(fd,405,"Use GET");return;}
    backend_proxy(fd,&r,extra);
}
static void metric_file(cJSON *json,const char *key,const char *path)
{
    char data[130];if(read_small(path,data,128,0)<0) return;
    char *end;double value=strtod(data,&end);
    if(end!=data && value>=0) cJSON_AddNumberToObject(json,key,value);
}
void camera_metrics(int fd)
{
    char text[8194];cJSON *json=cJSON_CreateObject();
    if(!json) return;
    if(read_small("/proc/stat",text,8192,0)<0) goto unavailable;
    unsigned long long n[8];
    if(sscanf(text,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",&n[0],&n[1],&n[2],&n[3],&n[4],&n[5],&n[6],&n[7])!=8) goto unavailable;
    uint64_t total=0;for(unsigned i=0;i<8;i++) total+=n[i];
    cJSON_AddNumberToObject(json,"cpuTotal",(double)total);cJSON_AddNumberToObject(json,"cpuIdle",(double)(n[3]+n[4]));
    if(read_small("/proc/meminfo",text,8192,0)<0) goto unavailable;
    uint64_t kernel=0;char *save;
    for(char *line=strtok_r(text,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        char name[64],unit[16];unsigned long value;
        if(sscanf(line,"%63s %lu %15s",name,&value,unit)!=3 || strcmp(unit,"kB")) continue;
        const char *key=NULL;
        if(!strcmp(name,"MemTotal:")) key="memTotal";
        if(!strcmp(name,"MemAvailable:")) key="memAvailable";
        if(!strcmp(name,"AnonPages:")) key="memAnon";
        if(!strcmp(name,"Shmem:")) key="memShmem";
        if(key) cJSON_AddNumberToObject(json,key,(double)value*1024);
        if(!strcmp(name,"SUnreclaim:") || !strcmp(name,"KernelStack:") || !strcmp(name,"PageTables:")) kernel+=(uint64_t)value*1024;
    }
    cJSON_AddNumberToObject(json,"memKernel",(double)kernel);
    metric_file(json,"uptime","/proc/uptime");metric_file(json,"load","/proc/loadavg");
    metric_file(json,"rx","/sys/class/net/wlan0/statistics/rx_bytes");metric_file(json,"tx","/sys/class/net/wlan0/statistics/tx_bytes");
    cJSON *rssi=cJSON_AddNullToObject(json,"wifiRssi"),*temperature=cJSON_AddNullToObject(json,"temperatureMilliC");
    if(read_small("/proc/net/wireless",text,8192,0)>=0) {
        char *wifi=strstr(text,"wlan0:");int strength;
        if(rssi && wifi && sscanf(wifi,"wlan0: %*x %*f %d",&strength)==1 && strength>-120 && strength<=0) {
            rssi->type=cJSON_Number;cJSON_SetNumberValue(rssi,strength);
        }
    }
    glob_t paths={0};
    if(!glob("/sys/class/thermal/thermal_zone*/temp",0,NULL,&paths)) {
        for(size_t i=0;i<paths.gl_pathc;i++) {
            char type[512];snprintf(type,sizeof(type),"%s",paths.gl_pathv[i]);char *slash=strrchr(type,'/');strcpy(slash+1,"type");
            if(read_small(type,text,8192,0)<0 || (!strstr(text,"cpu") && !strstr(text,"soc") && !strstr(text,"ak39"))) continue;
            if(read_small(paths.gl_pathv[i],text,128,0)>=0) {
                long value=strtol(text,NULL,10);
                if(temperature && value>=-40000 && value<=125000) {temperature->type=cJSON_Number;cJSON_SetNumberValue(temperature,value);break;}
            }
        }
    }
    globfree(&paths);
    char *body=cJSON_PrintUnformatted(json);
    if(body) http_reply(fd,200,"application/json",body,strlen(body),NULL);
    free(body);cJSON_Delete(json);return;
unavailable:
    cJSON_Delete(json);http_error(fd,503,"System counters unavailable");
}
