/* One bounded browser reader of the vendor loopback RTSP substream.
 * H.264 is repackaged only; PCMA is expanded losslessly to FLAC verbatim.
 * No public RTSP relay, transcoder, retained frame queue or credentials. */
#include "gateway.h"
#include "mux.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <mbedtls/base64.h>

#define FRAME_MAX (512*1024)
#define RTSP_BASE "rtsp://127.0.0.1:8555/vs1"
struct preview {
    int web,rtsp,audio,started,idr,bad,fu,have_sequence,video_pt,audio_pt;
    unsigned cseq;uint16_t sequence;
    char session[128],control[2][256];
    unsigned char sps[1024],pps[1024],*frame;
    size_t ns,np,used,fu_start;
    uint32_t timestamp,origin,audio_origin,index;
    int have_audio_origin;
    uint64_t video_dts,audio_dts,samples;
    struct mp4 mux;
};
void cancel_preview_locked(void)
{
    if(gateway.preview_fd>=0) shutdown(gateway.preview_fd,SHUT_RDWR);
    if(gateway.preview_client>=0) shutdown(gateway.preview_client,SHUT_RDWR);
}
static int preview_memory(unsigned minimum_kib)
{
    char text[4098];unsigned available;
    if(read_small("/proc/meminfo",text,4096,0)<0) return 0;
    char *p=strstr(text,"MemAvailable:");
    return p && sscanf(p,"MemAvailable: %u kB",&available)==1 && available>=minimum_kib;
}
static int reply_value(const char *text,const char *name,char *out,size_t capacity)
{
    size_t n=strlen(name);
    for(const char *p=strstr(text,"\r\n");p && p[2];p=strstr(p+2,"\r\n")) {
        p+=2;if(!strncasecmp(p,name,n) && p[n]==':') {
            p+=n+1;while(*p==' ' || *p=='\t') p++;
            const char *end=strstr(p,"\r\n");if(!end || (size_t)(end-p)>=capacity) return -1;
            memcpy(out,p,(size_t)(end-p));out[end-p]=0;return 0;
        }
        p-=2;
    }
    *out=0;return 0;
}
static int rtsp_request(struct preview *p,const char *method,const char *uri,const char *extra,char *body,size_t capacity)
{
    char text[8193],length[32],session[128];
    int n=snprintf(text,sizeof(text),"%s %s RTSP/1.0\r\nCSeq: %u\r\n%s%s%s%s\r\n",method,uri,++p->cseq,
        *p->session?"Session: ":"",p->session,*p->session?"\r\n":"",extra?extra:"");
    uint64_t deadline=monotonic_ms()+5000;
    if(n<0 || n>=(int)sizeof(text) || transfer_write(p->rtsp,text,(size_t)n,deadline) ||
        receive_header(p->rtsp,text,sizeof(text),deadline)<0 || strncmp(text,"RTSP/1.0 200 ",13)) return -1;
    if(reply_value(text,"Content-Length",length,sizeof(length)) || reply_value(text,"Session",session,sizeof(session))) return -1;
    if(*session) {
        char *semi=strchr(session,';');if(semi) *semi=0;
        for(char *v=session;*v;v++) if(!((*v>='0' && *v<='9') || (*v>='a' && *v<='z') || (*v>='A' && *v<='Z') || *v=='-' || *v=='_')) return -1;
        strcpy(p->session,session);
    }
    long size=0;if(*length && decimal(length,0,(long)capacity-1,&size)) return -1;
    if(transfer_read(p->rtsp,body,(size_t)size,deadline)) return -1;
    body[size]=0;return 0;
}
static int sdp_read(struct preview *p,char *body)
{
    int track=-1;p->video_pt=-1;p->audio_pt=-1;
    char *save=NULL;
    for(char *line=strtok_r(body,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)) {
        if(!strncmp(line,"m=",2)) {
            int pt;
            if(sscanf(line,"m=video %*u RTP/AVP %d",&pt)==1 && pt>=96 && pt<=127) {track=0;p->video_pt=pt;}
            else if(sscanf(line,"m=audio %*u RTP/AVP %d",&pt)==1 && pt==8) {track=1;p->audio_pt=8;}
            else track=-1;
        } else if(track>=0 && !strncmp(line,"a=control:",10)) {
            const char *value=line+10;int n;
            if(strstr(value,"://")) {
                if(strncmp(value,RTSP_BASE "/",sizeof(RTSP_BASE))) return -1;
                n=snprintf(p->control[track],sizeof(p->control[track]),"%s",value);
            } else {
                if(!*value || strspn(value,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789=/_-")!=strlen(value)) return -1;
                n=snprintf(p->control[track],sizeof(p->control[track]),RTSP_BASE "/%s",value);
            }
            if(n<0 || n>=(int)sizeof(p->control[track])) return -1;
        } else if(track==0 && strstr(line,"sprop-parameter-sets=")) {
            char *s=strstr(line,"sprop-parameter-sets=")+21,*comma=strchr(s,',');
            if(!comma) return -1;
            char *end=strchr(comma+1,';');if(!end) end=comma+strlen(comma);
            if(mbedtls_base64_decode(p->sps,sizeof(p->sps),&p->ns,(unsigned char *)s,(size_t)(comma-s)) ||
                mbedtls_base64_decode(p->pps,sizeof(p->pps),&p->np,(unsigned char *)comma+1,(size_t)(end-comma-1))) return -1;
        }
    }
    return p->video_pt<0 || !*p->control[0]?-1:0;
}
static int fragment(struct preview *p,unsigned track,uint32_t duration,const void *data,size_t size,int key,uint64_t dts)
{
    if(mp4_fragment(&p->mux,++p->index,track,duration,size,key,dts)) return -1;
    /* Send moof+mdat header and payload as successive binary messages, which
     * VideoRTC appends to its SourceBuffer. No extra full-frame allocation. */
    return websocket_send(p->web,2,p->mux.data,p->mux.used) || websocket_send(p->web,2,data,size)?-1:0;
}
static int video_frame(struct preview *p)
{
    if(p->bad || p->fu || !p->used) {p->started=0;return 0;}
    if(!p->started) {
        if(!p->idr || !p->ns || !p->np) return 0;
        if(p->index) return -1; /* Never stitch a damaged GOP into old timestamps. */
        if(mp4_init(&p->mux,p->sps,p->ns,p->pps,p->np,p->audio)) return -1;
        char message[160];int n=snprintf(message,sizeof(message),"{\"type\":\"mse\",\"value\":\"video/mp4; codecs=\\\"avc1.%02x%02x%02x%s\\\"\"}",p->sps[1],p->sps[2],p->sps[3],p->audio?",flac":"");
        if(n<=0 || n>=(int)sizeof(message) || websocket_send(p->web,1,message,(size_t)n) || websocket_send(p->web,2,p->mux.data,p->mux.used)) return -1;
        p->origin=p->timestamp;p->started=1;
    }
    uint32_t relative=p->timestamp-p->origin;
    if(relative>90000u*60*60*8 || (p->index && relative<p->video_dts)) return -1;
    p->video_dts=relative;
    return fragment(p,1,4500,p->frame,p->used,p->idr,p->video_dts);
}
static int nal(struct preview *p,const unsigned char *data,size_t n)
{
    if(!n || data[0]&128) return -1;
    unsigned type=data[0]&31;
    if(type==7 || type==8) {
        unsigned char *dest=type==7?p->sps:p->pps;size_t *size=type==7?&p->ns:&p->np;
        if(n>1024 || (type==7 && n<4) || (p->started && (*size!=n || memcmp(dest,data,n)))) return -1;
        memcpy(dest,data,n);*size=n;return 0;
    }
    if(type<1 || type>23 || p->used>FRAME_MAX-4 || n>FRAME_MAX-p->used-4) return -1;
    p->frame[p->used++]=(unsigned char)(n>>24);p->frame[p->used++]=(unsigned char)(n>>16);
    p->frame[p->used++]=(unsigned char)(n>>8);p->frame[p->used++]=(unsigned char)n;
    memcpy(p->frame+p->used,data,n);p->used+=n;if(type==5) p->idr=1;return 0;
}
static int video_packet(struct preview *p,const unsigned char *data,size_t n,uint16_t seq,uint32_t timestamp,int marker)
{
    if(p->have_sequence && seq!=(uint16_t)(p->sequence+1)) return -1;
    p->have_sequence=1;p->sequence=seq;
    if(timestamp!=p->timestamp) {
        if(p->used && video_frame(p)) return -1;
        p->used=0;p->idr=0;p->bad=0;p->fu=0;p->timestamp=timestamp;
    }
    if(!n) return -1;
    unsigned type=data[0]&31;
    if(type>=1 && type<=23) {if(p->fu || nal(p,data,n)) return -1;}
    else if(type==24) {
        if(p->fu) return -1;
        size_t pos=1;while(pos+2<=n) {
            size_t size=((size_t)data[pos]<<8)|data[pos+1];pos+=2;
            if(!size || size>n-pos || nal(p,data+pos,size)) return -1;
            pos+=size;
        }
        if(pos!=n) return -1;
    } else if(type==28) {
        if(n<3 || data[1]&32 || (data[1]&31)<1 || (data[1]&31)>23) return -1;
        if(data[1]&128) {
            if(p->fu || p->used>FRAME_MAX-5) return -1;
            p->fu=1;p->fu_start=p->used;p->used+=4;
            p->frame[p->used++]=(unsigned char)((data[0]&224)|(data[1]&31));
            if((data[1]&31)==5) p->idr=1;
        } else if(!p->fu) {p->bad=1;return 0;}
        if(n-2>FRAME_MAX-p->used) return -1;
        memcpy(p->frame+p->used,data+2,n-2);p->used+=n-2;
        if(data[1]&64) {
            size_t size=p->used-p->fu_start-4;
            for(unsigned i=0;i<4;i++) p->frame[p->fu_start+i]=(unsigned char)(size>>(24-8*i));
            p->fu=0;
        }
    } else return -1;
    if(marker) {int result=video_frame(p);p->used=0;p->idr=0;p->bad=0;return result;}
    return 0;
}
static int media(struct preview *p,const unsigned char *data,size_t n,unsigned channel)
{
    if(channel!=0 && channel!=2) return 0;
    if(n<12 || (data[0]>>6)!=2) return -1;
    size_t pos=12+4*(data[0]&15);if(pos>n) return -1;
    if(data[0]&16) {
        if(n-pos<4) return -1;
        size_t extension=4+4*(((size_t)data[pos+2]<<8)|data[pos+3]);if(extension>n-pos) return -1;pos+=extension;
    }
    if(data[0]&32) {if(!data[n-1] || data[n-1]>n-pos) return -1;n-=data[n-1];}
    if(pos>=n) return -1;
    uint32_t timestamp=((uint32_t)data[4]<<24)|((uint32_t)data[5]<<16)|((uint32_t)data[6]<<8)|data[7];
    if(channel==0) {
        if((data[1]&127)!=p->video_pt) return -1;
        return video_packet(p,data+pos,n-pos,(uint16_t)((data[2]<<8)|data[3]),timestamp,data[1]&128);
    }
    if(!p->audio || !p->started) return 0;
    if((data[1]&127)!=8 || n-pos>1280) return -1;
    if(!p->have_audio_origin) {p->audio_origin=timestamp;p->audio_dts=p->video_dts*8000/90000;p->have_audio_origin=1;}
    unsigned char flac[2600];size_t size=flac_pcma(flac,data+pos,n-pos,p->samples);p->samples+=n-pos;
    uint32_t relative=timestamp-p->audio_origin;
    if(relative>8000u*60*60*8 || !size) return -1;
    return fragment(p,2,(uint32_t)(n-pos),flac,size,1,p->audio_dts+relative);
}
void preview_live(int fd,const struct request *r)
{
    char origin[160],token[65];snprintf(origin,sizeof(origin),"http://%s",header(r,"Host"));
    if(strcmp(r->method,"GET") || strcmp(r->query,"src=preview") || strcmp(header(r,"Origin"),origin) || session_token(r,token)) {http_error(fd,403,"Same-origin preview required");return;}
    if(!preview_memory(4608)) {http_error(fd,503,"Insufficient memory for browser preview");return;}
    if(pthread_mutex_trylock(&gateway.preview_lock)) {http_error(fd,409,"One browser preview at a time");return;}
    struct preview *p=calloc(1,sizeof(*p));if(!p) goto release;
    p->web=fd;p->rtsp=-1;
    pthread_mutex_lock(&gateway.lock);gateway.preview_client=fd;uint64_t generation=gateway.generation;pthread_mutex_unlock(&gateway.lock);
    if(privacy_enabled() || !session_valid(token)) goto done;
    if(websocket_handshake(fd,r)) {http_error(fd,400,"Invalid WebSocket handshake");goto done;}
    unsigned char message[1281];size_t length;
    if(websocket_receive(fd,message,&length,monotonic_ms()+5000)!=1 || memchr(message,0,length)) goto done;
    cJSON *json=json_parse((char *)message);
    const cJSON *type=cJSON_GetObjectItemCaseSensitive(json,"type"),*value=cJSON_GetObjectItemCaseSensitive(json,"value");
    int valid=json_unique_object(json) && cJSON_GetArraySize(json)==2 && cJSON_IsString(type) && !strcmp(type->valuestring,"mse") && cJSON_IsString(value) && strstr(value->valuestring,"avc1.");
    if(valid) p->audio=strstr(value->valuestring,"flac")!=NULL;
    cJSON_Delete(json);if(!valid) goto done;
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(8555),.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    p->rtsp=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(p->rtsp<0) goto done;
    pthread_mutex_lock(&gateway.lock);gateway.preview_fd=p->rtsp;pthread_mutex_unlock(&gateway.lock);
    if(connect(p->rtsp,(struct sockaddr *)&address,sizeof(address)) && errno!=EINPROGRESS) goto done;
    char body[8193];
    if(rtsp_request(p,"DESCRIBE",RTSP_BASE,"Accept: application/sdp\r\n",body,sizeof(body)) || sdp_read(p,body) ||
        rtsp_request(p,"SETUP",p->control[0],"Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n",body,sizeof(body))) goto done;
    p->audio=p->audio && p->audio_pt==8 && *p->control[1];
    if(p->audio && rtsp_request(p,"SETUP",p->control[1],"Transport: RTP/AVP/TCP;unicast;interleaved=2-3\r\n",body,sizeof(body))) goto done;
    if(rtsp_request(p,"PLAY",RTSP_BASE,"Range: npt=0.000-\r\n",body,sizeof(body))) goto done;
    p->frame=malloc(FRAME_MAX);unsigned char *packet=malloc(65536);if(!packet || !p->frame) {free(packet);goto done;}
    uint64_t keepalive=monotonic_ms()+20000,started=monotonic_ms(),memory_check=started+1000;
    while(!stopping && session_valid(token) && !privacy_enabled()) {
        pthread_mutex_lock(&gateway.lock);int revoked=gateway.generation!=generation;pthread_mutex_unlock(&gateway.lock);if(revoked) break;
        struct pollfd fds[2]={{p->rtsp,POLLIN,0},{fd,POLLIN,0}};
        int ready=poll(fds,2,250);if(ready<0 && errno==EINTR) continue;if(ready<0) break;
        if(fds[1].revents) {size_t n;if(websocket_receive(fd,message,&n,monotonic_ms()+1000)<0) break;}
        if(fds[0].revents) {
            unsigned char head[4];if(transfer_read(p->rtsp,head,4,monotonic_ms()+2000)) break;
            if(head[0]=='$') {
                size_t n=((size_t)head[2]<<8)|head[3];
                if(!n || transfer_read(p->rtsp,packet,n,monotonic_ms()+2000) || media(p,packet,n,head[1])) break;
            } else {
                memcpy(body,head,4);size_t n=4;
                while(n+1<sizeof(body) && (n<4 || memcmp(body+n-4,"\r\n\r\n",4))) {
                    if(transfer_read(p->rtsp,body+n,1,monotonic_ms()+1000)) break;
                    n++;
                }
                body[n]=0;char size[32];long count=0;
                if(n+1>=sizeof(body) || strncmp(body,"RTSP/1.0 200 ",13) || reply_value(body,"Content-Length",size,sizeof(size)) ||
                    (*size && decimal(size,0,8192,&count)) || transfer_read(p->rtsp,body,(size_t)count,monotonic_ms()+2000)) break;
            }
        }
        if(!p->started && monotonic_ms()-started>10000) break;
        if(monotonic_ms()>=memory_check) {
            if(!preview_memory(4096)) break;
            memory_check=monotonic_ms()+1000;
        }
        if(monotonic_ms()>=keepalive) {
            int n=snprintf(body,sizeof(body),"OPTIONS " RTSP_BASE " RTSP/1.0\r\nCSeq: %u\r\nSession: %s\r\n\r\n",++p->cseq,p->session);
            if(n<=0 || n>=(int)sizeof(body) || transfer_write(p->rtsp,body,(size_t)n,monotonic_ms()+1000)) break;
            keepalive=monotonic_ms()+20000;
        }
    }
    free(packet);
done:
    pthread_mutex_lock(&gateway.lock);gateway.preview_fd=-1;gateway.preview_client=-1;pthread_mutex_unlock(&gateway.lock);
    if(p->rtsp>=0) close(p->rtsp);
    free(p->frame);free(p);
release:
    pthread_mutex_unlock(&gateway.preview_lock);
}
