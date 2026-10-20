#define _GNU_SOURCE
#include <sys/socket.h>
static ssize_t counted_send(int fd, const void *data, size_t length, int flags);
#define send counted_send
#define SYS_clock_gettime32 SYS_clock_gettime
#include "anyka-native-rtsp.c"
#undef send
#include <assert.h>

static unsigned int sends;
static size_t largest_send;
static ssize_t counted_send(int fd, const void *data, size_t length, int flags)
{
	sends++;
	if (length > largest_send) largest_send = length;
	return send(fd, data, length, flags);
}

// Thread creation is never exercised by these packetizer/parser unit tests.
int ak_thread_create(ak_pthread_t *id, thread_func function, void *argument, int size, int priority)
{ (void)id; (void)function; (void)argument; (void)size; (void)priority; return -1; }
int ak_thread_detach(ak_pthread_t id) { (void)id; return 0; }
int ak_thread_join(ak_pthread_t id) { (void)id; return 0; }

static void test_fields(void)
{
	char value[16];
	assert(field("PLAY rtsp://camera/vs0 RTSP/1.0\r\ncSeQ: 12\r\nSession: abcd\r\n\r\n", "CSeq", value, sizeof(value)) == 1);
	assert(!strcmp(value, "12"));
	assert(field("PLAY uri RTSP/1.0\r\nCSeq: 1\r\nCSeq: 2\r\n\r\n", "CSeq", value, sizeof(value)) < 0);
	assert(field("PLAY uri RTSP/1.0\r\nCSeq: 12345678901234567890\r\n\r\n", "CSeq", value, sizeof(value)) < 0);
}

