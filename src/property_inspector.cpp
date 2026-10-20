#include "property_inspector.h"
#include "project_io.h"
#include <hiyui/widget/checkbox.h>
#include <hiyui/widget/dropdown.h>
#include <algorithm>
#include <set>

namespace hiydesign {
using namespace hiyui;
namespace {
bool layoutField(const std::string &name) {
  static const std::set<std::string> names = {
      "x", "y", "width", "height", "maxWidth", "fill", "center", "flex", "padding",
      "gap", "margin", "align", "crossAlign", "scroll", "scrollable", "hidden"};
  return names.contains(name);
}

PropertyItem group(PropertyId id, const char *name) {
  PropertyItem item;
  item.id = id;
  item.name = name;
  item.kind = PropertyKind::Group;
  return item;
}

PropertyItem textItem(PropertyId id, std::string name, std::string value) {
  PropertyItem item;
  item.id = id;
  item.name = std::move(name);
  item.value = std::move(value);
  item.editor = "darkText";
  return item;
}
}

void PropertyInspector::init(Widget &parent) {
  PropertyGrid grid({.rowHeight = 32, .nameWidth = 112, .narrowWidth = 0,
                     .showSearch = true, .showDescription = true,
                     .bg = 0x252526, .textColor = 0xcccccc,
                     .headerBg = 0x2d2d30, .groupBg = 0x2d2d30, .selectedBg = 0x04395e,
                     .hoverBg = 0x37373d, .pressedBg = 0x04395e,
                     .borderColor = 0x3f3f46, .secondaryTextColor = 0x9d9d9d});
  grid.init(parent).fill().ref(grid_).radius(0);
  appearance_.bind(grid, [grid = grid_](Widget &, const EditorPalette &p) {
    using C = EditorColor;
    grid->palette({.bg = p[C::Panel], .textColor = p[C::Text], .headerBg = p[C::Raised],
        .groupBg = p[C::Raised], .selectedBg = p[C::Selected], .hoverBg = p[C::Hover],
        .pressedBg = p[C::Selected], .borderColor = p[C::Border], .secondaryTextColor = p[C::Muted]});
    grid->radius(0);
  });
  grid.registerEditor("darkText", [this](Widget &host, PropertyEditor editor) {
    Input input({.bg = 0x1e1e1e, .textColor = 0xcccccc, .fontSize = 12, .radius = 0});
    input.init(host).fill().text(editor.text()).ref(textInput_);
    input.padding(6, 3);
    appearance_.input(input);
    input.onChange([editor](const std::string &text) { editor.setText(text); });
    input.on(EventCode::Ready, [editor] { editor.commit(); });
    editor.focusTarget(input);
  });
  grid.registerEditor("darkChoice", [this](Widget &host, PropertyEditor editor) {
    const auto property = editor.property();
    if (!property) return;
    std::vector<DropdownItem> items;
    std::vector<PropertyValue> values;
    int selected = HIYUI_NO_SELECTION;
    for (const auto &option : property->options) {
      if (option.value == editor.value()) selected = static_cast<int>(items.size());
      items.push_back({.text = option.label.c_str()});
      values.push_back(option.value);
    }
    Dropdown input({.selectedIndex = selected, .bg = 0x2d2d30, .textColor = 0xcccccc,
                    .fontSize = 12, .radius = 0, .popupMode = PopupMode::Inline});
    input.init(host).fill().items(items).selectedIndex(selected);
    Ref<Dropdown> choice;
    input.ref(choice);
    appearance_.bind(input, [choice](Widget &, const EditorPalette &p) {
      using C = EditorColor;
      choice->bg(p[C::Raised]).textColor(p[C::Text]);
      choice->part(DropdownPart::Popup, Part({.normal = {.bg = p[C::Panel], .textColor = p[C::Text], .borderColor = p[C::Border], .fontSize = 12}}));
      choice->part(DropdownPart::Item, Part({.normal = {.bg = p[C::Panel], .textColor = p[C::Text], .fontSize = 12},
          .hovered = {.bg = p[C::Hover]}, .focused = {.bg = p[C::Hover]},
          .checked = {.bg = p[C::Selected]}, .pressed = {.bg = p[C::Selected]}}));
    });
    input.onChange([editor, values = std::move(values)](int index) {
      if (index >= 0 && index < static_cast<int>(values.size()) && editor.setValue(values[index])) editor.commit();
    });
    editor.focusTarget(input);
  });
  grid.registerEditor("darkBool", [this](Widget &host, PropertyEditor editor) {
    const auto value = editor.value();
    const auto *checked = value.get<bool>();
    Checkbox input({.checked = checked && *checked, .checkedColor = 0x0078d4,
                    .textColor = 0xcccccc, .fontSize = 12});
    input.init(host).fillWidth();
    appearance_.foreground(input, EditorColor::Text);
    input.onChange([editor](bool checked) { if (editor.setValue(checked)) editor.commit(); });
    editor.focusTarget(input);
  });
  grid.commitHandler([this](PropertyCommitRequest &request) { submit(request); });
}

void PropertyInspector::refresh(bool preserveEditor) {
  if (!grid_) return;
  std::string editingField, editingText;
  PropertyValue editingValue;
  bool textEditor = false;
  const auto active = grid_->editor();
  if (preserveEditor && nodeId_ == document_.selected && active && fields_.contains(active.id())) {
    editingField = fields_.at(active.id());
    editingText = active.text();
    editingValue = active.value();
    const auto property = active.property();
    textEditor = property && property->editor == "darkText";
  }
  fields_.clear();
  nodeId_ = document_.selected;
  const auto *node = document_.find(nodeId_);
  if (!node) { grid_->properties({}); return; }
  std::vector<PropertyItem> groups;
  groups.push_back(group(1, "标识"));
  groups.push_back(group(2, "布局"));
  groups.push_back(group(3, "内容与外观"));
  auto identity = textItem(10, node->kind == NodeKind::Component ? "id" : "编辑标识", node->id);
  identity.readOnly = node->locked;
  groups[0].children.push_back(std::move(identity));
  fields_[10] = "$id";
  auto type = textItem(11, "类型", nodeName(*node));
  type.readOnly = true;
  groups[0].children.push_back(std::move(type));
  PropertyItem locked;
  locked.id = 12;
  locked.name = "锁定";
  locked.kind = PropertyKind::Boolean;
  locked.editor = "darkBool";
  locked.value = node->locked;
  groups[0].children.push_back(std::move(locked));
  fields_[12] = "$locked";
  const auto *parent = document_.parent(nodeId_);
  const auto *parentEntry = parent ? catalog_.find(parent->type) : nullptr;
  auto layout = textItem(13, "父布局", parent ? catalog_.automaticLayout(*parent) ? "自动排列" : "绝对定位" : "视口根节点");
  layout.readOnly = true;
  groups[0].children.push_back(std::move(layout));
  auto uri = textItem(14, "namespace", nodeNamespace(*node));
  uri.readOnly = true;
  groups[0].children.push_back(std::move(uri));
  auto children = textItem(15, "子节点", std::to_string(node->children.size()));
  children.readOnly = true;
  children.description = "在组件树选择条目；使用新增子项、添加组件及排序按钮编辑。";
  groups[0].children.push_back(std::move(children));
  PropertyId id = 100;
  for (const auto &spec : catalog_.attributes(document_, *node)) {
    if (spec.name == "id" && node->kind == NodeKind::Component) continue;
    auto item = textItem(id, spec.name, attribute(*node, spec.name));
    item.description = spec.description;
    if (!item.description.empty()) item.description += "\n";
    item.description += spec.required ? "必填属性。" : "默认值：" + defaultText(spec) + "；重置后使用组件默认值。";
    if (spec.name == "width" || spec.name == "height" || spec.name == "maxWidth")
      item.description += "尺寸单位为逻辑像素；-1 使用该组件的自动尺寸规则。";
    item.defaultValue = PropertyValue("");
    item.modified = node->attributes.contains(spec.name);
    item.readOnly = node->locked || (parent && catalog_.automaticLayout(*parent) && (spec.name == "x" || spec.name == "y" || spec.name == "center"));
    if (parentEntry && parentEntry->metadata.design.layout == XmlDesignLayout::Managed &&
        (spec.name == "width" || spec.name == "height")) item.readOnly = true;
    if (spec.kind == XmlAttributeKind::Boolean || spec.kind == XmlAttributeKind::Enumeration) {
      item.kind = PropertyKind::Enum;
      item.editor = "darkChoice";
      item.options.push_back({"默认", ""});
      if (spec.kind == XmlAttributeKind::Boolean) {
        item.options.push_back({"true", "true"});
        item.options.push_back({"false", "false"});
        if (node->attributes.contains(spec.name)) {
          bool value = false;
          if (xmlTryParseBool(attribute(*node, spec.name), value)) item.value = value ? "true" : "false";
        }
      } else {
        for (const auto &option : spec.enumValues) item.options.push_back({option.name, option.name});
      }
    }
    fields_[id++] = spec.name;
    groups[layoutField(spec.name) ? 1 : 2].children.push_back(std::move(item));
  }
  grid_->properties(std::move(groups));
  if (!editingField.empty()) {
    for (const auto &[fieldId, field] : fields_) {
      if (field != editingField) continue;
      if (grid_->edit(fieldId)) {
        const auto editor = grid_->editor();
        if (textEditor) {
          editor.setText(editingText);
          if (textInput_) textInput_->text(editingText);
        }
        else editor.setValue(editingValue);
      }
      break;
    }
  }
}

bool PropertyInspector::commit() {
  if (!grid_) return true;
  const auto editor = grid_->editor();
  return !editor || editor.commit();
}

void PropertyInspector::resetSelected() {
  if (grid_) grid_->reset(grid_->selected());
}

void PropertyInspector::submit(PropertyCommitRequest &request) {
  const auto &change = request.change();
  const auto field = fields_.find(change.id);
  if (field == fields_.end()) { request.reject("属性已失效"); return; }
  DesignDocument candidate;
  candidate.state = document_.state;
  auto *node = candidate.find(nodeId_);
  if (!node) { request.reject("组件已删除"); return; }
  const auto &name = field->second;
  if (node->locked && name != "$locked") { request.reject("请先解除组件锁定"); return; }
  const auto *text = change.value.get<std::string>();
  std::string selection = nodeId_;
  if (name == "$locked") {
    const auto *value = change.value.get<bool>();
    if (!value) { request.reject("锁定值无效"); return; }
    node->locked = *value;
  } else if (!text) {
    request.reject("属性值无效"); return;
  } else if (name == "$id") {
    if (text->empty() || (*text != nodeId_ && candidate.find(*text))) {
      request.reject("id 不能为空或重复"); return;
    }
    selection = *text;
    node->id = *text;
  } else {
    const auto specs = catalog_.attributes(candidate, *node);
    const auto spec = std::find_if(specs.begin(), specs.end(), [&](const auto &value) { return value.name == name; });
    const bool emptyString = spec != specs.end() && spec->kind == XmlAttributeKind::String;
    if (change.origin == PropertyChangeOrigin::Reset || (text->empty() && !emptyString)) node->attributes.erase(name);
    else node->attributes[name] = *text;
  }
  std::string error = catalog_.validate(layoutXml(candidate.state));
  if (!error.empty()) { request.reject(error); return; }
  if (!apply || !apply(std::move(candidate.state), selection, error)) {
    request.reject(error.empty() ? "预览更新失败" : error); return;
  }
  nodeId_ = selection;
  request.accept();
}

}
