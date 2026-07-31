#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PreviewScenario {
  std::string id;
  std::string title;
  bool animated = false;
  void (*render)(uint32_t nowMs) = nullptr;
};

const std::vector<PreviewScenario>& previewScenarios();
int previewScenarioIndex(const std::string& id);
