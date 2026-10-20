/* Bounded C adaptation of go2rtc 1.9.14 pkg/iso and pkg/pcm/flac.go.
 * Copyright (c) 2022 Alexey Khit. MIT; see vendor/go2rtc/LICENSE.
 * Only the validated H.264 640x360 and mono 8kHz PCMA paths are retained. */
#include "mux.h"
#include <string.h>

static void bytes(struct mp4 *m,const void *data,size_t n)
{
    if(n>sizeof(m->data)-m->used) {m->error=1;return;}
    memcpy(m->data+m->used,data,n);m->used+=n;
}
static void zeros(struct mp4 *m,size_t n)
{
    static const unsigned char z[128]={0};
    while(n) {size_t k=n>sizeof(z)?sizeof(z):n;bytes(m,z,k);n-=k;}
}
static void u8(struct mp4 *m,unsigned n) {unsigned char v=(unsigned char)n;bytes(m,&v,1);}
static void u16(struct mp4 *m,unsigned n) {u8(m,n>>8);u8(m,n);}
static void u32(struct mp4 *m,uint32_t n) {u16(m,n>>16);u16(m,n);}
static void u64(struct mp4 *m,uint64_t n) {u32(m,(uint32_t)(n>>32));u32(m,(uint32_t)n);}
static void atom(struct mp4 *m,const char name[4])
{
    if(m->depth>=16) {m->error=1;return;}
    m->stack[m->depth++]=m->used;u32(m,0);bytes(m,name,4);
}
static void end(struct mp4 *m)
{
    if(!m->depth) {m->error=1;return;}
    size_t start=m->stack[--m->depth],n=m->used-start;
    if(start+4>sizeof(m->data)) {m->error=1;return;}
    for(unsigned i=0;i<4;i++) m->data[start+i]=(unsigned char)(n>>(24-8*i));
}
static void matrix(struct mp4 *m)
{
    u32(m,0x10000);u32(m,0);u32(m,0);u32(m,0);u32(m,0x10000);
    u32(m,0);u32(m,0);u32(m,0);u32(m,0x40000000);
}
static void track(struct mp4 *m,unsigned id,const unsigned char *sps,size_t ns,const unsigned char *pps,size_t np)
{
    int video=id==1;
    atom(m,"trak");atom(m,"tkhd");u32(m,3);zeros(m,8);u32(m,id);zeros(m,16);
    u16(m,0);u16(m,video?0:1);u16(m,video?0:0x100);u16(m,0);matrix(m);
    u32(m,video?640u<<16:0);u32(m,video?360u<<16:0);end(m);
    atom(m,"mdia");atom(m,"mdhd");zeros(m,12);u32(m,video?90000:8000);u32(m,0);u16(m,0x55c4);u16(m,0);end(m);
    atom(m,"hdlr");zeros(m,8);bytes(m,video?"vide":"soun",4);zeros(m,12);bytes(m,video?"VideoHandler":"SoundHandler",13);end(m);
    atom(m,"minf");atom(m,video?"vmhd":"smhd");u32(m,video?1:0);zeros(m,video?8:4);end(m);
    atom(m,"dinf");atom(m,"dref");u32(m,0);u32(m,1);atom(m,"url ");u32(m,1);end(m);end(m);end(m);
    atom(m,"stbl");atom(m,"stsd");u32(m,0);u32(m,1);
    atom(m,video?"avc1":"fLaC");zeros(m,6);u16(m,1);
    if(video) {
        zeros(m,16);u16(m,640);u16(m,360);u32(m,72u<<16);u32(m,72u<<16);u32(m,0);u16(m,1);zeros(m,32);u16(m,24);u16(m,0xffff);
        atom(m,"avcC");u8(m,1);bytes(m,sps+1,3);u8(m,0xff);u8(m,0xe1);
        u16(m,(unsigned)ns);bytes(m,sps,ns);u8(m,1);u16(m,(unsigned)np);bytes(m,pps,np);end(m);
        atom(m,"pasp");u32(m,1);u32(m,1);end(m);
    } else {
        zeros(m,8);u16(m,1);u16(m,16);zeros(m,4);u32(m,8000u<<16);
        /* dfLa FullBox + last STREAMINFO block, no fLaC marker. */
        atom(m,"dfLa");u32(m,0);u32(m,0x80000022);u16(m,32768);u16(m,32768);zeros(m,6);
        u8(m,8000>>12);u8(m,(8000>>4)&255);u8(m,(8000&15)<<4);u8(m,0xf0);zeros(m,20);end(m);
    }
    end(m);end(m);
    const char *empty[]={"stts","stsc","stsz","stco"};
    for(unsigned i=0;i<4;i++) {atom(m,empty[i]);zeros(m,i==2?12:8);end(m);}
    end(m);end(m);end(m);end(m);
}
int mp4_init(struct mp4 *m,const unsigned char *sps,size_t ns,const unsigned char *pps,size_t np,int audio)
{
    memset(m,0,sizeof(*m));
    if(ns<4 || ns>1024 || np<2 || np>1024 || (sps[0]&31)!=7 || (pps[0]&31)!=8) return -1;
    atom(m,"ftyp");bytes(m,"iso5",4);u32(m,512);bytes(m,"iso5iso6mp41",12);end(m);
    atom(m,"moov");atom(m,"mvhd");zeros(m,12);u32(m,1000);u32(m,0);u32(m,0x10000);u16(m,0x100);zeros(m,10);matrix(m);zeros(m,24);u32(m,3);end(m);
    track(m,1,sps,ns,pps,np);if(audio) track(m,2,NULL,0,NULL,0);
    atom(m,"mvex");for(unsigned id=1;id<=(audio?2u:1u);id++) {
        atom(m,"trex");u32(m,0);u32(m,id);u32(m,1);zeros(m,12);end(m);
    }
    end(m);end(m);return m->error || m->depth?-1:0;
}
int mp4_fragment(struct mp4 *m,uint32_t sequence,unsigned track_id,uint32_t duration,size_t size,int key,uint64_t dts)
{
    memset(m,0,sizeof(*m));if(size>512*1024 || !duration || track_id<1 || track_id>2) return -1;
    atom(m,"moof");atom(m,"mfhd");u32(m,0);u32(m,sequence);end(m);atom(m,"traf");
    atom(m,"tfhd");u32(m,0x020038);u32(m,track_id);u32(m,duration);u32(m,(uint32_t)size);u32(m,key?0x02000000:0x01010000);end(m);
    atom(m,"tfdt");u32(m,0x01000000);u64(m,dts);end(m);
    atom(m,"trun");u32(m,1);u32(m,1);u32(m,(uint32_t)m->used+4+8);end(m);end(m);end(m);
    u32(m,(uint32_t)size+8);bytes(m,"mdat",4);return m->error || m->depth?-1:0;
}
static uint8_t crc8(const unsigned char *p,size_t n)
{
    uint8_t crc=0;while(n--) {crc^=*p++;for(unsigned i=0;i<8;i++) crc=(uint8_t)((crc<<1)^((crc&128)?7:0));}return crc;
}
static uint16_t crc16(const unsigned char *p,size_t n)
{
    uint16_t crc=0;while(n--) {crc^=(uint16_t)*p++<<8;for(unsigned i=0;i<8;i++) crc=(uint16_t)((crc<<1)^((crc&32768)?0x8005:0));}return crc;
}
size_t flac_pcma(unsigned char *out,const unsigned char *in,size_t size,uint64_t sample)
{
    if(!size || size>1280 || sample>=UINT64_C(0x1000000000)) return 0;
    out[0]=0xff;out[1]=0xf9;out[2]=0x74;out[3]=8;size_t n=4;
    if(sample<128) out[n++]=(unsigned char)sample;
    else {
        unsigned count=2;while(count<7 && sample>=(UINT64_C(1)<<(5*count+1))) count++;
        out[n++]=(unsigned char)((0xffu<<(8-count))|(sample>>(6*(count-1))));
        for(unsigned i=count-1;i;i--) out[n++]=(unsigned char)(0x80|((sample>>(6*(i-1)))&63));
    }
    out[n++]=(unsigned char)((size-1)>>8);out[n++]=(unsigned char)(size-1);
    unsigned char checksum=crc8(out,n);out[n++]=checksum;out[n++]=2;
    for(size_t i=0;i<size;i++) {
        unsigned a=in[i]^0x55,segment=(a&0x70)>>4;int value=(int)((a&15)<<4)+8;
        if(segment) value=(value+0x100)<<(segment-1);
        if(!(a&128)) value=-value;
        out[n++]=(unsigned char)(value>>8);out[n++]=(unsigned char)value;
    }
    uint16_t sum=crc16(out,n);out[n++]=(unsigned char)(sum>>8);out[n++]=(unsigned char)sum;return n;
}
