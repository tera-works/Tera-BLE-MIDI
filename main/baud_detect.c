#include "baud_detect.h"
#include <string.h>
static const uint32_t candidates[]=BAUD_CANDIDATES;
#define NC (sizeof(candidates)/sizeof(candidates[0]))
bool baud_supported(uint32_t b){for(unsigned i=0;i<NC;i++)if(candidates[i]==b)return true;return false;}
static void clear(baud_detect_t *d){midi_init(&d->evidence,0,0);d->errors=0;d->trial_activity_ms=0;d->trial_active=false;}
void baud_init(baud_detect_t *d,uint32_t b) {
 memset(d,0,sizeof(*d));d->baud=baud_supported(b)?b:BAUD_DEFAULT;
 for(unsigned i=0;i<NC;i++)if(candidates[i]==d->baud)d->candidate=i;
 clear(d);
}
int baud_score(const baud_detect_t *d) {
 return (int)(d->evidence.structured*6+(d->evidence.valid-d->evidence.structured))- (int)(d->evidence.invalid*5+d->errors*12);
}
bool baud_confident(const baud_detect_t *d) {
 return d->evidence.valid>=BAUD_MIN_MESSAGES&&d->evidence.structured>=BAUD_MIN_STRUCTURED&&
 d->evidence.valid>=8*(d->evidence.invalid+d->errors+1)&&baud_score(d)>=BAUD_MIN_SCORE;
}
bool baud_advance(baud_detect_t *d) {
 if(!d->searching)return false;
 d->candidate=(d->candidate+1)%NC;d->baud=candidates[d->candidate];clear(d);return true;
}
bool baud_observe(baud_detect_t *d,const uint8_t *data,size_t n,unsigned errors,uint32_t now) {
 if(!n&&!errors)return false; /* Silence can NEVER start or advance a search. */
 if(!d->trial_active){d->trial_activity_ms=now;d->trial_active=true;}
 d->errors+=errors;
 for(size_t i=0;i<n;i++)midi_feed(&d->evidence,data[i],0);
 midi_flush(&d->evidence);
 d->last_score=baud_score(d);
 if(baud_confident(d)) {
  d->confirmed=true;d->searching=false;
  /* Preserve running status across evidence windows. */
  d->evidence.bytes=d->evidence.valid=d->evidence.structured=d->evidence.invalid=d->errors=0;
  d->trial_activity_ms=0;d->trial_active=false;return false;
 }
 bool bad=d->errors>=4||(d->evidence.bytes>=128&&
  (d->evidence.valid==0||d->evidence.invalid+d->errors>d->evidence.valid/2));
 if(!d->searching&&bad){d->searching=true;d->confirmed=false;return baud_advance(d);}
 if(d->searching&&(bad||(uint32_t)(now-d->trial_activity_ms)>=BAUD_TRIAL_MS))return baud_advance(d);
 /* Bounded rolling evidence; retain running status, discard old good scores. */
 if(!d->searching&&d->evidence.bytes>512) {
  d->evidence.bytes=d->evidence.valid=d->evidence.structured=d->evidence.invalid=d->errors=0;
  d->trial_activity_ms=0;d->trial_active=false;
 }
 return false;
}