static void test_idr_packetization(void)
{
	int sockets[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
	struct client client = {0}; client.fd = sockets[0]; client.auth = -1;
	client.tracks[0].enabled = 1; client.tracks[0].sequence = 123;
	unsigned char data[4016] = {0,0,0,1,0x41,1,2,3};
	client.frame = data;
	rtsp_frame_info frame = {0}; frame.uiDataLen = 8; frame.u64Time = 1000;
	assert(!send_video(&client, &frame));
	unsigned char output[8192];
	assert(recv(sockets[1], output, sizeof(output), MSG_DONTWAIT) < 0 && errno == EAGAIN);
	// Two complete parameter NALs and an IDR requiring FU-A fragmentation.
	unsigned char headers[] = {0,0,0,1,0x67,0x64,0,0x1f,0,0,1,0x68,1,2,0,0,0,1,0x65};
	memcpy(data, headers, sizeof(headers)); memset(data + sizeof(headers), 0x55, sizeof(data)-sizeof(headers));
	frame.uiDataLen = sizeof(data);
	assert(!send_video(&client, &frame)); assert(client.video_ready);
	size_t received = 0;
	for (;;) {
		ssize_t count = recv(sockets[1], output+received, sizeof(output)-received, MSG_DONTWAIT);
		if (count < 0) { assert(errno == EAGAIN); break; }
		assert(count > 0); received += (size_t)count;
	}
	size_t offset = 0; int packets = 0, markers = 0, starts = 0, ends = 0;
	while (offset < received) {
		assert(output[offset] == '$' && output[offset+1] == 0);
		size_t length = output[offset+2]*256U + output[offset+3];
		assert(length >= 13 && offset + length + 4 <= received);
		assert(output[offset+4] == 0x80 && (output[offset+5]&127) == 96);
		uint16_t sequence; memcpy(&sequence, output+offset+6, 2); assert(ntohs(sequence) == 123+packets);
		uint32_t timestamp; memcpy(&timestamp, output+offset+8, 4); assert(ntohl(timestamp) == 0);
		markers += !!(output[offset+5] & 128);
		if ((output[offset+16]&31) == 28) { starts += !!(output[offset+17]&128); ends += !!(output[offset+17]&64); }
		packets++; offset += length + 4;
	}
	assert(packets == 5 && markers == 1 && starts == 1 && ends == 1);
	close(sockets[0]); close(sockets[1]);
}

static void same_parameters(const struct venc_param *a, const struct venc_param *b)
{
	// Compare fields, not unspecified padding copied by a struct return.
#define SAME(field) assert(a->field == b->field)
	SAME(width); SAME(height); SAME(fps); SAME(goplen); SAME(target_kbps); SAME(max_kbps);
	SAME(profile); SAME(br_mode); SAME(initqp); SAME(minqp); SAME(maxqp); SAME(jpeg_qlevel);
	SAME(chroma_mode); SAME(enc_out_type); SAME(max_picture_size); SAME(enc_level);
	SAME(smart_mode); SAME(smart_goplen); SAME(smart_quality); SAME(smart_static_value); SAME(gdr_mode);
#undef SAME
}

static void test_encoder_parameters(void)
{
	struct venc_param input = {0}; input.fps=20; input.goplen=100; input.enc_out_type=H264_ENC_TYPE; input.target_kbps=2048;
	struct venc_param expected=input; expected.goplen=20;
	struct venc_param result=native_encoder_parameters(&input);
	same_parameters(&result,&expected); assert(input.goplen==100);
	input.enc_out_type=MJPEG_ENC_TYPE; result=native_encoder_parameters(&input); same_parameters(&result,&input);
	input.enc_out_type=H264_ENC_TYPE; input.goplen=10; result=native_encoder_parameters(&input); assert(result.goplen==10);
}
static void test_audio_clock(void)
{
	int sockets[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
	struct client client = {0}; client.fd = sockets[0]; client.auth=-1;
	client.timestamp_started=1; client.timestamp_origin=1000;
	unsigned char pcm[256]={0}, packet[272];
	assert(!send_audio(&client,pcm,sizeof(pcm),1010));
	assert(recv(sockets[1],packet,sizeof(packet),MSG_DONTWAIT)<0 && errno==EAGAIN);
	client.video_ready=1; client.clock_origin=now_ms();
	client.audio_clock_started=1; client.audio_next_timestamp=80;
	uint64_t times[]={1010,1042,9000,100,1080};
	for (unsigned int i=0;i<sizeof(times)/sizeof(times[0]);i++) {
		assert(!send_audio(&client,pcm,sizeof(pcm),times[i]));
		assert(recv(sockets[1],packet,sizeof(packet),0)==sizeof(packet));
		uint32_t stamp;memcpy(&stamp,packet+8,4);
		assert(ntohl(stamp)==80+i*256);
		assert(packet[5]==8);
	}
	client.audio_next_timestamp=UINT32_MAX-127;
	assert(!send_audio(&client,pcm,sizeof(pcm),100));
	assert(client.audio_next_timestamp==128);
	close(sockets[0]);close(sockets[1]);
}
static void test_shared_sender_clock(void)
{
	int sockets[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));
	struct client client={0};client.fd=sockets[0];client.auth=-1;
	start_timeline(&client,123456);
	client.clock_origin-=100;
	for(int i=0;i<2;i++) {
		client.tracks[i].enabled=1;client.tracks[i].packets=1;client.tracks[i].timestamp=999999999;
		assert(!sender_report(&client,i));
		unsigned char packet[32];assert(recv(sockets[1],packet,sizeof(packet),0)==sizeof(packet));
		uint32_t stamp;memcpy(&stamp,packet+20,4);stamp=ntohl(stamp);
		unsigned int milliseconds=stamp/(i ? 8U : 90U);
		assert(milliseconds>=100 && milliseconds<200);
	}
	close(sockets[0]);close(sockets[1]);
}
static void test_large_frame_batch(void)
{
	int sockets[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
	struct client client = {0}; client.fd=sockets[0]; client.auth=-1;
	client.tracks[0].enabled=1; client.tracks[0].sequence=65530;
	unsigned char data[30005]={0,0,0,1,0x65};
	memset(data+5,0x5a,sizeof(data)-5); client.frame=data;
	rtsp_frame_info frame={0}; frame.uiDataLen=sizeof(data); frame.u64Time=1000;
	sends=0; largest_send=0;
	assert(!send_video(&client,&frame));
	assert(client.video_ready && !client.rtp_batch_length);
	assert(sends <= 5 && largest_send <= RTP_BATCH_LIMIT);
	unsigned char output[32000]; size_t received=0;
	for (;;) {
		ssize_t count=recv(sockets[1],output+received,sizeof(output)-received,MSG_DONTWAIT);
		if (count<0) { assert(errno==EAGAIN); break; }
		assert(count>0); received+=(size_t)count;
	}
	size_t offset=0, bytes=0; unsigned int packets=0, markers=0, starts=0, ends=0;
	while (offset<received) {
		assert(output[offset]=='$');
		size_t length=output[offset+2]*256U+output[offset+3];
		assert(length>=14 && offset+length+4<=received);
		uint16_t sequence; memcpy(&sequence,output+offset+6,2);
		assert(ntohs(sequence)==(uint16_t)(65530+packets));
		assert(output[offset+16]==0x7c && (output[offset+17]&31)==5);
		markers+=!!(output[offset+5]&128); starts+=!!(output[offset+17]&128); ends+=!!(output[offset+17]&64);
		for(size_t i=offset+18;i<offset+length+4;i++) assert(output[i]==0x5a);
		bytes+=length-14; packets++; offset+=length+4;
	}
	assert(bytes==sizeof(data)-5 && packets==22 && markers==1 && starts==1 && ends==1);
	assert(client.tracks[0].packets==packets);
	// A following P-frame must not wait for another IDR or retain its tail.
	data[4]=0x41; frame.uiDataLen=100; frame.u64Time=1050;
	assert(!send_video(&client,&frame) && !client.rtp_batch_length);
	assert(recv(sockets[1],output,sizeof(output),MSG_DONTWAIT)>0);
	close(sockets[0]);close(sockets[1]);
}

static void test_revocation_and_blocked_reader(void)
{
	int sockets[2], auth[2];
	assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));
	assert(!socketpair(AF_UNIX,SOCK_STREAM,0,auth));
	struct client client={0};client.fd=sockets[0];client.auth=auth[0];
	unsigned char data[1400]={1};
	assert(!rtp(&client,0,data,sizeof(data),0,1,NULL,0));
	close(auth[1]);
	assert(flush_rtp(&client)<0);
	assert(recv(sockets[1],data,sizeof(data),MSG_DONTWAIT)<0 && errno==EAGAIN);
	close(auth[0]);close(sockets[0]);close(sockets[1]);
	assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));
	int limit=4096;setsockopt(sockets[0],SOL_SOCKET,SO_SNDBUF,&limit,sizeof(limit));
	while(send(sockets[0],data,sizeof(data),MSG_DONTWAIT)>0) {}
	assert(errno==EAGAIN);
	unsigned long start=now_ms();
	assert(write_all(sockets[0],-1,data,sizeof(data))<0);
	assert(now_ms()-start>=400 && now_ms()-start<1500);
	close(sockets[0]);close(sockets[1]);
}

