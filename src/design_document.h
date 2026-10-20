#pragma once

#include <hiyui/draw/color.h>
#include <hiyui/core/constants.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hiydesign {

using Attributes = std::map<std::string, std::string>;

struct DesignNode {
  std::string type;
  std::string id;
  Attributes attributes;
  std::vector<DesignNode> children;
  std::vector<Attributes> items;
  bool locked = false;
  bool operator==(const DesignNode &) const = default;
};

struct DevicePreset {
  const char *id;
  const char *name;
  int width;
  int height;
  int chromeHeight;
};

inline constexpr DevicePreset devices[] = {
    {"phone", "手机", 393, 852, 32},
    {"web", "Web", 1280, 800, 36},
    {"windows", "Windows", 1366, 768, 32},
    {"linux", "Linux", 1280, 800, 32},
    {"macos", "macOS", 1440, 900, 32},
};

struct Guide {
  bool vertical = false;
  int position = 0;
  bool operator==(const Guide &) const = default;
};

struct DocumentState {
  DesignNode root;
  std::string title = "用户登录";
  int device = 1;
  int width = 1280;
  int height = 800;
  unsigned nextId = 1;
  bool borderless = false;
  std::vector<Guide> guides;
  bool operator==(const DocumentState &) const = default;
};

class DesignDocument {
public:
  DocumentState state;
  std::string selected;
  DesignNode *find(const std::string &id);
  const DesignNode *find(const std::string &id) const;
  DesignNode *parent(const std::string &id);
  const DesignNode *parent(const std::string &id) const;
  std::string assignIds(DesignNode &node);
  bool eraseSelected();
  void copySelected();
  bool paste(const std::string &parentId);
  bool reorder(const std::string &id, int direction);
  void reset(DocumentState value, bool saved);
  void checkpoint();
  bool undo();
  bool redo();
  bool canUndo() const { return historyIndex_ > 0; }
  bool canRedo() const { return historyIndex_ + 1 < history_.size(); }
  bool dirty() const { return !saved_ || *saved_ != state; }
  void markSaved() { saved_ = state; }

private:
  std::vector<DocumentState> history_;
  size_t historyIndex_ = 0;
  std::optional<DocumentState> saved_;
  std::optional<DesignNode> clipboard_;
};

std::string attribute(const DesignNode &node, const std::string &key,
                      const std::string &fallback = {});
bool automaticLayout(const DesignNode &node);
void setGeometry(DesignNode &node, hiyui::Rect rect, bool position = true);

}
