#include "design_document.h"
#include <algorithm>
#include <cctype>
#include <utility>
#include <hiyui/xml/xml_registry.h>

namespace hiydesign {

bool responsiveSize(const DesignNode &node) {
  if (!node.layoutRules) return false;
  for (const char *name : {"width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight", "flex"})
    if (node.layoutRules->base.contains(name)) return true;
  return false;
}

namespace {
template <typename Node> Node *findNode(Node &node, const std::string &id) {
  if (node.id == id) return &node;
  for (auto &child : node.children)
    if (auto *found = findNode(child, id)) return found;
  return nullptr;
}

template <typename Node> Node *findParent(Node &node, const std::string &id) {
  for (auto &child : node.children) {
    if (child.id == id) return &node;
    if (auto *found = findParent(child, id)) return found;
  }
  return nullptr;
}
}

DesignNode *DesignDocument::find(const std::string &id) {
  return id.empty() ? nullptr : findNode(state.root, id);
}
const DesignNode *DesignDocument::find(const std::string &id) const {
  return id.empty() ? nullptr : findNode(state.root, id);
}
DesignNode *DesignDocument::parent(const std::string &id) {
  return findParent(state.root, id);
}
const DesignNode *DesignDocument::parent(const std::string &id) const {
  return findParent(state.root, id);
}

std::string DesignDocument::assignIds(DesignNode &node) {
  std::map<std::string, std::string> renamed;
  const auto assign = [&](auto &&self, DesignNode &value) -> void {
    const auto oldId = value.id;
    std::string prefix = nodeName(value);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    do {
      value.id = prefix + std::to_string(state.nextId++);
      if (state.nextId == 0) state.nextId = 1;
    } while (find(value.id));
    if (!oldId.empty()) renamed[oldId] = value.id;
    for (auto &child : value.children) self(self, child);
  };
  assign(assign, node);
  const auto remap = [&](auto &&self, DesignNode &value) -> void {
    if (value.layoutRules) {
      auto host = value.layoutRules->base.find("host");
      if (host != value.layoutRules->base.end() && renamed.contains(host->second))
        host->second = renamed.at(host->second);
    }
    for (auto &child : value.children) self(self, child);
  };
  remap(remap, node);
  return node.id;
}

bool DesignDocument::eraseSelected() {
  auto *owner = parent(selected);
  if (!owner || owner->locked) return false;
  auto *node = find(selected);
  if (!node || node->locked) return false;
  const auto id = selected;
  selected = owner->id;
  std::erase_if(owner->children, [&](const auto &child) { return child.id == id; });
  checkpoint();
  return true;
}

void DesignDocument::copySelected() {
  if (const auto *node = find(selected)) clipboard_ = *node;
}

bool DesignDocument::paste(const std::string &parentId) {
  auto *owner = find(parentId);
  if (!owner || owner->locked || !clipboard_) return false;
  DesignNode node = *clipboard_;
  node.locked = false;
  assignIds(node);
  selected = node.id;
  owner->children.push_back(std::move(node));
  checkpoint();
  return true;
}

bool DesignDocument::reorder(const std::string &id, int direction) {
  auto *owner = parent(id);
  auto *node = find(id);
  if (!owner || !node || owner->locked || node->locked) return false;
  auto it = std::find_if(owner->children.begin(), owner->children.end(),
                         [&](const auto &child) { return child.id == id; });
  const auto index = std::distance(owner->children.begin(), it);
  const auto next = index + direction;
  if (next < 0 || next >= static_cast<ptrdiff_t>(owner->children.size())) return false;
  std::swap(owner->children[static_cast<size_t>(index)], owner->children[static_cast<size_t>(next)]);
  checkpoint();
  return true;
}

bool DesignDocument::canMove(const std::string &id, const std::string &parentId, size_t index) const {
  const auto *node = find(id);
  const auto *source = parent(id);
  const auto *target = find(parentId);
  if (!node || !source || !target || node->locked || source->locked || target->locked ||
      index > target->children.size() || findNode(*node, parentId)) return false;
  if (source == target) {
    const auto it = std::find_if(source->children.begin(), source->children.end(),
                                 [&](const auto &child) { return child.id == id; });
    const auto current = static_cast<size_t>(it - source->children.begin());
    if (index == current || index == current + 1) return false;
  }
  return true;
}

bool DesignDocument::move(std::string id, std::string parentId, size_t index) {
  if (!canMove(id, parentId, index)) return false;
  auto *source = parent(id);
  auto it = std::find_if(source->children.begin(), source->children.end(),
                         [&](const auto &child) { return child.id == id; });
  if (source->id == parentId && static_cast<size_t>(it - source->children.begin()) < index) --index;
  DesignNode node = std::move(*it);
  source->children.erase(it);
  auto *target = find(parentId);
  target->children.insert(target->children.begin() + static_cast<ptrdiff_t>(index), std::move(node));
  selected = std::move(id);
  checkpoint();
  return true;
}

void DesignDocument::reset(DocumentState value, bool saved) {
  state = std::move(value);
  selected = state.root.id;
  history_ = {state};
  historyIndex_ = 0;
  saved_ = saved ? std::optional<DocumentState>(state) : std::nullopt;
}

void DesignDocument::checkpoint() {
  if (!history_.empty() && history_[historyIndex_] == state) return;
  history_.resize(historyIndex_ + (history_.empty() ? 0 : 1));
  history_.push_back(state);
  if (history_.size() > 100) history_.erase(history_.begin());
  historyIndex_ = history_.size() - 1;
}

bool DesignDocument::undo() {
  if (!canUndo()) return false;
  state = history_[--historyIndex_];
  if (!find(selected)) selected = state.root.id;
  return true;
}
bool DesignDocument::redo() {
  if (!canRedo()) return false;
  state = history_[++historyIndex_];
  if (!find(selected)) selected = state.root.id;
  return true;
}

std::string attribute(const DesignNode &node, const std::string &key,
                      const std::string &fallback) {
  const auto it = node.attributes.find(key);
  return it == node.attributes.end() ? fallback : it->second;
}

std::string componentKey(std::string_view uri, std::string_view name) {
  return uri == hiyui::XmlBuiltinNamespaceUri ? std::string(name) :
      "{" + std::string(uri) + "}" + std::string(name);
}

std::string nodeNamespace(const DesignNode &node) {
  const auto end = node.type.rfind('}');
  return node.type.starts_with('{') && end != std::string::npos ?
      node.type.substr(1, end - 1) : hiyui::XmlBuiltinNamespaceUri;
}

std::string nodeName(const DesignNode &node) {
  const auto end = node.type.rfind('}');
  return node.type.starts_with('{') && end != std::string::npos ? node.type.substr(end + 1) : node.type;
}

void setGeometry(DesignNode &node, hiyui::Rect rect, bool position) {
  if (position) {
    node.attributes["x"] = std::to_string(rect.x);
    node.attributes["y"] = std::to_string(rect.y);
    node.attributes.erase("center");
  }
  node.attributes["width"] = std::to_string(std::max(1, rect.w));
  node.attributes["height"] = std::to_string(std::max(1, rect.h));
  node.attributes.erase("fill");
  node.attributes.erase("flex");
}

}
