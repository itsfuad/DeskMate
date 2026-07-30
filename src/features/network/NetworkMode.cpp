#include "NetworkMode.h"
#include "Platform.h"
#include "Gfx.h"
#include "TileRenderer.h"
#include "Net.h"
#include <Arduino_GFX_Library.h>
NetworkMode g_networkMode;
static uint16_t samples[60];static bool oks[60];static uint8_t head=0,count=0;static uint32_t outageStart=0,lastChange=0;static bool online=false;
static void drawNet(TileCanvas&g,void*ctx){const Settings&s=*static_cast<const Settings*>(ctx);g.fillScreen(C_BLACK);g.setTextWrap(false);g.setTextSize(1);g.setTextColor(C_GRAY);g.setCursor(8,8);g.print("NETWORK GUARDIAN");g.setCursor(180,8);g.print(netRSSI());g.print(" dBm");g.fillCircle(22,54,10,online?C_GREEN:C_RED);g.setTextSize(3);g.setTextColor(online?C_GREEN:C_RED);g.setCursor(43,42);g.print(online?"ONLINE":"OFFLINE");uint16_t last=count?samples[(head+59)%60]:0;char b[32];g.setTextSize(2);g.setTextColor(C_WHITE);snprintf(b,sizeof(b),"%u ms",last);g.setCursor(12,88);g.print(b);g.setTextSize(1);g.setTextColor(C_GRAY);g.setCursor(12,111);g.print(s.network.probeHost);g.print(':');g.print(s.network.probePort);
 int x0=8,y0=145,w=224,h=68;g.drawRect(x0,y0,w,h,C_DGRAY);uint16_t mx=1;for(int i=0;i<count;i++)if(oks[i])mx=max(mx,samples[i]);for(int i=0;i<count;i++){int idx=(head+60-count+i)%60;int x=x0+2+i*(w-4)/59;if(!oks[idx])g.drawFastVLine(x,y0+2,h-4,C_RED);else{int hh=constrain((int)samples[idx]*(h-6)/max(100,(int)mx),2,h-6);g.drawFastVLine(x,y0+h-3-hh,hh,C_GREEN);}}g.setCursor(10,224);g.setTextColor(C_GRAY);if(!online&&outageStart){snprintf(b,sizeof(b),"OUTAGE %lus",(unsigned long)((millis()-outageStart)/1000));g.print(b);}else{g.print("LAST 10 MINUTES");}}
void NetworkMode::probe(const Settings& s) {
  WiFiClient client;
  const uint32_t startedAtMs = millis();
  const bool connected = client.connect(
      s.network.probeHost.c_str(),
      s.network.probePort);
  const uint32_t elapsedMs = millis() - startedAtMs;
  client.stop();

  const uint16_t latencyMs = connected
      ? static_cast<uint16_t>(elapsedMs > 9999UL ? 9999UL : elapsedMs)
      : 0;

  samples[head] = latencyMs;
  oks[head] = connected;
  head = (head + 1U) % 60U;
  if (count < 60U) ++count;

  if (connected != online) {
    online = connected;
    lastChange = millis();
    outageStart = connected ? 0 : lastChange;
  } else if (!connected && outageStart == 0) {
    // The first probe can fail while the default state is already offline.
    outageStart = millis();
  }

  dirty_ = true;
}
void NetworkMode::begin(const Settings&){next_=0;dirty_=true;}void NetworkMode::invalidate(const Settings&){next_=0;dirty_=true;}void NetworkMode::wake(const Settings&){dirty_=true;}void NetworkMode::render(const Settings&s){gfxRenderTiled(drawNet,const_cast<Settings*>(&s),C_BLACK);}void NetworkMode::service(const Settings&s){uint32_t n=millis();if((int32_t)(n-next_)>=0){next_=n+(uint32_t)s.network.pollSec*1000;probe(s);}if(dirty_){render(s);dirty_=false;}}
