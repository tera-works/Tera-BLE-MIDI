#include "baud_detect.h"
static baud_detect_t detector;
void wave_reset(unsigned b){baud_init(&detector,b);}
unsigned wave_baud(void){return detector.baud;}
unsigned wave_confirmed(void){return detector.confirmed;}
unsigned wave_searching(void){return detector.searching;}
unsigned wave_feed(const unsigned char *d,unsigned n,unsigned errors,unsigned now){return baud_observe(&detector,d,n,errors,now);}
