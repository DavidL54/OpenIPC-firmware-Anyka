#ifndef ANYKA_MUX_H
#define ANYKA_MUX_H
#include <stdint.h>
#include <stddef.h>
struct mp4 { unsigned char data[4096]; size_t used, stack[16]; unsigned depth; int error; };
int mp4_init(struct mp4 *m,const unsigned char *sps,size_t ns,const unsigned char *pps,size_t np,int audio);
int mp4_fragment(struct mp4 *m,uint32_t sequence,unsigned track,uint32_t duration,size_t size,int key,uint64_t dts);
size_t flac_pcma(unsigned char *out,const unsigned char *in,size_t size,uint64_t sample);
#endif
