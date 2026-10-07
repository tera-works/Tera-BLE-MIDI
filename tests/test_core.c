#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "midi_parser.h"
#include "ble_codec.h"
#include "baud_detect.h"
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct {uint8_t bytes[20000],kind[20000];uint16_t ts[20000];size_t n,events;} capture_t;
static capture_t cap;
static void sink(void *ctx,const uint8_t *b,size_t n,uint16_t ts,uint8_t kind) {
 capture_t *c=ctx;assert(c->n+n<=sizeof(c->bytes));
 memcpy(c->bytes+c->n,b,n);c->n+=n;c->ts[c->events]=ts;c->kind[c->events++]=kind;
}
static void feed(midi_parser_t *p,const uint8_t *b,size_t n){for(size_t i=0;i<n;i++)midi_feed(p,b[i],123);midi_flush(p);}
static int parser_tests(void) {
 const uint8_t cases[][3]={{0x90,60,100},{0x80,60,0},{0xa2,4,5},{0xb0,7,100},{0xc0,3,0},{0xd0,4,0},{0xe0,0,64},{0xf1,2,0},{0xf2,3,4},{0xf3,5,0},{0xf6,0,0}};
 const size_t lengths[]={3,3,3,3,2,2,3,2,3,2,1};midi_parser_t p;
 for(unsigned i=0;i<sizeof(lengths)/sizeof(*lengths);i++) {
  memset(&cap,0,sizeof(cap));midi_init(&p,sink,&cap);feed(&p,cases[i],lengths[i]);
  CHECK(cap.events==1);CHECK(cap.n==lengths[i]);CHECK(memcmp(cap.bytes,cases[i],lengths[i])==0);CHECK(p.invalid==0);
 }
 memset(&cap,0,sizeof(cap));midi_init(&p,sink,&cap);
 const uint8_t run[]={0x90,60,100,61,101,0xf8,62,102};feed(&p,run,sizeof(run));
 const uint8_t expect[]={0x90,60,100,0x90,61,101,0xf8,0x90,62,102};
 CHECK(cap.n==sizeof(expect));CHECK(memcmp(cap.bytes,expect,sizeof(expect))==0);CHECK(p.valid==4);
 memset(&cap,0,sizeof(cap));midi_init(&p,sink,&cap);
 const uint8_t insert[]={0x90,60,0xfa,0xfe,100};feed(&p,insert,sizeof(insert));
 const uint8_t insert_expected[]={0xfa,0xfe,0x90,60,100};
 CHECK(cap.n==sizeof(insert_expected));CHECK(memcmp(cap.bytes,insert_expected,cap.n)==0);CHECK(p.invalid==0);
 memset(&cap,0,sizeof(cap));midi_init(&p,sink,&cap);
 const uint8_t sx[]={0xf0,1,2,0xf8,3,4,0xf7};
 for(unsigned i=0;i<sizeof(sx);i++){feed(&p,sx+i,1);} /* every UART boundary */
 CHECK(cap.n==sizeof(sx));CHECK(memcmp(cap.bytes,sx,sizeof(sx))==0);CHECK(!p.in_sysex);
 memset(&cap,0,sizeof(cap));midi_init(&p,sink,&cap);midi_feed(&p,0xf0,0);
 for(unsigned i=0;i<10000;i++)midi_feed(&p,i&127,0);
 midi_feed(&p,0xf7,0);CHECK(cap.n==10002);CHECK(p.valid==1);CHECK(p.invalid==0);
 memset(&cap,0,sizeof(cap));midi_init(&p,sink,&cap);
 const uint8_t bad[]={1,2,0xf4,0xf5,0xf9,0xfd,0x90,60,0x80,60,0};feed(&p,bad,sizeof(bad));
 CHECK(p.invalid==7);CHECK(cap.n==3);CHECK(cap.bytes[0]==0x80);
 memset(&cap,0,sizeof(cap));midi_init(&p,sink,&cap);
 const uint8_t broken_sx[]={0xf0,1,0x90,60,100};feed(&p,broken_sx,sizeof(broken_sx));
 CHECK(p.invalid==1);CHECK(cap.bytes[2]==0xf7);CHECK(cap.bytes[3]==0x90);
 memset(&cap,0,sizeof(cap));midi_init(&p,sink,&cap);
 const uint8_t common_cancel[]={0x90,60,1,0xf1,1,61,2};feed(&p,common_cancel,sizeof(common_cancel));
 CHECK(p.invalid==2);CHECK(p.valid==2);
 /* UART running status survives arbitrary chunks and evidence windows. */
 puts("MIDI parser: PASS");return 0;
}
typedef struct {uint8_t p[2000][BLE_PACKET_MAX];size_t n[2000],count;bool reject;} packets_t;
static packets_t packets;
static bool packet_sink(void *ctx,const uint8_t *p,size_t n) {
 packets_t *q=ctx;if(q->reject)return false;assert(q->count<2000);assert(n<=BLE_PACKET_MAX);
 memcpy(q->p[q->count],p,n);q->n[q->count++]=n;return true;
}
static void encoder_sink(void *ctx,const uint8_t *p,size_t n,uint16_t ts,uint8_t kind) {assert(ble_encode(ctx,p,n,ts,kind));}
static int codec_tests(void) {
 ble_encoder_t e;ble_decoder_t d;memset(&packets,0,sizeof(packets));ble_encoder_init(&e,packet_sink,&packets);
 const uint8_t note[]={0x90,60,100};CHECK(ble_encode(&e,note,3,8191,MIDI_SHORT));
 CHECK(packets.count==1);CHECK(packets.n[0]==5);CHECK(packets.p[0][0]==0xbf);CHECK(packets.p[0][1]==0xff);
 memset(&cap,0,sizeof(cap));ble_decoder_init(&d,sink,&cap);CHECK(ble_decode(&d,packets.p[0],packets.n[0]));
 CHECK(cap.ts[0]==8191);CHECK(cap.n==3);CHECK(memcmp(cap.bytes,note,3)==0);
 const uint8_t multi[]={0x80,0xfe,0x90,60,100,0xff,61,101,0x80,0xb0,7,99,0x81,0xf8,0x82,62,102};
 memset(&cap,0,sizeof(cap));ble_decoder_init(&d,sink,&cap);CHECK(ble_decode(&d,multi,sizeof(multi)));
 CHECK(cap.events==5);CHECK(cap.ts[0]==126);CHECK(cap.ts[1]==127);CHECK(cap.ts[2]==128);CHECK(cap.ts[4]==130);
 CHECK(cap.bytes[10]==0xb0); /* retained running status after realtime */
 const uint8_t sys_common[]={0x80,0x80,0x90,1,2,0x81,0xf1,3,0x82,4,5};
 memset(&cap,0,sizeof(cap));ble_decoder_init(&d,sink,&cap);CHECK(ble_decode(&d,sys_common,sizeof(sys_common)));
 CHECK(cap.n==8);CHECK(cap.bytes[5]==0x90);CHECK(cap.ts[2]==2);
 const uint8_t missing_ts[]={0x80,0x80,0x90,1,2,0x81,0xf8,3,4};CHECK(!ble_decode(&d,missing_ts,sizeof(missing_ts)));
 const uint8_t no_ts_running[]={0x80,0x80,0x90,1,2,3,4};
 memset(&cap,0,sizeof(cap));ble_decoder_init(&d,sink,&cap);CHECK(ble_decode(&d,no_ts_running,sizeof(no_ts_running)));CHECK(cap.events==2);
 const uint8_t illegal_cross[]={0x80,0x80,3,4};CHECK(!ble_decode(&d,illegal_cross,sizeof(illegal_cross)));
 const uint8_t partial[]={0x80,0x80,0x90,1};CHECK(!ble_decode(&d,partial,sizeof(partial)));
 const uint8_t header[]={0xc0,0x80,0xf8};CHECK(!ble_decode(&d,header,sizeof(header)));
 const uint8_t ts_only[]={0x80,0x80};CHECK(!ble_decode(&d,ts_only,sizeof(ts_only)));
 /* Complete encoder->decoder streaming roundtrip, long SysEx with realtime. */
 memset(&packets,0,sizeof(packets));ble_encoder_init(&e,packet_sink,&packets);midi_parser_t p;midi_init(&p,encoder_sink,&e);
 midi_feed(&p,0xf0,8190);
 for(unsigned i=0;i<1024;i++){midi_feed(&p,i&127,8190);if(i==500)midi_feed(&p,0xf8,8190);}
 midi_feed(&p,0xf7,8190);midi_flush(&p);
 memset(&cap,0,sizeof(cap));ble_decoder_init(&d,sink,&cap);
 for(size_t i=0;i<packets.count;i++){CHECK(packets.n[i]<=20);CHECK(ble_decode(&d,packets.p[i],packets.n[i]));}
 CHECK(cap.n==1027);CHECK(cap.bytes[0]==0xf0);CHECK(cap.bytes[502]==0xf8);CHECK(cap.bytes[1026]==0xf7);CHECK(!d.midi.in_sysex);
 /* End timestamp must fit in its own packet at MTU boundary. */
 e.limit=5;memset(&packets,0,sizeof(packets));e.sysex=false;
 const uint8_t small_sx[]={0xf0,1,2,3,4,5,6,0xf7};CHECK(ble_encode(&e,small_sx,sizeof(small_sx),0,MIDI_SYSEX));
 memset(&cap,0,sizeof(cap));ble_decoder_init(&d,sink,&cap);
 for(size_t i=0;i<packets.count;i++){CHECK(packets.n[i]<=5);CHECK(ble_decode(&d,packets.p[i],packets.n[i]));}
 CHECK(cap.n==sizeof(small_sx));CHECK(memcmp(cap.bytes,small_sx,cap.n)==0);
 const uint8_t begin[]={0x80,0x80,0xf0,1};CHECK(ble_decode(&d,begin,sizeof(begin)));
 CHECK(!ble_decode(&d,header,sizeof(header)));CHECK(!d.midi.in_sysex);CHECK(cap.bytes[cap.n-1]==0xf7);
 packets.reject=true;e.sysex=false;CHECK(!ble_encode(&e,small_sx,sizeof(small_sx),0,MIDI_SYSEX));CHECK(!e.sysex);
 puts("BLE codec: PASS");return 0;
}
static int baud_tests(void) {
 const uint32_t rates[]={31250,38400,57600,115200,230400};baud_detect_t d;
 const uint8_t note[]={0x90,60,100};
 for(unsigned r=0;r<5;r++) {
  baud_init(&d,rates[r]);CHECK(d.baud==rates[r]);CHECK(baud_supported(rates[r]));
  for(unsigned i=0;i<11;i++)CHECK(!baud_observe(&d,note,3,0,1+i));
  CHECK(!d.confirmed);CHECK(!baud_observe(&d,note,3,0,13));CHECK(d.confirmed);CHECK(!d.searching);
 }
 baud_init(&d,0);CHECK(d.baud==31250);
 for(unsigned i=0;i<10000;i++)CHECK(!baud_observe(&d,0,0,0,i*1000));
 CHECK(!d.searching);CHECK(d.baud==31250);
 CHECK(!baud_observe(&d,note,3,0,100001));CHECK(!d.confirmed);
 CHECK(baud_observe(&d,0,0,4,100002));CHECK(d.searching);uint32_t b=d.baud;
 for(unsigned i=0;i<10000;i++)CHECK(!baud_observe(&d,0,0,0,200000+i*1000));
 CHECK(d.searching&&d.baud==b); /* Search freezes when stimulus stops. */
 for(unsigned r=0;r<5;r++) {
  baud_init(&d,31250);CHECK(baud_observe(&d,0,0,4,1));
  for(unsigned i=0;i<5&&d.baud!=rates[r];i++)CHECK(baud_observe(&d,0,0,4,2+i));
  CHECK(d.baud==rates[r]);
  for(unsigned i=0;i<12;i++)CHECK(!baud_observe(&d,note,3,0,10+i));
  CHECK(d.confirmed&&!d.searching);
 }
 uint8_t noise[256];uint32_t random=12345;
 for(unsigned i=0;i<sizeof(noise);i++){random=random*1664525+1013904223;noise[i]=random>>24;}
 baud_init(&d,31250);(void)baud_observe(&d,noise,sizeof(noise),0,1);CHECK(!d.confirmed);CHECK(d.searching);
 baud_init(&d,31250);CHECK(!baud_observe(&d,note,3,0,1));CHECK(!d.confirmed);
 const uint8_t clock[]={0xf8,0xfe,0xfa,0xfc};
 for(unsigned i=0;i<100;i++)(void)baud_observe(&d,clock,sizeof(clock),0,i+2);
 CHECK(!d.confirmed);CHECK(!d.searching);
 baud_init(&d,31250);CHECK(!baud_observe(&d,note,3,0,1));
 const uint8_t run[]={61,101};
 for(unsigned i=0;i<1000;i++)CHECK(!baud_observe(&d,run,2,0,2+i));
 CHECK(d.confirmed&&!d.searching);CHECK(d.evidence.invalid==0);
 /* Previously healthy traffic must not mask a later baud change. */
 CHECK(baud_observe(&d,0,0,4,2000));CHECK(d.searching);
 baud_init(&d,31250);CHECK(baud_observe(&d,0,0,4,0));
 CHECK(!baud_observe(&d,note,3,0,0));CHECK(d.evidence.valid==1);
 baud_init(&d,31250);CHECK(baud_observe(&d,0,0,4,0xfffffff0u));
 CHECK(!baud_observe(&d,note,3,0,0xfffffff0u));
 CHECK(baud_observe(&d,note,3,0,300)); /* Trial duration handles clock rollover. */
 puts("Auto baud: PASS");return 0;
}
static int fuzz_tests(void) {
 uint32_t r=87654321;ble_decoder_t d;uint8_t input[BLE_PACKET_MAX];
 for(unsigned trial=0;trial<10000;trial++) {
  r=r*1664525+1013904223;size_t n=1+r%sizeof(input);
  for(size_t i=0;i<n;i++){r=r*1664525+1013904223;input[i]=r>>24;}
  memset(&cap,0,sizeof(cap));ble_decoder_init(&d,sink,&cap);
  (void)ble_decode(&d,input,n);CHECK(cap.n<sizeof(cap.bytes));
 }
 static capture_t expected;ble_encoder_t e;midi_parser_t p,reference;
 for(unsigned trial=0;trial<1000;trial++) {
  memset(&packets,0,sizeof(packets));memset(&expected,0,sizeof(expected));memset(&cap,0,sizeof(cap));
  ble_encoder_init(&e,packet_sink,&packets);midi_init(&p,encoder_sink,&e);midi_init(&reference,sink,&expected);
  size_t n=0;
  for(unsigned j=0;j<25;j++) {
   r=r*1664525+1013904223;uint8_t status=0x80+(r%7)*16+(r%16);
   input[n++]=status;input[n++]=(r>>8)&127;
   if((status&0xe0)!=0xc0)input[n++]=(r>>16)&127;
   if(j%7==0)input[n++]=0xf8;
  }
  feed(&p,input,n);feed(&reference,input,n);ble_decoder_init(&d,sink,&cap);
  for(size_t i=0;i<packets.count;i++)CHECK(ble_decode(&d,packets.p[i],packets.n[i]));
  CHECK(cap.n==expected.n);CHECK(memcmp(cap.bytes,expected.bytes,cap.n)==0);
 }
 puts("Malformed fuzz and roundtrip: PASS (10000 random packets, 1000 streams)");return 0;
}
int main(void) {
 if(parser_tests()||codec_tests()||baud_tests()||fuzz_tests())return 1;
 printf("PASS: %u checks\n",checks);return 0;
}
