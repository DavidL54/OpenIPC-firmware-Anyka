#include "gateway.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

struct options { int volume,rate,nr,mp3,source_rate,source_channels; };
static int audio_options(const char *query,struct options *o,int live)
{
    *o=(struct options){.rate=8000,.source_rate=8000,.source_channels=1};
    unsigned seen=0;
    for(const char *p=query;*p;) {
        const char *end=strchr(p,'&');if(!end) end=p+strlen(p);
        const char *eq=memchr(p,'=',(size_t)(end-p));char key[32],value[32];
        if(!eq || decode_component(p,(size_t)(eq-p),key,sizeof(key)) || decode_component(eq+1,(size_t)(end-eq-1),value,sizeof(value))) return -1;
        unsigned bit=0;long number;
        if(!strcmp(key,"volume")) {bit=1;if(decimal(value,-30,0,&number)) return -1;o->volume=(int)number;}
        else if(live && !strcmp(key,"rate")) {bit=2;if(strcmp(value,"8000")) return -1;}
        else if(live && !strcmp(key,"nr")) {bit=4;if(decimal(value,0,1,&number)) return -1;o->nr=(int)number;}
        else if(live && !strcmp(key,"format")) {bit=8;if(strcmp(value,"pcm") && strcmp(value,"mp3")) return -1;o->mp3=!strcmp(value,"mp3");}
        else if(live && !strcmp(key,"source_rate")) {
            bit=16;if(decimal(value,8000,48000,&number)) return -1;
            if(number!=8000 && number!=11025 && number!=12000 && number!=16000 && number!=22050 && number!=24000 && number!=32000 && number!=44100 && number!=48000) return -1;
            o->source_rate=(int)number;
        } else if(live && !strcmp(key,"source_channels")) {bit=32;if(decimal(value,1,2,&number)) return -1;o->source_channels=(int)number;}
        else return -1;
        if(seen&bit) return -1;
        seen|=bit;p=*end?end+1:end;
        if(*end && !*p) return -1;
    }
    return !(seen&1) || (o->mp3 && (seen&48)!=48) ? -1:0;
}
int websocket_send(int fd,int opcode,const void *data,size_t size)
{
    unsigned char head[10];head[0]=(unsigned char)(0x80|opcode);size_t length;
    if(size>512*1024+4096 || (opcode>=8 && size>125)) return -1;
    if(size<126) {head[1]=(unsigned char)size;length=2;}
    else if(size<=65535) {head[1]=126;head[2]=(unsigned char)(size>>8);head[3]=(unsigned char)size;length=4;}
    else {head[1]=127;for(unsigned i=0;i<8;i++) head[2+i]=(unsigned char)((uint64_t)size>>(56-8*i));length=10;}
    uint64_t deadline=monotonic_ms()+1000;
    return transfer_write(fd,head,length,deadline) || transfer_write(fd,data,size,deadline) ? -1:0;
}
static int report(int fd,const char *kind,const char *message)
{
    char text[256];int n=snprintf(text,sizeof(text),"{\"type\":\"%s\",\"value\":\"%s\"}",kind,message);
    if(n<0 || n>=(int)sizeof(text)) return -1;
    return websocket_send(fd,1,text,(size_t)n);
}
/* Client messages are masked and <=1280 bytes. Continuations and interleaved
 * ping/pong are accepted without allocating a message queue. */
