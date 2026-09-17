#include <assert.h>
#include <deque>
#include <vector>
#include <stdio.h>
#include "RvcFanManager.h"
uint32_t now=1000;
FakeEsp ESP;
std::deque<twai_message_t> incoming;
std::vector<twai_message_t> transmitted;
bool writable=true;
int busState=TWAI_STATE_RUNNING, installs=0;
uint32_t millis() { return now; }
int twai_driver_install(const twai_general_config_t* g,const twai_timing_config_t*,const twai_filter_config_t*) { assert(g->tx_queue_len==0);++installs;return ESP_OK; }
int twai_driver_uninstall(){return ESP_OK;} int twai_start(){busState=TWAI_STATE_RUNNING;return ESP_OK;}
int twai_stop(){busState=TWAI_STATE_STOPPED;return ESP_OK;}
int twai_initiate_recovery(){busState=TWAI_STATE_RECOVERING;return ESP_OK;}
int twai_transmit(const twai_message_t* m,int){if(!writable)return -1;assert(m->extd && m->ss);transmitted.push_back(*m);return ESP_OK;}
int twai_receive(twai_message_t* m,int){if(incoming.empty())return -1;*m=incoming.front();incoming.pop_front();return ESP_OK;}
int twai_get_status_info(twai_status_info_t* s){s->state=busState;return ESP_OK;}
void feedback(uint8_t instance, bool on,uint8_t speed, bool extended=true) {
 twai_message_t m{};m.extd=extended;m.identifier=RvcFan::id(RvcFan::kStatus,142);m.data_length_code=8;
 m.data[0]=instance;m.data[1]=on?0x15:0x14;m.data[2]=speed*2;incoming.push_back(m);
}
void ready(RvcFanManager& m){now+=301;m.update();now+=301;m.update();}
size_t commands(){size_t n=0;for(const auto& m:transmitted)if(RvcFan::dgn(m.identifier)==RvcFan::kCommand)++n;return n;}
int main(){
 assert(RvcFan::id(RvcFan::kCommand,159)==0x19FEA69F);
 assert(RvcFan::id(RvcFan::kRequest,254,159)==0x18EA9FFE);
 uint8_t bytes[8];RvcFan::Status state;
 for(uint8_t p=0;p<=100;++p){RvcFan::command(7,p,bytes);assert(bytes[0]==7&&bytes[2]==p*2);assert(bytes[1]==(p?0xD5:0xFC));for(unsigned i=3;i<8;++i)assert(bytes[i]==255);}
 RvcFan::command(7,50,bytes);
 assert(!RvcFan::decode(RvcFan::id(RvcFan::kStatus,142),false,false,bytes,8,7,state));
 assert(!RvcFan::decode(RvcFan::id(RvcFan::kStatus,142),true,true,bytes,8,7,state));
 assert(!RvcFan::decode(RvcFan::id(RvcFan::kStatus,142),true,false,bytes,7,7,state));
 assert(!RvcFan::decode(RvcFan::id(RvcFan::kStatus,142),true,false,bytes,8,8,state));
 bytes[2]=255;assert(!RvcFan::decode(RvcFan::id(RvcFan::kStatus,142),true,false,bytes,8,7,state));
 OutputController out;SettingsManager settings;RvcFanManager manager(out,settings);
 assert(manager.begin());assert(installs==0&&!out.handler(50));
 settings.config.enabled=1; ++settings.revision; manager.update();assert(installs==1);ready(manager);
 assert(!out.online&&!out.handler(50)&&commands()==0);
 bool claimed=false;for(const auto& m:transmitted) if(RvcFan::dgn(m.identifier)==RvcFan::kClaim) {
   claimed=true;assert(m.identifier==0x18EE009F && (m.data[7]&0x80));
 } assert(claimed);
 feedback(2,true,40);manager.update();assert(!out.online);
 feedback(1,false,50);manager.update();assert(out.online&&!out.on);
 assert(out.handler(20));assert(out.handler(34));now+=151;manager.update();assert(commands()==1);
 const auto& cmd=transmitted.back();assert(RvcFan::dgn(cmd.identifier)==RvcFan::kCommand&&cmd.data[2]==60);
 feedback(1,true,30);manager.update();assert(out.on&&out.speed==30&&!out.pending);
 feedback(1,true,80);manager.update();assert(out.speed==80&&commands()==1); // Local switch feedback does not echo.
 assert(out.handler(0));now+=151;manager.update();feedback(1,false,80);manager.update();assert(!out.on&&!out.pending);
 assert(out.handler(40));now+=151;manager.update();now+=6001;manager.update();assert(!out.pending&&out.error==3);
 const auto sent=commands();now+=16000;manager.update();assert(!out.online&&!out.handler(10)&&commands()==sent);
 feedback(1,true,20);manager.update();assert(out.online);
 // Address contention suspends output and selects another control-panel address.
 twai_message_t claim{};claim.extd=1;claim.identifier=RvcFan::id(RvcFan::kClaim,159);claim.data_length_code=8;
 incoming.push_back(claim);manager.update();assert(out.source==158&&!out.online&&!out.handler(20));ready(manager);assert(out.online);
 assert(out.handler(100));busState=TWAI_STATE_BUS_OFF;manager.update();assert(!out.online&&!out.pending&&out.error==1);
 busState=TWAI_STATE_STOPPED;manager.update();ready(manager);assert(!out.online&&commands()==sent);
 feedback(1,false,60);manager.update();assert(out.online);
 // A busy/unplugged transmitter times out and does not replay on recovery.
 writable=false;assert(out.handler(70));now+=2001;manager.update();assert(!out.pending&&out.error==3);
 writable=true;const auto beforeRecovery=commands();now+=500;manager.update();assert(commands()==beforeRecovery);
 // Direction-only commands preserve fan power/speed and all lid/rain fields.
 assert(out.directionHandler(true));now+=151;manager.update();
 auto reverse=transmitted.back();assert(reverse.data[0]==1&&reverse.data[3]==0xFD);
 for(unsigned i=1;i<8;++i)if(i!=3)assert(reverse.data[i]==255);
 feedback(1,false,60);manager.update();assert(out.pending); // Wrong direction cannot confirm.
 feedback(1,false,60);incoming.back().data[3]=1;manager.update();assert(!out.pending&&out.direction==1&&!out.on);
 // Coalesce both fields in either order; latest direction wins.
 assert(out.directionHandler(false));assert(out.handler(40));now+=151;manager.update();
 assert(transmitted.back().data[2]==80&&transmitted.back().data[3]==0xFC);
 feedback(1,true,40);manager.update();assert(!out.pending);
 assert(out.handler(70));assert(out.directionHandler(true));assert(out.directionHandler(false));
 now+=151;manager.update();assert(transmitted.back().data[2]==140&&transmitted.back().data[3]==0xFC);
 feedback(1,true,70);manager.update();assert(!out.pending);
 // Unknown direction does not masquerade as exhaust or disable speed control.
 feedback(1,true,70);incoming.back().data[3]=255;manager.update();
 assert(out.direction==3&&!out.directionHandler(true));
 assert(out.handler(20));now+=151;manager.update();assert(transmitted.back().data[3]==255);
 feedback(1,true,20);manager.update();
 assert(out.directionHandler(true));now+=151;manager.update();now+=6001;manager.update();
 assert(!out.pending&&out.error==3); // Unconfirmed direction times out.
 const auto directionTimeoutCount=commands();now+=16000;manager.update();
 assert(!out.directionHandler(false)&&commands()==directionTimeoutCount);
 // Wrong source/instance and unavailable speed are rejected without refreshing health.
 RvcFan::Config invalid;invalid.source=150;assert(!RvcFan::valid(invalid));
 invalid.source=159;invalid.instance=0;assert(!RvcFan::valid(invalid));
 // Exhaustion stops at 151 rather than claiming 144–150 static device addresses.
 settings.config.source=151;++settings.revision;manager.update();ready(manager);
 claim.identifier=RvcFan::id(RvcFan::kClaim,151);incoming.push_back(claim);manager.update();
 assert(out.error==2&&!out.online&&!out.handler(10));
 settings.config.enabled=0;++settings.revision;manager.update();assert(!out.enabled&&!out.handler(100));
 puts("RV-C protocol and manager tests passed");
}