static size_t reference_start(const unsigned char *data,size_t length,size_t offset,size_t *prefix)
{
	for(size_t i=offset;i+3<=length;i++) {
		if(data[i] || data[i+1]) continue;
		if(data[i+2]==1) {*prefix=3;return i;}
		if(i+4<=length && !data[i+2] && data[i+3]==1) {*prefix=4;return i;}
	}
	*prefix=0;return length;
}

static void test_annexb_scan(void)
{
	unsigned char data[257]; unsigned int state=37;
	for(unsigned int test=0;test<500;test++) {
		for(size_t i=0;i<sizeof(data);i++) {state=state*1103515245U+12345U;data[i]=(state>>24)%4;}
		for(size_t len=0;len<sizeof(data);len++) {
			size_t offset=len?test%len:0,p,q;
			assert(start_code(data,len,offset,&p)==reference_start(data,len,offset,&q) && p==q);
		}
	}
}

static unsigned char mock_block;
static int mock_frame(int handle,struct frame *frame,unsigned char block)
{assert(handle==7 && frame);mock_block=block;return 12;}
static int mock_stream(int handle,struct audio_stream *stream,unsigned char block)
{assert(handle==8 && stream);mock_block=block;return 13;}
static void mock_resolve(void) {sdk_ai_frame=mock_frame;sdk_aenc_stream=mock_stream;}
static void test_audio_wait_forwarding(void)
{
	pthread_once(&audio_wait_once,mock_resolve);
	struct frame frame={0};struct audio_stream stream={0};
	audio_blocking=1;
	assert(ak_ai_get_frame(7,&frame,0)==12 && mock_block==1);
	assert(ak_aenc_get_stream(8,&stream,0)==13 && mock_block==1);
	assert(ak_ai_get_frame(7,&frame,1)==12 && mock_block==1);
	audio_blocking=0;
	assert(ak_ai_get_frame(7,&frame,0)==12 && mock_block==0);
	assert(ak_aenc_get_stream(8,&stream,0)==13 && mock_block==0);
	assert(ak_aenc_get_stream(8,&stream,1)==13 && mock_block==1);
}

