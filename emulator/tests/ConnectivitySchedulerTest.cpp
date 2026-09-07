#include "Connectivity.h"
#include "EmulatorPlatform.h"
#include "PollScheduler.h"
#include "CrashBreadcrumbs.h"

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

  void pollResultChanged(const Settings&, PollResult value) override {
    lastReportedResult = value;
    ++resultReports;
  }

  uint32_t durationMs = 0;
  PollResult result = PollResult::Success;
  bool probeOnline = true;
  uint8_t attempts = 0;
  uint8_t resultReports = 0;
  PollResult lastReportedResult = PollResult::Skipped;

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
  EmulatorConstraints limits;
  auto configure = [&]() {
    emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Sta, -56, 640,
                      "/tmp/deskmate-connectivity-test", 0, {}, limits);
  };
  limits.freeHeapBytes = 13119;
  configure();
  if (!expect(!platformTlsMemoryReady(), "TLS admitted below heap threshold")) return 1;
  limits.freeHeapBytes = 13120;
  limits.maximumBlockBytes = 6200;
  configure();
  if (!expect(platformTlsMemoryReady(), "TLS rejected at threshold")) return 1;
  limits.maximumBlockBytes = 6199;
  configure();
  if (!expect(!platformTlsMemoryReady(), "TLS admitted below block threshold")) return 1;
  limits.freeHeapBytes = 15456;
  limits.maximumBlockBytes = 15160;
  configure();
  if (!expect(platformTlsMemoryReady(), "historically working heap rejected construction")) return 1;
  {
    EmulatorSecureClient client;
    if (!expect(ESP.getFreeHeap() == 9256 && platformTlsConnectMemoryReady(),
                "constructor stack not charged before connection")) return 1;
    if (!expect(emulatorReserveHeap(3000) && !platformTlsConnectMemoryReady(),
                "late pressure was not rejected")) return 1;
    if (!expect(!client.connect("fixture.invalid", 443) && emulatorNetworkRequests() == 0,
                "low-headroom connect reached network")) return 1;
    emulatorReleaseHeap(3000);
  }
  if (!expect(ESP.getFreeHeap() == 15456, "stack reservation leaked")) return 1;
  limits.freeHeapBytes = 13120;
  configure();
  {
    EmulatorSecureClient first;
    if (!expect(ESP.getFreeHeap() == 6920 && platformTlsConnectMemoryReady(),
                "post-construction boundary failed")) return 1;
    {
      EmulatorSecureClient second;
      first.stop();
      if (!expect(ESP.getFreeHeap() == 6920, "shared stack charged twice or freed by stop")) return 1;
    }
    if (!expect(emulatorReserveHeap(513) && !platformTlsConnectMemoryReady(),
                "late allocation did not reject connection")) return 1;
    emulatorReleaseHeap(513);
    if (!expect(platformTlsConnectMemoryReady(), "late allocation recovery failed")) return 1;
  }
  if (!expect(ESP.getFreeHeap() == 13120, "last destructor did not release stack")) return 1;
  emulatorFailNothrowAfter(0);
  if (!expect(platformMakeSecureClient() == nullptr && ESP.getFreeHeap() == 13120,
              "factory allocation failure leaked memory")) return 1;
  {
    std::unique_ptr<SecureClient> recovered(platformMakeSecureClient());
    if (!expect(bool(recovered), "factory did not recover")) return 1;
  }
  limits.freeHeapBytes = 8192;
  limits.maximumBlockBytes = 1024;
  limits.freeStackBytes = 1024;
  configure();
  if (!expect(platformMutationMemoryReady(), "mutation rejected at threshold")) return 1;
  limits.freeStackBytes = 1023;
  configure();
  if (!expect(!platformMutationMemoryReady(), "mutation admitted below stack threshold")) return 1;
  limits.freeStackBytes = 1024;
  limits.freeHeapBytes = 8191;
  configure();
  if (!expect(!platformMutationMemoryReady(), "mutation admitted below heap threshold")) return 1;
  limits.freeHeapBytes = 8192;
  limits.maximumBlockBytes = 1023;
  configure();
  if (!expect(!platformMutationMemoryReady(), "mutation admitted below block threshold")) return 1;

  CrashBreadcrumbs record;
  for (uint32_t i = 0; i < 20; ++i) record.add(1, i, i, 100, 50, 20);
  if (!expect(record.valid() && record.entries[(record.count - 1) % 8].detail == 19,
              "breadcrumb ring lost newest entry")) return 1;
  record.entries[0].heap ^= 1;
  if (!expect(!record.valid(), "breadcrumb corruption accepted")) return 1;
  record = CrashBreadcrumbs();
  record.count = UINT32_MAX;
  record.add(2, 42, 0, 0, 0, 0);
  if (!expect(record.valid() && record.entries[(record.count - 1) % 8].detail == 42,
              "breadcrumb sequence rollover failed")) return 1;

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
  if (!expect(provider.resultReports == 1 &&
              provider.lastReportedResult == PollResult::Failed,
              "visible mode did not receive failed poll result")) return 1;

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

  // Begin immediately before wrap; the job itself crosses UINT32_MAX.
  emulatorSetMillis(UINT32_MAX - 100);
  connectivityBegin(false);
  PollScheduler rollover;
  FakeMode rolling("rollover", 12, false);
  rolling.durationMs = 200;
  DisplayMode* one[] = {&rolling};
  const bool selected[] = {true};
  rollover.bind(one, 1);
  rollover.begin(settings);
  rollover.forceAll();
  rollover.service(settings, selected, &rolling, nullptr);
  if (!expect(rolling.attempts == 1 && rollover.lastJobDurationMs() == 200,
              "job duration across millis rollover is wrong")) return 1;
  rolling.durationMs = 0;
  emulatorSetMillis(200);
  rollover.service(settings, selected, &rolling, nullptr);
  if (!expect(rolling.attempts == 1, "rollover deadline ran early")) return 1;
  emulatorSetMillis(1200);
  rollover.service(settings, selected, &rolling, nullptr);
  if (!expect(rolling.attempts == 2, "rollover deadline never ran")) return 1;

  std::puts("Connectivity scheduler recovery and rollover tests passed");
  return 0;
}
