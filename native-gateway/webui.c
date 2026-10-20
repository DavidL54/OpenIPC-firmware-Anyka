#include "gateway.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <spawn.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int webui_route(const char *path)
{
    static const char *routes[]={"/","/index.html","/favicon.ico","/cgi-bin/access.cgi","/cgi-bin/dashboard.cgi",
        "/cgi-bin/live.cgi","/cgi-bin/camera.cgi","/cgi-bin/stream-urls.cgi","/cgi-bin/logs.cgi",
        "/cgi-bin/logs-data.cgi","/cgi-bin/tools.cgi","/cgi-bin/extensions.cgi","/cgi-bin/ntfy.cgi"};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++) if(!strcmp(path,routes[i])) return 1;
    if(strncmp(path,"/a/",3) || !path[3] || strchr(path+3,'/') || strlen(path)>160) return 0;
    if(strspn(path+3,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=strlen(path+3)) return 0;
    const char *ext=strrchr(path,'.');if(!ext) return 0;
    static const char *extensions[]={".js",".css",".svg",".png",".jpg",".ico",".woff",".woff2",".ttf"};
    for(unsigned i=0;i<sizeof(extensions)/sizeof(extensions[0]);i++) if(!strcmp(ext,extensions[i])) return 1;
    return 0;
}
void webui_time(int fd,const struct request *r)
{
    char body[129],value[32];long seconds;
    if(strcmp(r->method,"POST") || *r->query || !r->content_length || r->content_length>128 ||
        strncmp(header(r,"Content-Type"),"application/x-www-form-urlencoded",33) ||
        transfer_read(fd,body,r->content_length,monotonic_ms()+3000)) {http_error(fd,400,"Expected timestamp form");return;}
    body[r->content_length]=0;
    if(memchr(body,0,r->content_length) || form_value(body,"seconds",value,sizeof(value)) || decimal(value,1700000000,2147483647,&seconds)) {http_error(fd,400,"Invalid timestamp");return;}
    struct timespec ts={.tv_sec=(time_t)seconds};
    if(clock_settime(CLOCK_REALTIME,&ts)) {http_error(fd,500,"Clock update failed");return;}
    http_reply(fd,200,"text/plain; charset=utf-8","Clock updated",13,NULL);
}
static int quote(char *out,size_t capacity,size_t *used,const char *input)
{
    for(const unsigned char *p=(const unsigned char *)input;*p;p++) {
        char escaped=0;
        switch(*p) {case '\\':escaped='\\';break;case '"':escaped='"';break;case '\n':escaped='n';break;case '\r':escaped='r';break;case '\t':escaped='t';break;}
        if(*used+3>=capacity || (!escaped && *p<32)) return -1;
        if(escaped) {out[(*used)++]='\\';out[(*used)++]=escaped;}else out[(*used)++]=(char)*p;
    }
    out[*used]=0;return 0;
}
/* curl is a short-lived musl C helper from the same OpenIPC image. Secrets
 * travel only through stdin configuration, never argv, files or SDK memory.
 * Certificate/hostname verification is mandatory; redirects are not followed. */