int websocket_receive(int fd,unsigned char out[1281],size_t *size,uint64_t deadline)
{
    int message=0;size_t used=0;unsigned controls=0;
    for(;;) {
        unsigned char h[4],mask[4],control[125];
        if(transfer_read(fd,h,2,deadline) || h[0]&0x70 || !(h[1]&0x80)) return -1;
        int opcode=h[0]&15,fin=h[0]&0x80;size_t length=h[1]&127;
        if(length==127) return -1;
        if(length==126) {
            if(transfer_read(fd,h+2,2,deadline)) return -1;
            length=((size_t)h[2]<<8)|h[3];if(length<126) return -1;
        }
        if(opcode>=8) {
            if(!fin || length>125 || ++controls>16 || (opcode!=8 && opcode!=9 && opcode!=10) || (opcode==8 && length==1)) return -1;
        } else if(opcode==0) {if(!message) return -1;}
        else if((opcode!=1 && opcode!=2) || message) return -1;
        else message=opcode;
        if(length>1280-used || transfer_read(fd,mask,4,deadline)) return -1;
        unsigned char *payload=opcode>=8?control:out+used;
        if(transfer_read(fd,payload,length,deadline)) return -1;
        for(size_t i=0;i<length;i++) payload[i]^=mask[i%4];
        if(opcode>=8) {
            if(opcode==8) {websocket_send(fd,8,payload,length);return -1;}
            if(opcode==9 && websocket_send(fd,10,payload,length)) return -1;
            continue;
        }
        used+=length;
        if(fin) {out[used]=0;*size=used;return message;}
    }
}
int websocket_handshake(int fd,const struct request *r)
{
    const char *key=header(r,"Sec-WebSocket-Key"),*connection=header(r,"Connection");
    unsigned char decoded[32],digest[20],accept[32];size_t length;
    char combined[128],response[256];
    if(strcmp(header(r,"Sec-WebSocket-Version"),"13") || strcasecmp(header(r,"Upgrade"),"websocket") ||
        !strcasestr(connection,"upgrade") || strlen(key)!=24 || *header(r,"Sec-WebSocket-Protocol") ||
        mbedtls_base64_decode(decoded,sizeof(decoded),&length,(const unsigned char *)key,24) || length!=16) return -1;
    snprintf(combined,sizeof(combined),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
    if(mbedtls_sha1((const unsigned char *)combined,strlen(combined),digest) ||
        mbedtls_base64_encode(accept,sizeof(accept),&length,digest,sizeof(digest))) return -1;
    int n=snprintf(response,sizeof(response),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);
    if(n<=0 || n>=(int)sizeof(response)) return -1;
    return transfer_write(fd,response,(size_t)n,monotonic_ms()+1000);
}
static int enough_memory(void)
{
    char text[4098];if(read_small("/proc/meminfo",text,4096,0)<0) return 0;
    char *p=strstr(text,"MemAvailable:");unsigned long available;
    return p && sscanf(p,"MemAvailable: %lu kB",&available)==1 && available>=4608;
}
static int audio_begin(int fd,uint64_t *generation)
{
    if(pthread_mutex_trylock(&gateway.audio_lock)) return -1;
    if(privacy_enabled() || !enough_memory()) {pthread_mutex_unlock(&gateway.audio_lock);return -1;}
    pthread_mutex_lock(&gateway.lock);
    *generation=++gateway.audio_generation;gateway.audio_client=fd;
    pthread_mutex_unlock(&gateway.lock);return 0;
}
static void audio_release(void)
{
    pthread_mutex_lock(&gateway.lock);
    int sdk=gateway.audio_fd;gateway.audio_fd=-1;gateway.audio_client=-1;
    pthread_mutex_unlock(&gateway.lock);
    if(sdk>=0) close(sdk);
    pthread_mutex_unlock(&gateway.audio_lock);
}
int audio_session_alive(const char *token,uint64_t generation,uint64_t deadline)
{
    pthread_mutex_lock(&gateway.lock);
    int ok=generation==gateway.audio_generation && !gateway.privacy;
    pthread_mutex_unlock(&gateway.lock);
    return ok && monotonic_ms()<deadline && !privacy_enabled() && session_valid(token);
}
static int speaker_open(const struct options *o,uint64_t generation)
{
    int fd=connect_unix(AUDIO_SOCKET,1000);if(fd<0) return -1;
    pthread_mutex_lock(&gateway.lock);
    int revoked=generation!=gateway.audio_generation || gateway.privacy;
    if(!revoked) gateway.audio_fd=fd;
    pthread_mutex_unlock(&gateway.lock);
    if(revoked) {close(fd);return -1;}
    unsigned char head[24]={0},ack;
    memcpy(head,o->mp3?"MP02":"AO02",4);
    uint32_t words[5]={htonl((uint32_t)o->volume),htonl(8000),htonl((uint32_t)o->nr),htonl((uint32_t)o->source_rate),htonl((uint32_t)o->source_channels)};
    memcpy(head+4,words,sizeof(words));uint64_t deadline=monotonic_ms()+3000;
    if(transfer_write(fd,head,o->mp3?24:16,deadline) || transfer_read(fd,&ack,1,deadline) || ack) return -1;
    return fd;
}
static int speaker_frame(int fd,const void *data,size_t size,unsigned timeout)
{
    unsigned char frame[1282],ack;
    if(size>1280) return -1;
    frame[0]=(unsigned char)(size>>8);frame[1]=(unsigned char)size;
    if(size) memcpy(frame+2,data,size);
    uint64_t deadline=monotonic_ms()+(size?timeout:4000);
    return transfer_write(fd,frame,size+2,deadline) || transfer_read(fd,&ack,1,deadline) || ack ? -1:0;
}
void audio_live(int fd,const struct request *r)
{
    struct options options;char expected[160],token[65];
    snprintf(expected,sizeof(expected),"http://%s",header(r,"Host"));
    if(strcmp(r->method,"GET") || strcmp(expected,header(r,"Origin")) || audio_options(r->query,&options,1) || session_token(r,token)) {
        http_error(fd,403,"Valid same-origin audio options required");return;
    }
    uint64_t generation;
    if(audio_begin(fd,&generation)) {http_error(fd,409,"Speaker busy, private, or insufficient memory");return;}
    if(websocket_handshake(fd,r)) {http_error(fd,400,"Invalid WebSocket handshake");audio_release();return;}
    int receive_buffer=2048;setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&receive_buffer,sizeof(receive_buffer));
    int sdk=speaker_open(&options,generation);
    if(sdk<0) {report(fd,"error","Native speaker unavailable");audio_release();return;}
    if(report(fd,"ready","PCM mono 8000 Hz signed 16-bit little-endian (native SDK)")) {audio_release();return;}
    uint64_t started=monotonic_ms(),deadline=started+300000,received=0;
    unsigned char data[1281];size_t length;
    while(audio_session_alive(token,generation,deadline)) {
        uint64_t read_deadline=monotonic_ms()+3000;if(read_deadline>deadline) read_deadline=deadline;
        int kind=websocket_receive(fd,data,&length,read_deadline);
        if(kind<0) break;
        if(!audio_session_alive(token,generation,deadline)) {report(fd,"error","Session expired or playback stopped");break;}
        if(kind==1 && length==4 && !memcmp(data,"stop",4)) break;
        if(kind==1 && length==3 && !memcmp(data,"end",3)) {
            if(speaker_frame(sdk,NULL,0,4000)) report(fd,"error","Speaker drain failed");
            else report(fd,"ended","Playback completed");
            break;
        }
        if(kind!=2 || !length || length>1280 || (!options.mp3 && length%2)) {report(fd,"error","Invalid audio packet");break;}
        received+=length;
        if((options.mp3 && received>20*1024*1024) || (!options.mp3 && received>(monotonic_ms()-started)*16+2560)) {
            report(fd,"error","Audio arrives faster than real time");break;
        }
        if(speaker_frame(sdk,data,length,options.mp3?3000:1000)) {report(fd,"error","Native speaker stopped or stalled");break;}
        if(options.mp3 && report(fd,"credit","Next MP3 chunk")) break;
    }
    websocket_send(fd,8,"",0);audio_release();
}
void audio_post(int fd,const struct request *r)
{
    if(strcmp(r->method,"POST")) {http_error(fd,405,"Use POST");return;}
    if(!strcmp(r->path,"/api/audio/stop")) {
        pthread_mutex_lock(&gateway.lock);cancel_audio_locked();pthread_mutex_unlock(&gateway.lock);
        http_reply(fd,204,"application/json","",0,NULL);return;
    }
    struct options options;char token[65];
    if(audio_options(r->query,&options,0) || strcmp(header(r,"Content-Type"),"application/octet-stream") ||
        !r->content_length || r->content_length>480000 || r->content_length%2 || session_token(r,token)) {
        http_error(fd,400,"Use bounded mono 8000 Hz signed 16-bit PCM");return;
    }
    uint64_t generation;
    if(audio_begin(fd,&generation)) {http_error(fd,409,"Speaker busy, private, or insufficient memory");return;}
    char name[]="/tmp/anyka-vocal-XXXXXX";int file=mkostemp(name,O_CLOEXEC),error=0;
    if(file<0) {audio_release();http_error(fd,503,"Cannot prepare speaker playback");return;}
    unlink(name);
    size_t remaining=r->content_length;unsigned char data[4096];uint64_t until=monotonic_ms()+10000;
    while(remaining) {
        size_t take=remaining>sizeof(data)?sizeof(data):remaining;
        if(transfer_read(fd,data,take,until) || write(file,data,take)!=(ssize_t)take) {error=1;break;}
        remaining-=take;
    }
    if(lseek(file,0,SEEK_SET)<0) error=1;
    uint64_t started=monotonic_ms(),deadline=started+40000;int sdk=-1;
    if(!error && audio_session_alive(token,generation,deadline)) sdk=speaker_open(&options,generation);
    if(sdk<0) error=1;
    uint64_t sent=0;
    while(!error && remaining<r->content_length) {
        if(!audio_session_alive(token,generation,deadline)) {error=1;break;}
        ssize_t n=read(file,data,640);
        if(n<0 && errno==EINTR) continue;
        if(n<0) {error=1;break;}
        if(!n) {error=speaker_frame(sdk,NULL,0,4000);break;}
        if(speaker_frame(sdk,data,(size_t)n,1000)) {error=1;break;}
        sent+=(size_t)n;
        uint64_t scheduled=started+sent/16,now=monotonic_ms();
        if(scheduled>now) usleep((useconds_t)(scheduled-now)*1000);
    }
    close(file);audio_release();
    if(error) http_error(fd,503,"Native speaker playback failed or stopped");
    else http_reply(fd,204,"application/json","",0,NULL);
}
