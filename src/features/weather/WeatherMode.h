#pragma once
#include "Mode.h"
class WeatherMode: public DisplayMode { public: const char* id() const override{return "weather";} uint8_t modeConst() const override{return MODE_WEATHER;} void begin(const Settings&) override; void service(const Settings&) override; void invalidate(const Settings&) override; void wake(const Settings&) override; private: uint32_t nextPoll_=0,lastAnim_=0; bool dirty_=true; void fetch(const Settings&); void render(const Settings&);};
extern WeatherMode g_weatherMode;