void webui_notification(int fd,const struct request *r)
{
    char body[4097],server[256],topic[121],token[513],message[1025],config[8192];
    if(strcmp(r->method,"POST") || *r->query || !r->content_length || r->content_length>4096 ||
        strncmp(header(r,"Content-Type"),"application/x-www-form-urlencoded",33) ||
        transfer_read(fd,body,r->content_length,monotonic_ms()+5000)) {http_error(fd,400,"Expected notification form");return;}
    body[r->content_length]=0;
    if(memchr(body,0,r->content_length) || form_value(body,"server",server,sizeof(server)) || form_value(body,"topic",topic,sizeof(topic)) ||
        form_value(body,"token",token,sizeof(token)) || form_value(body,"message",message,sizeof(message))) {http_error(fd,400,"Invalid notification form");goto clear;}
    size_t length=strlen(server);if(length && server[length-1]=='/') server[--length]=0;
    if(strncmp(server,"https://",8) || !server[8] || strspn(server+8,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-")!=strlen(server+8) ||
        strlen(topic)<3 || strspn(topic,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(topic) ||
        strchr(token,'\r') || strchr(token,'\n') || !*message) {http_error(fd,400,"Use an HTTPS server origin and a valid topic");goto clear;}
    pthread_mutex_lock(&gateway.lock);uint64_t now=monotonic_ms();
    int limited=gateway.last_notification && now-gateway.last_notification<15000;
    if(!limited) gateway.last_notification=now;
    pthread_mutex_unlock(&gateway.lock);
    if(limited) {http_error(fd,429,"Wait fifteen seconds before sending again");goto clear;}
    size_t used=(size_t)snprintf(config,sizeof(config),"url = \"%s/%s\"\nheader = \"Content-Type: text/plain; charset=utf-8\"\n",server,topic);
    if(*token) {
        const char *prefix="header = \"Authorization: Bearer ";size_t n=strlen(prefix);
        memcpy(config+used,prefix,n);used+=n;
        if(quote(config,sizeof(config),&used,token)) goto invalid;
        memcpy(config+used,"\"\n",2);used+=2;
    }
    memcpy(config+used,"data-binary = \"",15);used+=15;
    if(quote(config,sizeof(config),&used,message)) goto invalid;
    memcpy(config+used,"\"\n",2);used+=2;
    int input[2],output[2];if(pipe2(input,O_CLOEXEC)) goto unavailable;
    if(pipe2(output,O_CLOEXEC)) {close(input[0]);close(input[1]);goto unavailable;}
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,input[0],STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions,output[1],STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
    char *argv[]={"/media/lib/ld-musl-arm.so.1","--library-path","/media/lib:/media/usr/lib","/media/usr/bin/curl","-q",
        "--silent","--max-time","10","--connect-timeout","5","--proto","=https","--max-redirs","0",
        "--cacert","/media/etc/ssl/certs/ca-certificates.crt","--output","/dev/null","--write-out","%{http_code}","--config","-",NULL};
    char *env[]={"PATH=/bin:/usr/bin","LC_ALL=C","LD_PRELOAD=/media/usr/lib/anyka-clock-compat.so",NULL};pid_t child;
    int error=posix_spawn(&child,argv[0],&actions,NULL,argv,env);posix_spawn_file_actions_destroy(&actions);
    close(input[0]);close(output[1]);
    if(error) {close(input[1]);close(output[0]);goto unavailable;}
    ssize_t written=write(input[1],config,used);close(input[1]);
    uint64_t deadline=monotonic_ms()+12000;int status=0,finished=0;
    while(monotonic_ms()<deadline && !stopping) {
        pid_t done=waitpid(child,&status,WNOHANG);if(done==child) {finished=1;break;}if(done<0 && errno!=EINTR) break;
        struct pollfd client={fd,0,0};poll(&client,1,100);if(client.revents&(POLLHUP|POLLERR|POLLNVAL)) break;
    }
    if(!finished) {kill(child,SIGKILL);while(waitpid(child,&status,0)<0 && errno==EINTR) {}}
    char code[4]={0};ssize_t n=read(output[0],code,3);close(output[0]);
    if(written!=(ssize_t)used || !finished || !WIFEXITED(status) || WEXITSTATUS(status) || n!=3 || code[0]!='2') goto unavailable;
    http_reply(fd,200,"text/plain; charset=utf-8","Sent",4,NULL);goto clear;
invalid:
    http_error(fd,400,"Invalid notification text");goto clear;
unavailable:
    http_error(fd,502,"HTTPS delivery failed; check clock, DNS and certificates");
clear:
    memset(body,0,sizeof(body));memset(token,0,sizeof(token));memset(message,0,sizeof(message));memset(config,0,sizeof(config));
}
