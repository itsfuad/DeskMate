#include "WeatherMode.h"
#include "Platform.h"
#include "Clock.h"
#include "Gfx.h"
#include "TileRenderer.h"
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

WeatherMode g_weatherMode;
struct WeatherData { bool valid=false,error=false; float temp=0,apparent=0,wind=0; int code=0,humidity=0,rain=0; float hourly[8]={0}; uint32_t updated=0; } static W;

static const char* cond(int c){ if(c==0)return "CLEAR"; if(c<=3)return "CLOUDY"; if(c<=48)return "FOG"; if(c<=67)return "RAIN"; if(c<=77)return "SNOW"; if(c<=82)return "SHOWERS"; return "STORM"; }
static void cloud(TileCanvas& g,int x,int y,uint16_t c){g.fillCircle(x,y,16,c);g.fillCircle(x+18,y-7,22,c);g.fillCircle(x+43,y,17,c);g.fillRoundRect(x-12,y,72,24,10,c);}
static void drawWeather(TileCanvas& g,void* ctx){ const Settings&s=*static_cast<const Settings*>(ctx); g.fillScreen(C_BLACK); uint32_t t=millis()/80;
  g.setTextWrap(false); g.setTextColor(C_GRAY);g.setTextSize(1);g.setCursor(8,8);g.print(s.weather.city.substring(0,18));
  String tm=clockTimeStr(); if(tm.length()>5) tm=tm.substring(tm.length()-5); g.setCursor(240-gfxTextW(tm.c_str(),1)-8,8);g.print(tm);
  if(!W.valid){g.setTextColor(W.error?C_RED:C_YELLOW);g.setTextSize(2);g.setCursor(28,104);g.print(W.error?"WEATHER ERROR":"LOADING WEATHER");return;}
  bool rainy=W.code>=51&&W.code<=99; bool cloudy=W.code>=1;
  if(W.code==0){g.fillCircle(58,70,28,C_YELLOW); for(int a=0;a<360;a+=45){float r=a*PI/180;g.drawLine(58+cos(r)*36,70+sin(r)*36,58+cos(r)*46,70+sin(r)*46,C_YELLOW);}}
  if(cloudy){int dx=(t%70)-35; cloud(g,35+dx/5,72,C_DGRAY); cloud(g,95-dx/7,55,C_GRAY);}
  if(rainy){for(int i=0;i<18;i++){int x=(i*37+t*3)%240;int y=82+((i*29+t*5)%75);g.drawLine(x,y,x-3,y+9,C_BLUE);}}
  char b[20]; snprintf(b,sizeof(b),"%.0f%c",W.temp,247);g.setTextSize(5);g.setTextColor(C_WHITE);g.setCursor(12,132);g.print(b);
  g.setTextSize(2);g.setTextColor(rainy?C_BLUE:C_GREEN);g.setCursor(150,140);g.print(cond(W.code));
  g.setTextSize(1);g.setTextColor(C_GRAY);snprintf(b,sizeof(b),"FEELS %.0f%c",W.apparent,247);g.setCursor(151,166);g.print(b);snprintf(b,sizeof(b),"HUM %d%%",W.humidity);g.setCursor(151,180);g.print(b);snprintf(b,sizeof(b),"WIND %.0f km/h",W.wind);g.setCursor(151,194);g.print(b);
  int gx=8,gy=216,gw=224,gh=18; float mn=W.hourly[0],mx=mn;for(float v:W.hourly){mn=min(mn,v);mx=max(mx,v);} if(mx-mn<1)mx=mn+1; for(int i=1;i<8;i++){int x0=gx+(i-1)*gw/7,x1=gx+i*gw/7;int y0=gy+gh-(int)((W.hourly[i-1]-mn)/(mx-mn)*gh),y1=gy+gh-(int)((W.hourly[i]-mn)/(mx-mn)*gh);g.drawLine(x0,y0,x1,y1,C_GREEN);} }

void WeatherMode::fetch(const Settings&s){ String u=F("https://api.open-meteo.com/v1/forecast?latitude=");u+=String(s.weather.lat,4);u+=F("&longitude=");u+=String(s.weather.lon,4);u+=F("&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m&hourly=temperature_2m&forecast_hours=8&timezone=auto");
  std::unique_ptr<SecureClient> c(platformMakeSecureClient(4096,nullptr,512,false)); HTTPClient h; h.setTimeout(s.httpTimeout); if(!h.begin(*c,u)){W.error=true;return;} int rc=h.GET(); if(rc!=200){W.error=true;h.end();return;} JsonDocument filter; auto cu=filter["current"].to<JsonObject>();cu["temperature_2m"]=true;cu["relative_humidity_2m"]=true;cu["apparent_temperature"]=true;cu["weather_code"]=true;cu["wind_speed_10m"]=true;filter["hourly"]["temperature_2m"]=true; String payload=h.getString(); h.end(); JsonDocument d; auto e=deserializeJson(d,payload,DeserializationOption::Filter(filter)); if(e){W.error=true;return;} W.temp=d["current"]["temperature_2m"]|0;W.humidity=d["current"]["relative_humidity_2m"]|0;W.apparent=d["current"]["apparent_temperature"]|0;W.code=d["current"]["weather_code"]|0;W.wind=d["current"]["wind_speed_10m"]|0;int i=0;for(JsonVariantConst v:d["hourly"]["temperature_2m"].as<JsonArrayConst>()){if(i<8)W.hourly[i++]=v.as<float>();}W.valid=true;W.error=false;W.updated=millis();dirty_=true; }
void WeatherMode::begin(const Settings&s){nextPoll_=0;dirty_=true;}
void WeatherMode::invalidate(const Settings&s){nextPoll_=0;dirty_=true;}
void WeatherMode::wake(const Settings&s){dirty_=true;}
void WeatherMode::render(const Settings&s){gfxRenderTiled(drawWeather,const_cast<Settings*>(&s),C_BLACK);}
void WeatherMode::service(const Settings&s){uint32_t now=millis();if((int32_t)(now-nextPoll_)>=0){nextPoll_=now+(uint32_t)s.weather.pollSec*1000;fetch(s);} if(dirty_||now-lastAnim_>900){lastAnim_=now;render(s);dirty_=false;}}