static char fake_audio_ring, fake_video_ring;
static int reader_live[SDK_READER_LIMIT], reader_cursor[SDK_READER_LIMIT], reader_deletes, video_deletes;
static int reader_allocation_failure;
static unsigned char reader_data[SDK_READER_LIMIT];
static int mock_reader_new(CIRBUF_HANDLE ring, char *name)
{
	assert(ring == &fake_audio_ring && !strncmp(name,"SDKaudio",8));
	if (reader_allocation_failure) return -1;
	for (int i=0;i<SDK_READER_LIMIT;i++) if (!reader_live[i]) {
		reader_live[i]=1; reader_cursor[i]=0; return i;
	}
	return -1;
}
static int mock_reader_delete(CIRBUF_HANDLE ring, unsigned int id)
{
	if (ring == &fake_video_ring) {video_deletes++;return 0;}
	assert(ring == &fake_audio_ring && id<SDK_READER_LIMIT && reader_live[id]);
	reader_live[id]=0;reader_deletes++;return 0;
}
static int mock_ring_get(CIRBUF_HANDLE ring,unsigned int id,unsigned char **data,unsigned int *size,unsigned int timeout)
{
	assert(!timeout);
	if (ring == &fake_video_ring) {assert(id==7);return 17;}
	assert(ring==&fake_audio_ring && id<SDK_READER_LIMIT && reader_live[id]);
	reader_data[id]=(unsigned char)reader_cursor[id]++;
	*data=&reader_data[id];*size=1;return 0;
}
static void mock_readers_resolve(void)
{
	sdk_reader_new=mock_reader_new;sdk_reader_delete=mock_reader_delete;sdk_ring_get=mock_ring_get;
	assert(!pthread_key_create(&audio_reader_key,NULL));
}
static int mock_play(char *source) {return atoi(source);}
static int mock_teardown(int handle)
{
	assert(handle>=0 && handle<SDK_READER_LIMIT);
	assert(!cirbuf_reader_delete(&fake_video_ring,0));
	// Reproduce the sample's erroneous, shared audio reader 0 teardown.
	return cirbuf_reader_delete(&fake_audio_ring,0);
}
static int mock_audio_callback(int handle,rtsp_frame_info *frame,void *user)
{
	assert(handle>=0 && handle<SDK_READER_LIMIT && user==&fake_audio_ring);
	unsigned char *data=NULL;unsigned int size=0;
	int result=cirbuf_get_buff_data(&fake_audio_ring,0,&data,&size,0);
	if (!result) {frame->pData=data;frame->uiDataLen=size;}
	return result;
}
static void test_independent_audio_readers(void)
{
	pthread_mutex_init(&callbacks_lock,NULL);
	pthread_once(&audio_reader_once,mock_readers_resolve);
	play_callback=mock_play;teardown_callback=mock_teardown;audio_callback=mock_audio_callback;
	assert(serialized_play("0")==0 && serialized_play("1")==1);
	rtsp_frame_info frame={0};
	for (int sequence=0;sequence<10;sequence++) for(int handle=0;handle<2;handle++) {
		assert(!serialized_audio(handle,&frame,&fake_audio_ring));
		assert(frame.uiDataLen==1 && *(unsigned char *)frame.pData==sequence);
	}
	assert(sdk_audio_readers[0].id!=sdk_audio_readers[1].id);
	// Teardown of a session which never read audio must not delete reader 0.
	assert(serialized_play("2")==2 && !serialized_teardown(2));
	assert(!reader_deletes && video_deletes==1);
	assert(!serialized_teardown(0) && reader_deletes==1);
	assert(!serialized_audio(1,&frame,&fake_audio_ring) && *(unsigned char *)frame.pData==10);
	assert(serialized_audio(0,&frame,&fake_audio_ring)<0);
	assert(serialized_play("0")==0 && !serialized_audio(0,&frame,&fake_audio_ring));
	assert(*(unsigned char *)frame.pData==0);
	assert(!serialized_teardown(1) && !serialized_teardown(0) && reader_deletes==3);
	reader_allocation_failure=1;
	assert(serialized_play("0")==0 && serialized_audio(0,&frame,&fake_audio_ring)<0);
	assert(!serialized_teardown(0) && reader_deletes==3);
	assert(serialized_audio(-1,&frame,NULL)<0 && serialized_audio(SDK_READER_LIMIT,&frame,NULL)<0);
	unsigned char *data=NULL;unsigned int size=0;
	assert(cirbuf_get_buff_data(&fake_video_ring,7,&data,&size,0)==17);
	pthread_mutex_destroy(&callbacks_lock);
}

int main(void)
{
	test_fields();test_idr_packetization();test_encoder_parameters();test_audio_clock();test_shared_sender_clock();
	test_large_frame_batch();test_revocation_and_blocked_reader();test_annexb_scan();test_audio_wait_forwarding();
	test_independent_audio_readers();
	puts("native RTSP parser, bounded batches, IDR/FU-A, revocation, SDK waits, independent audio readers and clocks: OK");
}
