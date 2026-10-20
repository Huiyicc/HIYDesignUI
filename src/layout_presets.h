#pragma once

#include <span>

namespace hiydesign {

struct LayoutPreset {
  const char *name = "";
  const char *xml = "";
};

std::span<const LayoutPreset> layoutPresets();

}
