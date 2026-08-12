#include "Connectivity.h"
#include "EmulatorPlatform.h"
#include "PollScheduler.h"

#include <cstdio>

namespace {
class FakeMode : public DisplayMode {
 public:
  FakeMode(const char* name, uint8_t mode, bool internet)
      : name_(name), mode_(mode), internet_(internet) {}

  const char* id() const override { return name_; }
  uint8_t modeConst() const override { return mode_; }
  uint32_t pollIntervalMs(const Settings&) const override { return 1000; }
  uint16_t pollBudgetMs(const Settings&) const override { return 4500; }
  uint8_t pollCost() const override { return internet_ ? 5 : 1; }
  bool requiresInternet() const override { return internet_; }

  PollResult poll(const Settings&, uint16_t) override {
    ++attempts;
    if (!internet_) connectivityRecord(probeOnline);
    delay(durationMs);
    return result;
  }

  uint32_t durationMs = 0;
  PollResult result = PollResult::Success;
  bool probeOnline = true;
  uint8_t attempts = 0;

 private:
  const char* name_;
  uint8_t mode_;
  bool internet_;
};

bool expect(bool condition, const char* message) {
  if (condition) return true;
  std::fprintf(stderr, "%s\n", message);
  return false;
}
}

int main() {
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Sta, -56, 640,
                    "/tmp/deskmate-connectivity-test", 0);
  emulatorSetMillis(0);
  connectivityBegin(true);
  connectivityRecord(true);

  Settings settings{};
  FakeMode provider("provider", 10, true);
  FakeMode network("network", 11, false);
  DisplayMode* modes[] = {&provider, &network};
  const bool enabled[] = {true, true};

  PollScheduler scheduler;
  scheduler.bind(modes, 2);
  scheduler.begin(settings);

  provider.durationMs = 10000;
  provider.result = PollResult::Failed;
  emulatorSetMillis(1000);
  scheduler.service(settings, enabled, &provider, nullptr);
  if (!expect(provider.attempts == 1, "initial provider attempt did not run")) return 1;

  network.probeOnline = false;
  scheduler.force(network.modeConst());
  emulatorSetMillis(12000);
  scheduler.service(settings, enabled, &provider, nullptr);
  if (!expect(connectivityState() == InternetState::Offline,
              "network probe did not publish offline state")) return 1;

  scheduler.force(provider.modeConst());
  emulatorSetMillis(12500);
  scheduler.service(settings, enabled, &provider, nullptr);
  if (!expect(provider.attempts == 1,
              "provider ran while shared connectivity was offline")) return 1;

  network.probeOnline = true;
  scheduler.force(network.modeConst());
  emulatorSetMillis(13000);
  scheduler.service(settings, enabled, &provider, nullptr);
  if (!expect(connectivityState() == InternetState::Online,
              "network probe did not publish recovered state")) return 1;

  provider.durationMs = 0;
  provider.result = PollResult::Success;
  emulatorSetMillis(14000);
  scheduler.service(settings, enabled, &provider, nullptr);
  if (!expect(provider.attempts == 2,
              "provider was not retried after connectivity recovery")) return 1;

  std::puts("Connectivity scheduler recovery test passed");
  return 0;
}
