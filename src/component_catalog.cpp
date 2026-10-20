#include "component_catalog.h"
#include <algorithm>

namespace hiydesign {
using namespace hiyui;

ComponentCatalog::ComponentCatalog() {
  registerBuiltinXmlWidgets();
  schema_ = xmlSchemaFromRegistry(XmlRegistry::instance().snapshot());
  entries_ = {{"Button", "常用组件", 120, 40}, {"Label", "常用组件", 120, 28},
              {"Input", "常用组件", 220, 38}, {"Checkbox", "常用组件", 150, 32},
              {"Radio", "常用组件", 150, 32}, {"Switch", "常用组件", 48, 28},
              {"Dropdown", "常用组件", 180, 36}, {"Slider", "常用组件", 200, 20},
              {"Progress", "常用组件", 200, 20}, {"Image", "常用组件", 160, 120},
              {"Box", "容器与布局", 240, 180}, {"VBox", "容器与布局", 240, 180},
              {"HBox", "容器与布局", 320, 100}, {"FlowBox", "容器与布局", 320, 180},
              {"Card", "容器与布局", 280, 200}};
  for (auto &entry : entries_)
    for (const auto &module : schema_.modules)
      if (module.namespaceUri == XmlBuiltinNamespaceUri)
        for (const auto &widget : module.widgets)
          if (widget.localName == entry.type) entry.metadata = widget;
  std::erase_if(entries_, [](const auto &entry) { return entry.metadata.localName.empty(); });
}

const ComponentEntry *ComponentCatalog::find(const std::string &type) const {
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                              [&](const auto &entry) { return entry.type == type; });
  return it == entries_.end() ? nullptr : &*it;
}

bool ComponentCatalog::container(const std::string &type) const {
  const auto *entry = find(type);
  return entry && entry->metadata.childMode == XmlChildMode::WidgetChildren && type != "Button";
}

DesignNode ComponentCatalog::create(const std::string &type) const {
  DesignNode node;
  node.type = type;
  const auto *entry = find(type);
  if (!entry) return node;
  setGeometry(node, {0, 0, entry->width, entry->height});
  if (type == "Button") node.attributes["text"] = "按钮";
  if (type == "Label") node.attributes["text"] = "文本标签";
  if (type == "Input") node.attributes["placeholder"] = "请输入内容";
  if (type == "Checkbox" || type == "Radio") node.attributes["text"] = "选项";
  if (type == "Slider" || type == "Progress") node.attributes["value"] = "50";
  if (type == "Dropdown") node.items = {{{"text", "选项一"}}, {{"text", "选项二"}}, {{"text", "选项三"}}};
  if (container(type)) {
    node.attributes["bg"] = "#ffffff";
    node.attributes["padding"] = "12";
    if (type != "Box") node.attributes["gap"] = "8";
  }
  return node;
}

std::vector<XmlAttributeSpec> ComponentCatalog::attributes(const std::string &type) const {
  const auto *entry = find(type);
  if (!entry) return {};
  auto result = entry->metadata.attributes;
  for (const auto &value : schema_.commonAttributes)
    if (std::none_of(result.begin(), result.end(), [&](const auto &item) { return item.name == value.name; }))
      result.push_back(value);
  return result;
}

std::string ComponentCatalog::validate(const std::string &xml) const {
  const auto result = xmlValidate(xml, schema_);
  if (result) return {};
  std::string message;
  for (const auto &diagnostic : result.diagnostics) {
    if (!message.empty()) message += "\n";
    message += diagnostic.message;
  }
  return message.empty() ? "布局无效" : message;
}

}
