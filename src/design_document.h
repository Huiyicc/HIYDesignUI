#pragma once

#include <hiyui/draw/color.h>
#include <hiyui/core/constants.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hiydesign {

using Attributes = std::map<std::string, std::string>;
enum class NodeKind { Component, Item, Template };

struct DesignLayoutRules {
  Attributes base;
  std::vector<Attributes> rules;
  bool operator==(const DesignLayoutRules &) const = default;
};

struct DesignNode {
  std::string type;
  std::string id;
  Attributes attributes;
  std::vector<DesignNode> children;
  bool locked = false;
  NodeKind kind = NodeKind::Component;
  std::string prefix;
  std::optional<DesignLayoutRules> layoutRules;
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
  std::map<std::string, std::vector<Attributes>> collections;
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
  const DesignNode *clipboard() const { return clipboard_ ? &*clipboard_ : nullptr; }
  bool paste(const std::string &parentId);
  bool reorder(const std::string &id, int direction);
  bool canMove(const std::string &id, const std::string &parentId, size_t index) const;
  bool move(std::string id, std::string parentId, size_t index);
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
std::string componentKey(std::string_view namespaceUri, std::string_view localName);
std::string nodeNamespace(const DesignNode &node);
std::string nodeName(const DesignNode &node);
void setGeometry(DesignNode &node, hiyui::Rect rect, bool position = true);
bool responsiveSize(const DesignNode &node);

}
