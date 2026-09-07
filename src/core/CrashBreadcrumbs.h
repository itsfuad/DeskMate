#pragma once
#include "Platform.h"
#include <stddef.h>

// Opt in with DESKMATE_CRASH_TRACE; production builds allocate no trace state.
// User RTC words 0..31 are reserved for eboot. No flash writes, pointers,
// credentials, or dynamically allocated state belong in this record.
struct CrashBreadcrumbs {
  struct Entry {
    uint32_t ms, heap, block, stack;
    uint32_t operation, detail;
  };
  uint32_t version = 1;
  uint32_t count = 0;
  Entry entries[8] = {};
  uint32_t checksum = 0;

  uint32_t hash() const {
    const auto* bytes = reinterpret_cast<const uint8_t*>(this);
    uint32_t value = 2166136261UL;
    for (size_t i = 0; i < offsetof(CrashBreadcrumbs, checksum); ++i)
      value = (value ^ bytes[i]) * 16777619UL;
    return value;
  }
  bool valid() const { return version == 1 && checksum == hash(); }
  void add(uint32_t operation, uint32_t detail, uint32_t ms,
           uint32_t heap, uint32_t block, uint32_t stack) {
    entries[count % 8] = {ms, heap, block, stack, operation, detail};
    // Keep the ring index valid even when the sequence counter wraps.
    if (count == UINT32_MAX) count = 8;
    else ++count;
    checksum = hash();
  }
};
static_assert(sizeof(CrashBreadcrumbs) % 4 == 0 && sizeof(CrashBreadcrumbs) <= 384,
              "RTC breadcrumb record must fit after eboot reservation");

enum class CrashOperation : uint32_t {
  Boot = 1, Settings, Display, Network, PollBegin, PollEnd,
  HttpConnect, HttpHeaders, ConfigMutation, Invalidate, Parse,
  WebStatus, WebRoot, RenderBegin, RenderEnd,
  HttpConnected, HttpWriteBegin, HttpWriteEnd, TlsAdmissionRejected,
  RadarTestBegin, RadarTestEnd
};

inline CrashBreadcrumbs& crashCurrent() {
  static CrashBreadcrumbs record;
  return record;
}

inline void crashMark(CrashOperation operation, uint32_t detail = 0) {
#if defined(DESKMATE_ESP8266) && defined(DESKMATE_CRASH_TRACE)
  CrashBreadcrumbs& record = crashCurrent();
  record.add(static_cast<uint32_t>(operation), detail, millis(),
             ESP.getFreeHeap(), platformMaxFreeBlock(), platformFreeContStack());
  ESP.rtcUserMemoryWrite(32, reinterpret_cast<uint32_t*>(&record), sizeof(record));
#else
  (void)operation;
  (void)detail;
#endif
}

inline void crashBegin(String& report) {
#if defined(DESKMATE_ESP8266) && defined(DESKMATE_CRASH_TRACE)
  CrashBreadcrumbs previous;
  if (ESP.rtcUserMemoryRead(32, reinterpret_cast<uint32_t*>(&previous), sizeof(previous)) &&
      previous.valid()) {
    report += "\nRTC breadcrumbs v1 (previous boot; not a backtrace):\n";
    const uint32_t size = previous.count < 8 ? previous.count : 8;
    for (uint32_t i = 0; i < size; ++i) {
      const auto& entry = previous.entries[(previous.count - size + i) % 8];
      char line[128];
      snprintf(line, sizeof(line), "ms=%lu op=%lu detail=%lu heap=%lu block=%lu stack=%lu\n",
               (unsigned long)entry.ms, (unsigned long)entry.operation,
               (unsigned long)entry.detail, (unsigned long)entry.heap,
               (unsigned long)entry.block, (unsigned long)entry.stack);
      report += line;
    }
  } else report += "\nRTC breadcrumbs unavailable (power loss, old format, or invalid checksum)\n";
  crashCurrent() = CrashBreadcrumbs();
  crashMark(CrashOperation::Boot);
#else
  (void)report;
#endif
}
