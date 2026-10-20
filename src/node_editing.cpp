#include "designer_app.h"
#include "project_io.h"
#include <algorithm>

namespace hiydesign {
using namespace hiyui;

void DesignerApp::insertComponent(const std::string &type) {
  if (!commitEdits()) return;
  const auto *entry = catalog_.find(type);
  if (!entry) { status("请先在组件箱选择组件"); return; }
  DesignDocument candidate = document_;
  DesignNode node;
  std::string error;
  if (!catalog_.create(type, node, error)) { log(error); return; }
  node.id.clear();
  auto *parent = candidate.find(candidate.selected);
  while (parent && !catalog_.accepts(candidate, *parent, node)) parent = candidate.parent(parent->id);
  if (!parent) { status("目标节点不接受该组件，或已达到数量限制"); return; }
  candidate.assignIds(node);
  candidate.selected = node.id;
  parent->children.push_back(std::move(node));
  ensurePreviewCollections(candidate.state, catalog_);
  if (!apply(std::move(candidate.state), candidate.selected, error)) log(error);
}

void DesignerApp::insertItem() {
  if (!commitEdits()) return;
  DesignDocument candidate = document_;
  auto *parent = candidate.find(candidate.selected);
  if (!parent) return;
  const auto *entry = catalog_.owner(candidate, *parent);
  if (!entry || entry->metadata.itemTag.empty()) { status("该节点没有注册的子项类型"); return; }
  if ((parent->kind == NodeKind::Item && entry->metadata.childMode != XmlChildMode::ItemTree) ||
      parent->kind == NodeKind::Template) parent = candidate.parent(parent->id);
  if (!parent) return;
  DesignNode item;
  std::string error;
  if (!catalog_.createItem(*entry, item, error)) { log(error); return; }
  item.id.clear();
  if (!catalog_.accepts(candidate, *parent, item)) { status("该节点不能再添加此类子项"); return; }
  if (!catalog_.assignItemIds(candidate, *parent, item, error)) { log(error); return; }
  candidate.assignIds(item);
  candidate.selected = item.id;
  parent->children.push_back(std::move(item));
  if (!apply(std::move(candidate.state), candidate.selected, error)) log(error);
}

void DesignerApp::previewObject() {
  if (!commitEdits()) return;
  std::string error;
  if (!surface_->previewSelection(error)) log(error);
}

void DesignerApp::editPreviewData() {
  if (!commitEdits()) return;
  root_.with([&](Box &root) {
    Box shade;
    shade.init(root).fill().floating().clickable().ref(confirmShade_)
        .style(Style{.bgFill = {.color = RGBA(0x000000, 155)}});
    VBox dialog({.padding = 16, .gap = 10, .bg = color(EditorColor::Panel), .radius = 8});
    dialog.init(shade).fillWidth().maxWidth(720).height(460).maxHeight(root.height() - 32).center();
    appearance_.background(dialog, EditorColor::Panel);
    caption(dialog, "集合预览数据", EditorColor::Text, 16).fillWidth();
    caption(dialog, "Collection 名称对应 items 集合；Field 保存每条记录的字段。", EditorColor::Muted, 12).fillWidth();
    Input editor({.multiline = true, .horizontalScroll = true, .bg = color(EditorColor::Background),
                   .textColor = color(EditorColor::Text), .fontSize = 12, .radius = 3});
    Ref<Input> input;
    editor.init(dialog).fillWidth().height(0).flex(1).text(collectionsXml(document_.state)).ref(input).focus();
    appearance_.input(editor);
    auto actions = strip(dialog, 32);
    actionButton(actions, "应用", 0, [this, input] {
      auto state = document_.state;
      std::string error;
      if (!input || !parseCollections(input->text(), state, error) ||
          !apply(std::move(state), document_.selected, error)) { log(error); return; }
      cancelConfirmation();
    });
    actionButton(actions, "取消", 0, [this] { cancelConfirmation(); });
  });
}
}
