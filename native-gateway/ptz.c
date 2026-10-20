#include "gateway.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <spawn.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern char **environ;
static pthread_mutex_t presets_lock=PTHREAD_MUTEX_INITIALIZER;
static int integer(const cJSON *o,const char *key,int low,int high,int *out)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsNumber(v) || v->valuedouble<low || v->valuedouble>high || v->valuedouble!=(int)v->valuedouble) return -1;
    *out=(int)v->valuedouble;return 0;
}
static int preset_path(char out[528])
{
    const char *access_file=*gateway.access_file?gateway.access_file:getenv("ANYKA_ACCESS_FILE");
    if(!access_file) access_file="/mnt/openipc-persist/web-access.json";
    if(strlen(access_file)>500) return -1;
    strcpy(out,access_file);char *slash=strrchr(out,'/');if(!slash) return -1;
    strcpy(slash+1,"ptz-presets.json");return 0;
}
static cJSON *find_slot(cJSON *json,int id)
{
    cJSON *v; cJSON_ArrayForEach(v,cJSON_GetObjectItemCaseSensitive(json,"positions")) {
        int n;if(!integer(v,"id",1,8,&n) && n==id) return v;
    }
    return NULL;
}
static int valid_name(const char *s)
{
    size_t length=strlen(s);if(!length || length>48) return 0;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++) if(*p<32 || *p==127) return 0;
    return 1;
}
static cJSON *load_presets(void)
{
    char path[528],data[4098];if(preset_path(path)) return NULL;
    ssize_t n=read_small(path,data,4096,0);
    if(n<0 && errno==ENOENT) return json_parse("{\"version\":1,\"startup\":0,\"positions\":[]}");
    if(n<0) return NULL;
    cJSON *json=json_parse(data),*slots=cJSON_GetObjectItemCaseSensitive(json,"positions");int version=0,startup=0,seen=0;
    if(!json_unique_object(json) || integer(json,"version",1,1,&version) || integer(json,"startup",0,8,&startup) || !cJSON_IsArray(slots) || cJSON_GetArraySize(slots)>8) goto bad;
    cJSON *v;cJSON_ArrayForEach(v,slots) {
        int id,pan,tilt;const cJSON *name=cJSON_GetObjectItemCaseSensitive(v,"name");
        if(!json_unique_object(v) || integer(v,"id",1,8,&id) || integer(v,"pan",0,4039,&pan) || integer(v,"tilt",0,967,&tilt) || !cJSON_IsString(name) || !valid_name(name->valuestring) || (seen&(1<<id))) goto bad;
        seen|=1<<id;
    }
    if(startup && !(seen&(1<<startup))) goto bad;
    return json;
bad:cJSON_Delete(json);return NULL;
}
static int run_motor(const char *action,int pan,int tilt,char output[513],int client,const char *token)
{
    char helper[600],x[16],y[16];
    const char *assets=*gateway.assets?gateway.assets:"/mnt/openipc-persist/sdk-rtsp";
    if(snprintf(helper,sizeof(helper),"%s/anyka-ptz",assets)>=(int)sizeof(helper)) return 1;
    snprintf(x,sizeof(x),"%d",pan);snprintf(y,sizeof(y),"%d",tilt);
    char *argv[]={"/media/lib/ld-musl-arm.so.1","--library-path","/media/lib:/media/usr/lib",helper,(char *)action,x,y,NULL};
    if(strcmp(action,"move-to")) argv[5]=NULL;
    int pipes[2];if(pipe2(pipes,O_CLOEXEC|O_NONBLOCK)) return 1;
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,pipes[1],STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
    posix_spawn_file_actions_addclose(&actions,pipes[0]);posix_spawn_file_actions_addclose(&actions,pipes[1]);
    pid_t pid;int error=posix_spawn(&pid,argv[0],&actions,NULL,argv,environ);
    posix_spawn_file_actions_destroy(&actions);close(pipes[1]);if(error) {close(pipes[0]);return 1;}
    uint64_t until=monotonic_ms()+(!strcmp(action,"move-to")?65000:4000);size_t used=0;int status=0,cancelled=0;
    for(;;) {
        ssize_t n=read(pipes[0],output+used,512-used);
        if(n>0) used+=(size_t)n;
        if(waitpid(pid,&status,WNOHANG)==pid) break;
        int disconnected=0;
        if(client>=0) {char byte;disconnected=recv(client,&byte,1,MSG_PEEK|MSG_DONTWAIT)==0;}
        if(stopping || monotonic_ms()>=until || used==512 || disconnected || (token && !session_valid(token))) {
            kill(pid,SIGTERM);while(waitpid(pid,&status,0)<0 && errno==EINTR) {} cancelled=1;break;
        }
        struct pollfd p={pipes[0],POLLIN,0};poll(&p,1,50);
    }
    ssize_t n;while(used<512 && (n=read(pipes[0],output+used,512-used))>0) used+=(size_t)n;
    close(pipes[0]);output[used]=0;
    return cancelled?5:WIFEXITED(status)?WEXITSTATUS(status):1;
}
static cJSON *position(int client,const char *token)
{
    char data[513];if(run_motor("status",0,0,data,client,token)) return NULL;
    cJSON *json=json_parse(data);int pan=0,tilt=0;
    if(!json_unique_object(json) || integer(json,"pan",0,4039,&pan) || integer(json,"tilt",0,967,&tilt)) {cJSON_Delete(json);return NULL;}
    return json;
}
static void json_reply(int fd,cJSON *json)
{
    char *data=cJSON_PrintUnformatted(json);
    if(data) {http_reply(fd,200,"application/json",data,strlen(data),NULL);free(data);}
    else http_error(fd,503,"Cannot prepare positions");
}
void ptz_presets_http(int fd,const struct request *r)
{
    char token[65];if(session_token(r,token)) {http_error(fd,401,"Session required");return;}
    if(*r->query || (strcmp(r->method,"GET") && strcmp(r->method,"POST"))) {http_error(fd,405,"Use GET or POST without query");return;}
    char body[1025]="",action[32]="",id_text[16]="",name[64]="";long id=0;
    if(!strcmp(r->method,"POST")) {
        if(!r->content_length || r->content_length>1024 || strcmp(header(r,"Content-Type"),"application/x-www-form-urlencoded") || transfer_read(fd,body,r->content_length,monotonic_ms()+3000)) {http_error(fd,400,"Expected position form");return;}
        if(memchr(body,0,r->content_length) || form_value(body,"action",action,sizeof(action)) || form_value(body,"id",id_text,sizeof(id_text)) || decimal(id_text,0,8,&id)) {http_error(fd,400,"Invalid position action");return;}
        if(strcmp(action,"startup") && id==0) {http_error(fd,400,"Choose position 1 to 8");return;}
        if(strcmp(action,"save") && strcmp(action,"delete") && strcmp(action,"recall") && strcmp(action,"startup")) {http_error(fd,400,"Invalid position action");return;}
        if(!strcmp(action,"save") && (form_value(body,"name",name,sizeof(name)) || !valid_name(name))) {http_error(fd,400,"Position name required (48 bytes maximum)");return;}
    }
    pthread_mutex_lock(&presets_lock);
    cJSON *json=load_presets();if(!json) {pthread_mutex_unlock(&presets_lock);http_error(fd,503,"Saved positions are invalid or unavailable");return;}
    cJSON *slot=find_slot(json,(int)id);
    if(*action && strcmp(action,"save") && id && !slot) {cJSON_Delete(json);pthread_mutex_unlock(&presets_lock);http_error(fd,404,"Position not saved");return;}
    if(!strcmp(action,"recall")) {
        int pan=0,tilt=0;integer(slot,"pan",0,4039,&pan);integer(slot,"tilt",0,967,&tilt);
        cJSON_Delete(json);pthread_mutex_unlock(&presets_lock);
        char output[513];int result=run_motor("move-to",pan,tilt,output,fd,token);
        if(result) http_error(fd,result==5?409:503,result==5?"Movement interrupted":"Cannot reach saved position");
        else http_reply(fd,200,"application/json",output,strlen(output),NULL);
        return;
    }
    if(!strcmp(action,"save")) {
        cJSON *current=position(fd,token);int pan=0,tilt=0;
        if(!current || access("/tmp/anyka-ptz-homed",F_OK)) {cJSON_Delete(current);cJSON_Delete(json);pthread_mutex_unlock(&presets_lock);http_error(fd,409,"Motor busy or not calibrated");return;}
        integer(current,"pan",0,4039,&pan);integer(current,"tilt",0,967,&tilt);cJSON_Delete(current);
        if(slot) {cJSON_DetachItemViaPointer(cJSON_GetObjectItemCaseSensitive(json,"positions"),slot);cJSON_Delete(slot);}
        slot=cJSON_CreateObject();cJSON_AddNumberToObject(slot,"id",id);cJSON_AddStringToObject(slot,"name",name);cJSON_AddNumberToObject(slot,"pan",pan);cJSON_AddNumberToObject(slot,"tilt",tilt);cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(json,"positions"),slot);
    } else if(!strcmp(action,"delete")) {
        cJSON_DetachItemViaPointer(cJSON_GetObjectItemCaseSensitive(json,"positions"),slot);cJSON_Delete(slot);
        int startup=0;integer(json,"startup",0,8,&startup);if(startup==id) cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(json,"startup"),0);
    } else if(!strcmp(action,"startup")) cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(json,"startup"),id);
    if(*action) {
        char path[528],*data=cJSON_PrintUnformatted(json);int bad=!data || preset_path(path) || private_write(path,data);free(data);
        if(bad) {cJSON_Delete(json);pthread_mutex_unlock(&presets_lock);http_error(fd,503,"Position could not be saved durably");return;}
    }
    cJSON *current=position(fd,token);cJSON_AddItemToObject(json,"current",current?current:cJSON_CreateNull());
    cJSON_AddBoolToObject(json,"homed",!access("/tmp/anyka-ptz-homed",F_OK));
    json_reply(fd,json);cJSON_Delete(json);pthread_mutex_unlock(&presets_lock);
}
int ptz_startup(void)
{
    umask(0077);
    if(access("/tmp/anyka-ptz-homed",F_OK)) return 1;
    if(!access("/tmp/anyka-ptz-startup-done",F_OK)) return 0;
    cJSON *json=load_presets();if(!json) return 1;
    int id=0;integer(json,"startup",0,8,&id);cJSON *slot=find_slot(json,id);
    int marker=open("/tmp/anyka-ptz-startup-done",O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);
    if(marker<0) {cJSON_Delete(json);return errno==EEXIST?0:1;} close(marker);
    char output[513];int result=0;
    if(slot) {int pan=0,tilt=0;integer(slot,"pan",0,4039,&pan);integer(slot,"tilt",0,967,&tilt);result=run_motor("move-to",pan,tilt,output,-1,NULL);}
    cJSON_Delete(json);
    /* Attempt once per boot, including Stop or failure: no repeated movement on UI restart. */
    return result;
}
