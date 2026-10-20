#include "gateway.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct state gateway={.lock=PTHREAD_MUTEX_INITIALIZER,.password_lock=PTHREAD_MUTEX_INITIALIZER,
    .persist_lock=PTHREAD_MUTEX_INITIALIZER,.audio_lock=PTHREAD_MUTEX_INITIALIZER,
    .preview_lock=PTHREAD_MUTEX_INITIALIZER,.audio_fd=-1,.audio_client=-1,.preview_fd=-1,.preview_client=-1};
volatile sig_atomic_t stopping;
static void stop(int signal_number) { (void)signal_number;stopping=1; }
static int copy_env(char *dest,size_t capacity,const char *key,const char *fallback)
{
    const char *value=getenv(key);if(!value || !*value) value=fallback;
    if(strlen(value)>=capacity) return -1;
    strcpy(dest,value);return 0;
}
static void *worker_run(void *argument)
{
    struct worker *worker=argument;
    int fd=worker->fd,kind=worker->kind;
    if(kind) native_serve(fd);
    else {
        struct sockaddr_in addr;socklen_t length=sizeof(addr);char peer[INET_ADDRSTRLEN]="";
        if(!getpeername(fd,(struct sockaddr *)&addr,&length) && inet_ntop(AF_INET,&addr.sin_addr,peer,sizeof(peer))) http_serve(fd,peer);
    }
    pthread_mutex_lock(&gateway.lock);
    close(fd);worker->active=0;worker->fd=-1;
    pthread_mutex_unlock(&gateway.lock);return NULL;
}
static int listener_http(void)
{
    const char *endpoint=getenv("ANYKA_WEB_LISTEN");if(!endpoint || !*endpoint) endpoint=":80";
    char ip[64];long port=80;struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr={.s_addr=htonl(INADDR_ANY)}};
    const char *colon=strrchr(endpoint,':');
    if(!colon || (size_t)(colon-endpoint)>=sizeof(ip) || decimal(colon+1,1,65535,&port)) return -1;
    memcpy(ip,endpoint,(size_t)(colon-endpoint));ip[colon-endpoint]=0;
    if(*ip && inet_pton(AF_INET,ip,&addr.sin_addr)!=1) return -1;
    addr.sin_port=htons((uint16_t)port);
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0) return -1;
    int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    if(bind(fd,(struct sockaddr *)&addr,sizeof(addr)) || listen(fd,8)) {close(fd);return -1;}
    return fd;
}
static int listener_auth(void)
{
    struct stat st;
    if(mkdir("/conf/anyka-private",0700) && errno!=EEXIST) return -1;
    if(lstat("/conf/anyka-private",&st) || !S_ISDIR(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&0777)!=0700) return -1;
    struct sockaddr_un addr={.sun_family=AF_UNIX};strcpy(addr.sun_path,AUTH_SOCKET);
    if(!lstat(AUTH_SOCKET,&st)) {
        if(!S_ISSOCK(st.st_mode) || st.st_uid!=geteuid()) return -1;
        int old=connect_unix(AUTH_SOCKET,250);
        if(old>=0) {close(old);return -1;}
        if(unlink(AUTH_SOCKET)) return -1;
    } else if(errno!=ENOENT) return -1;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0) return -1;
    if(bind(fd,(struct sockaddr *)&addr,sizeof(addr)) || chmod(AUTH_SOCKET,0600) || listen(fd,2)) {close(fd);return -1;}
    return fd;
}
int main(int argc,char **argv)
{
    if(argc==2 && (!strcmp(argv[1],"-version") || !strcmp(argv[1],"--version"))) {
        puts("anyka-gateway C native-webui linux/arm v2");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--ptz-startup") && geteuid()==0) return ptz_startup();
    if(argc!=1 || geteuid()!=0) {fputs("anyka-gateway: root and no arguments required\n",stderr);return 1;}
    const char *native=getenv("ANYKA_NATIVE_RTSP");
    if(native && strcmp(native,"1")) {fputs("anyka-gateway: native SDK RTSP required\n",stderr);return 1;}
    struct rlimit cores={0,0};setrlimit(RLIMIT_CORE,&cores);prctl(PR_SET_DUMPABLE,0,0,0,0);
    /* Temporary SD comparisons may use the old supervisor's process name.
     * Internal images use the explicit anyka-gateway name. */
    const char *compat=getenv("ANYKA_COMPAT_PROCESS_NAME");
    if(compat && strcmp(compat,"go2rtc")) return 1;
    prctl(PR_SET_NAME,compat?compat:"anyka-gateway",0,0,0);
    umask(0077);signal(SIGPIPE,SIG_IGN);signal(SIGINT,stop);signal(SIGTERM,stop);
    const char *assets=access("/app/runtime/sdk-rtsp",R_OK)==0?"/app/runtime/sdk-rtsp":"/mnt/openipc-persist/sdk-rtsp";
    if(copy_env(gateway.access_file,sizeof(gateway.access_file),"ANYKA_ACCESS_FILE","/mnt/openipc-persist/web-access.json") ||
        copy_env(gateway.assets,sizeof(gateway.assets),"ANYKA_PRIVACY_ASSETS",assets) ||
        copy_env(gateway.setup_ip,sizeof(gateway.setup_ip),"ANYKA_SETUP_IP","192.0.2.20") || load_access()) {
        fputs("anyka-gateway: invalid private configuration\n",stderr);return 1;
    }
    gateway.privacy=access(PRIVACY_MARKER,F_OK)==0 || errno!=ENOENT;
    const char *ui=getenv("ANYKA_WEB_UI");
    if(ui && strcmp(ui,"0") && strcmp(ui,"1")) return 1;
    gateway.web_ui=!ui || strcmp(ui,"0");
    if(unlink("/conf/anyka-private/rtsp-ha1") && errno!=ENOENT) return 1;
    int http=listener_http();if(http<0) {perror("anyka-gateway HTTP listener");return 1;}
    int auth=listener_auth();if(auth<0) {close(http);perror("anyka-gateway private auth listener");return 1;}
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);pthread_attr_setstacksize(&attr,128*1024);
    fprintf(stderr,"anyka-gateway: C API ready; OpenIPC interface=%d; bounded HTTP/audio/preview\n",gateway.web_ui);
    while(!stopping) {
        struct pollfd listeners[2]={{http,POLLIN,0},{auth,POLLIN,0}};
        int ready=poll(listeners,2,250);if(ready<0 && errno==EINTR) continue;if(ready<0) break;
        for(unsigned kind=0;kind<2;kind++) if(listeners[kind].revents&POLLIN) {
            int client=accept4(listeners[kind].fd,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);if(client<0) continue;
            if(!kind) {int yes=1;setsockopt(client,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));}
            pthread_mutex_lock(&gateway.lock);
            unsigned first=kind?HTTP_SLOTS:0,limit=kind?HTTP_SLOTS+AUTH_SLOTS:HTTP_SLOTS,index;
            for(index=first;index<limit;index++) if(!gateway.workers[index].active) break;
            if(index==limit) close(client);
            else {
                gateway.workers[index]=(struct worker){.fd=client,.kind=(int)kind,.active=1};pthread_t thread;
                int error=pthread_create(&thread,&attr,worker_run,&gateway.workers[index]);
                if(error) {
                    fprintf(stderr,"anyka-gateway: cannot start bounded worker: %s\n",strerror(error));
                    close(client);gateway.workers[index].active=0;stopping=1;
                }
            }
            pthread_mutex_unlock(&gateway.lock);
        }
    }
    close(http);close(auth);unlink(AUTH_SOCKET);pthread_attr_destroy(&attr);
    pthread_mutex_lock(&gateway.lock);cancel_audio_locked();
    for(unsigned i=0;i<HTTP_SLOTS+AUTH_SLOTS;i++) if(gateway.workers[i].active) shutdown(gateway.workers[i].fd,SHUT_RDWR);
    pthread_mutex_unlock(&gateway.lock);
    /* Let a password writer finish fsync and the read-only remount before exit. */
    for(;;) {
        unsigned active=0;
        pthread_mutex_lock(&gateway.lock);
        for(unsigned i=0;i<HTTP_SLOTS+AUTH_SLOTS;i++) active+=gateway.workers[i].active;
        pthread_mutex_unlock(&gateway.lock);
        if(!active) break;
        usleep(20000);
    }
    memset(gateway.password_hash,0,sizeof(gateway.password_hash));memset(gateway.ha1,0,sizeof(gateway.ha1));
    return 0;
}
