#include "designer_app.h"
#include <hiyui/misc/async.h>
#include <algorithm>

namespace hiydesign {
using namespace hiyui;
namespace {
size_t childIndex(const DesignNode &parent, const std::string &id) {
  const auto it = std::find_if(parent.children.begin(), parent.children.end(),
                               [&](const auto &child) { return child.id == id; });
  return static_cast<size_t>(it - parent.children.begin());
}
}

void DesignerApp::syncTreeSelection(bool reveal) {
  if (!tree_) return;
  if (reveal) {
    for (auto *parent = document_.parent(document_.selected); parent; parent = document_.parent(parent->id)) {
      for (const auto &[id, name] : treeIds_) {
        if (name != parent->id) continue;
        if (!tree_->isExpanded(id)) tree_->expandNode(id);
        break;
      }
    }
  }
  for (const auto &[id, name] : treeIds_) {
    if (name != document_.selected) continue;
    tree_->selectNode(id);
    return;
  }
  tree_->deselectAll();
}

bool DesignerApp::canMoveNode(const std::string &id, const TreeMove &destination) const {
  if (!document_.canMove(id, destination.parent, destination.index)) return false;
  return catalog_.accepts(document_, *document_.find(destination.parent), *document_.find(id));
}

std::optional<DesignerApp::TreeMove> DesignerApp::treeDestination(const std::string &id, TreeAction action) const {
  const auto *parent = document_.parent(id);
  if (!parent) return std::nullopt;
  const size_t index = childIndex(*parent, id);
  TreeMove result{parent->id, index};
  switch (action) {
  case TreeAction::Up:
    if (!index) return std::nullopt;
    result.index = index - 1;
    break;
  case TreeAction::Down:
    result.index = index + 2;
    break;
  case TreeAction::First:
    result.index = 0;
    break;
  case TreeAction::Last:
    result.index = parent->children.size();
    break;
  case TreeAction::Outdent: {
    const auto *grandparent = document_.parent(parent->id);
    if (!grandparent) return std::nullopt;
    result = {grandparent->id, childIndex(*grandparent, parent->id) + 1};
    break;
  }
  case TreeAction::Indent: {
    if (!index) return std::nullopt;
    const auto &previous = parent->children[index - 1];
    result = {previous.id, previous.children.size()};
    break;
  }
  case TreeAction::Delete:
    return std::nullopt;
  }
  return canMoveNode(id, result) ? std::optional<TreeMove>(std::move(result)) : std::nullopt;
}

std::vector<ContextMenuItem> DesignerApp::treeMenu(TreeNodeId treeId) {
  if (treeDropQueued_ || !treeIds_.contains(treeId)) return {};
  const auto id = treeIds_.at(treeId);
  if (!commitEdits() || !document_.find(id) || !surface_->select(id)) {
    syncTreeSelection();
    return {};
  }
  const auto *node = document_.find(id);
  const auto *parent = document_.parent(id);
  const bool removable = node && parent && !node->locked && !parent->locked;
  std::vector<ContextMenuItem> items;
  const auto append = [&](const char *text, TreeAction action) {
    const bool enabled = action == TreeAction::Delete ? removable : treeDestination(id, action).has_value();
    items.push_back({.text = text, .disabled = !enabled,
        .action = [this, owner = root_, id, action](MenuContext &) {
          if (owner) treeAction(id, action);
        }});
  };
  append("上移", TreeAction::Up);
  append("下移", TreeAction::Down);
  append("移至首位", TreeAction::First);
  append("移至末位", TreeAction::Last);
  items.push_back({.separator = true});
  append("提升一级", TreeAction::Outdent);
  append("移入上一节点", TreeAction::Indent);
  items.push_back({.separator = true});
  append("删除", TreeAction::Delete);
  return items;
}

bool DesignerApp::applyTreeMove(const std::string &id, const TreeMove &destination) {
  if (!canMoveNode(id, destination)) {
    status("无法移动：节点已锁定、位置未改变或目标不接受该节点");
    return false;
  }
  const bool reparent = document_.parent(id)->id != destination.parent;
  const auto point = reparent ? surface_->positionIn(id, destination.parent) : std::nullopt;
  DesignDocument candidate;
  candidate.state = document_.state;
  if (!candidate.move(id, destination.parent, destination.index)) return false;
  auto *node = candidate.find(id);
  const auto *parent = candidate.find(destination.parent);
  if (reparent && node->kind == NodeKind::Component && catalog_.visual(candidate, *node)) {
    if (catalog_.automaticLayout(*parent)) {
      node->attributes.erase("x");
      node->attributes.erase("y");
      node->attributes.erase("center");
    } else if (point) {
      node->attributes["x"] = std::to_string(point->x);
      node->attributes["y"] = std::to_string(point->y);
      node->attributes.erase("center");
    }
  }
  std::string error;
  if (!apply(std::move(candidate.state), id, error)) { log(error); return false; }
  syncTreeSelection(true);
  status("已移动 " + id);
  return true;
}

void DesignerApp::treeAction(const std::string &id, TreeAction action) {
  if (treeDropQueued_ || !commitEdits() || !document_.find(id)) return;
  if (action == TreeAction::Delete) {
    document_.selected = id;
    erase();
    return;
  }
  const auto destination = treeDestination(id, action);
  if (destination) applyTreeMove(id, *destination);
  else status("当前节点不能执行此移动");
}

void DesignerApp::queueTreeDrop(TreeNodeId source, TreeNodeId target, TreeDropPosition position) {
  if (treeDropQueued_) return;
  if (!treeIds_.contains(source) || !treeIds_.contains(target)) { refreshTree(); return; }
  const auto sourceId = treeIds_.at(source);
  const auto targetId = treeIds_.at(target);
  treeDropQueued_ = true;
  const auto result = async::postUi([this, owner = root_, sourceId, targetId, position] {
    if (!owner) return;
    bool applied = false;
    if (commitEdits()) {
      const auto *target = document_.find(targetId);
      const auto *parent = position == TreeDropPosition::Inside ? target : document_.parent(targetId);
      if (parent) {
        const size_t index = position == TreeDropPosition::Inside ? parent->children.size() :
            childIndex(*parent, targetId) + (position == TreeDropPosition::After ? 1 : 0);
        applied = applyTreeMove(sourceId, {parent->id, index});
      } else {
        status("不能移动到根组件外部");
      }
    }
    if (!applied) refreshTree();
    treeDropQueued_ = false;
  });
  if (result != async::PostStatus::Accepted) {
    treeDropQueued_ = false;
    refreshTree();
    status("拖拽操作未入队，请重试");
  }
}
}
