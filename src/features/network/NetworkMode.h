#pragma once
#include "Mode.h"
class NetworkMode: public DisplayMode {public:const char* id() const override{return "network";}uint8_t modeConst()const override{return MODE_NETWORK;}void begin(const Settings&)override;void service(const Settings&)override;void invalidate(const Settings&)override;void wake(const Settings&)override;private:uint32_t next_=0;bool dirty_=true;void probe(const Settings&);void render(const Settings&);};extern NetworkMode g_networkMode;
