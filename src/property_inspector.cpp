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

bool ruleControls(const DesignNode &node, const std::string &name) {
  if (!node.layoutRules) return false;
  const auto &base = node.layoutRules->base;
  if (name == "fill" || name == "size") return base.contains("width") || base.contains("height");
  if (name == "padding") return base.contains("paddingX") || base.contains("paddingY");
  return base.contains(name);
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
  if (nodeId_ != document_.selected) ruleIndex_ = -1;
  nodeId_ = document_.selected;
  const auto *node = document_.find(nodeId_);
  if (!node) { grid_->properties({}); return; }
  if (!node->layoutRules || ruleIndex_ >= static_cast<int>(node->layoutRules->rules.size())) ruleIndex_ = -1;
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
    if (ruleControls(*node, spec.name)) {
      item.readOnly = true;
      item.description += "此字段由响应式布局控制，请在响应式分组编辑。";
    }
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
  appendLayoutProperties(groups, *node);
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

void PropertyInspector::appendLayoutProperties(std::vector<PropertyItem> &groups, const DesignNode &node) {
  const auto *entry = node.kind == NodeKind::Component ? catalog_.find(node.type) : nullptr;
  if (!entry || entry->metadata.resourceOnly) return;
  auto controls = group(4, "响应式布局");
  auto mode = textItem(40, "编辑范围", std::to_string(ruleIndex_));
  mode.kind = PropertyKind::Enum;
  mode.editor = "darkChoice";
  mode.options.push_back({"基础值", "-1"});
  if (node.layoutRules)
    for (size_t index = 0; index < node.layoutRules->rules.size(); ++index)
      mode.options.push_back({"条件 " + std::to_string(index + 1), std::to_string(index)});
  controls.children.push_back(std::move(mode));
  fields_[40] = "$ruleMode";
  auto action = textItem(41, "条件操作", "");
  action.kind = PropertyKind::Enum;
  action.editor = "darkChoice";
  action.options = {{"选择操作", ""}, {"新增条件", "add"}};
  if (ruleIndex_ >= 0) action.options.push_back({"删除当前条件", "remove"});
  if (node.layoutRules) action.options.push_back({"移除全部规则", "clear"});
  action.readOnly = node.locked;
  controls.children.push_back(std::move(action));
  fields_[41] = "$ruleAction";
  const auto &metadata = catalog_.schema().document;
  const auto &specs = ruleIndex_ < 0 ? metadata.layoutRules.attributes : metadata.layoutRule.attributes;
  const Attributes empty;
  const auto &values = !node.layoutRules ? empty : ruleIndex_ < 0
      ? node.layoutRules->base : node.layoutRules->rules[static_cast<size_t>(ruleIndex_)];
  auto fields = group(5, ruleIndex_ < 0 ? "响应式基础值" : "当前条件差异");
  const auto *parent = document_.parent(node.id);
  const auto *parentEntry = parent ? catalog_.find(parent->type) : nullptr;
  PropertyId id = fields_.rbegin()->first + 1;
  for (const auto &spec : specs) {
    const bool boundary = spec.name.find("Host") != std::string::npos;
    const auto current = values.find(spec.name);
    auto item = textItem(id, spec.name, current == values.end() ? "" : current->second);
    item.defaultValue = PropertyValue("");
    item.modified = current != values.end();
    item.readOnly = node.locked;
    item.description = spec.description;
    if (parentEntry && parentEntry->metadata.design.layout == XmlDesignLayout::Managed &&
        (spec.name == "width" || spec.name == "height")) {
      item.readOnly = true;
      item.description += "尺寸由父布局分配。";
    }
    if (boundary) item.description += "宿主逻辑内容尺寸；下界包含、上界不包含，上界 -1 表示不限。";
    else if (ruleIndex_ >= 0) {
      const auto base = node.layoutRules->base.find(spec.name);
      item.description += base == node.layoutRules->base.end() ? "请先在基础值中设置此字段。"
          : "基础值：" + base->second + "；清空或重置后继承基础值。";
    } else if (spec.name != "host") item.description += "清空时使用组件原有配置；有条件差异时需保留基础值。";
    if (spec.name == "host" || spec.kind == XmlAttributeKind::Enumeration || spec.kind == XmlAttributeKind::Boolean) {
      item.kind = PropertyKind::Enum;
      item.editor = "darkChoice";
      item.options.push_back({spec.name == "host" ? "直接父容器" : ruleIndex_ < 0 ? "组件配置" : "继承基础值", ""});
      if (spec.name == "host") {
        for (auto *ancestor = document_.parent(node.id); ancestor; ancestor = document_.parent(ancestor->id)) {
          if (ancestor->kind != NodeKind::Component) continue;
          bool inTemplate = false;
          for (auto *scope = document_.parent(ancestor->id); scope; scope = document_.parent(scope->id))
            if (scope->kind == NodeKind::Template) inTemplate = true;
          const auto *hostEntry = catalog_.find(ancestor->type);
          if (!inTemplate && hostEntry && !hostEntry->metadata.resourceOnly)
            item.options.push_back({ancestor->id, ancestor->id});
        }
      } else if (spec.kind == XmlAttributeKind::Boolean) {
        item.options.push_back({"true", "true"});
        item.options.push_back({"false", "false"});
        bool value = false;
        if (current != values.end() && xmlTryParseBool(current->second, value)) item.value = value ? "true" : "false";
      } else for (const auto &option : spec.enumValues) item.options.push_back({option.name, option.name});
    }
    fields_[id++] = "$rule." + spec.name;
    fields.children.push_back(std::move(item));
  }
  groups.insert(groups.begin() + 2, std::move(controls));
  groups.insert(groups.begin() + 3, std::move(fields));
}

void PropertyInspector::submitLayout(PropertyCommitRequest &request, const std::string &field) {
  const auto &change = request.change();
  const auto *text = change.value.get<std::string>();
  if (!text) { request.reject("规则值无效"); return; }
  DesignDocument candidate;
  candidate.state = document_.state;
  auto *node = candidate.find(nodeId_);
  if (!node) { request.reject("组件已删除"); return; }
  if (field == "$ruleMode") {
    int64_t index = -1;
    if (!xmlTryParseInt(*text, index) || index < -1 ||
        (index >= 0 && (!node->layoutRules || index >= static_cast<int64_t>(node->layoutRules->rules.size())))) {
      request.reject("条件已失效"); return;
    }
    ruleIndex_ = static_cast<int>(index);
    request.accept();
    if (refreshRequested) refreshRequested();
    return;
  }
  if (node->locked) { request.reject("请先解除组件锁定"); return; }
  int nextIndex = ruleIndex_;
  if (field == "$ruleAction") {
    if (text->empty()) { request.accept(); return; }
    if (*text == "add") {
      if (!node->layoutRules) node->layoutRules.emplace();
      nextIndex = static_cast<int>(node->layoutRules->rules.size());
      node->layoutRules->rules.emplace_back();
    } else if (*text == "clear") {
      node->layoutRules.reset();
      nextIndex = -1;
    } else if (*text == "remove" && node->layoutRules && ruleIndex_ >= 0 &&
               ruleIndex_ < static_cast<int>(node->layoutRules->rules.size())) {
      node->layoutRules->rules.erase(node->layoutRules->rules.begin() + ruleIndex_);
      nextIndex = -1;
    } else { request.reject("条件已失效"); return; }
  } else {
    const auto name = field.substr(6);
    if (!node->layoutRules) node->layoutRules.emplace();
    auto &rules = *node->layoutRules;
    if (ruleIndex_ >= static_cast<int>(rules.rules.size())) { request.reject("条件已失效"); return; }
    auto &values = ruleIndex_ < 0 ? rules.base : rules.rules[static_cast<size_t>(ruleIndex_)];
    if (text->empty() || change.origin == PropertyChangeOrigin::Reset) values.erase(name);
    else {
      if (ruleIndex_ >= 0 && name.find("Host") == std::string::npos && !rules.base.contains(name)) {
        request.reject("请先为 " + name + " 设置响应式基础值"); return;
      }
      values[name] = *text;
    }
    if (rules.base.empty() && rules.rules.empty()) node->layoutRules.reset();
  }
  std::string error = catalog_.validate(layoutXml(candidate.state));
  if (!error.empty()) { request.reject(error); return; }
  if (!apply || !apply(std::move(candidate.state), nodeId_, error)) {
    request.reject(error.empty() ? "预览更新失败" : error); return;
  }
  ruleIndex_ = nextIndex;
  request.accept();
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
  if (field->second.starts_with("$rule")) { submitLayout(request, field->second); return; }
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
    const auto renameHost = [&](auto &&self, DesignNode &value) -> void {
      if (value.layoutRules) {
        auto host = value.layoutRules->base.find("host");
        if (host != value.layoutRules->base.end() && host->second == nodeId_) host->second = selection;
      }
      for (auto &child : value.children) self(self, child);
    };
    renameHost(renameHost, *node);
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
