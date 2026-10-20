#include "component_catalog.h"
#include "project_io.h"
#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace hiydesign {
using namespace hiyui;

ComponentCatalog::ComponentCatalog() {
  registerBuiltinXmlWidgets();
  schema_ = xmlSchemaFromRegistry(XmlRegistry::instance().snapshot());
  for (const auto &module : schema_.modules) {
    for (const auto &widget : module.widgets) {
      ComponentEntry entry;
      entry.type = componentKey(module.namespaceUri, widget.localName);
      entry.namespaceUri = module.namespaceUri;
      entry.localName = widget.localName;
      entry.prefix = module.preferredPrefix;
      entry.category = widget.design.category.empty() ?
          (module.name.empty() ? "其他组件" : module.name) : widget.design.category;
      entry.width = widget.design.width;
      entry.height = widget.design.height;
      entry.metadata = widget;
      entries_.push_back(std::move(entry));
    }
  }
}

const ComponentEntry *ComponentCatalog::find(const std::string &type) const {
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                              [&](const auto &entry) { return entry.type == type; });
  return it == entries_.end() ? nullptr : &*it;
}

const ComponentEntry *ComponentCatalog::owner(const DesignDocument &document, const DesignNode &node) const {
  const auto *current = &node;
  while (current && current->kind != NodeKind::Component) current = document.parent(current->id);
  return current ? find(current->type) : nullptr;
}

bool ComponentCatalog::container(const std::string &type) const {
  const auto *entry = find(type);
  return entry && entry->metadata.childMode == XmlChildMode::WidgetChildren;
}

bool ComponentCatalog::container(const DesignDocument &document, const DesignNode &node) const {
  if (node.kind == NodeKind::Component) return container(node.type);
  if (node.kind == NodeKind::Template) return true;
  const auto *entry = owner(document, node);
  return entry && entry->metadata.itemAllowsChildren;
}

bool ComponentCatalog::automaticLayout(const DesignNode &node) const {
  const auto *entry = node.kind == NodeKind::Component ? find(node.type) : nullptr;
  return entry && entry->metadata.design.layout != XmlDesignLayout::Absolute;
}

bool ComponentCatalog::visual(const DesignDocument &document, const DesignNode &node) const {
  for (auto *current = &node; current; current = document.parent(current->id)) {
    if (current->kind == NodeKind::Template) return false;
    const auto *entry = current->kind == NodeKind::Component ? find(current->type) : nullptr;
    if (entry && entry->metadata.resourceOnly) return false;
  }
  if (node.kind == NodeKind::Item) {
    const auto *entry = owner(document, node);
    return entry && entry->metadata.itemAllowsChildren;
  }
  return true;
}

bool ComponentCatalog::accepts(const DesignDocument &document, const DesignNode &parent,
                               const DesignNode &child) const {
  const auto *entry = owner(document, parent);
  if (!entry || parent.locked) return false;
  const auto &metadata = entry->metadata;
  const auto existing = [&](NodeKind kind) {
    return std::count_if(parent.children.begin(), parent.children.end(), [&](const auto &node) {
      return node.kind == kind && (child.id.empty() || node.id != child.id);
    });
  };
  if (child.kind != NodeKind::Component) {
    if (nodeNamespace(child) != entry->namespaceUri || nodeName(child) != metadata.itemTag) return false;
    if (child.kind == NodeKind::Template)
      return parent.kind == NodeKind::Component && metadata.childMode == XmlChildMode::Template && !existing(NodeKind::Template);
    return (parent.kind == NodeKind::Component &&
            (metadata.childMode == XmlChildMode::ItemList || metadata.childMode == XmlChildMode::ItemTree)) ||
           (parent.kind == NodeKind::Item && metadata.childMode == XmlChildMode::ItemTree);
  }
  const auto *childEntry = find(child.type);
  if (!childEntry) return false;
  for (auto *ancestor = &parent; ancestor; ancestor = document.parent(ancestor->id))
    if (ancestor->kind == NodeKind::Template &&
        (childEntry->metadata.resourceOnly || childEntry->metadata.childMode == XmlChildMode::Template)) return false;
  const auto &restriction = childEntry->metadata.structure;
  if (parent.kind == NodeKind::Template)
    return restriction.parent.empty() && !childEntry->metadata.resourceOnly && !existing(NodeKind::Component);
  if (parent.kind == NodeKind::Item)
    return metadata.itemAllowsChildren && restriction.parent.empty();
  if (childEntry->metadata.resourceOnly)
    return metadata.childMode == XmlChildMode::WidgetChildren || metadata.childMode == XmlChildMode::Template;
  if (metadata.childMode != XmlChildMode::WidgetChildren) return false;
  if (!restriction.parent.empty() &&
      (entry->namespaceUri != childEntry->namespaceUri || entry->localName != restriction.parent)) return false;
  if (!metadata.structure.children.empty() &&
      (entry->namespaceUri != childEntry->namespaceUri ||
       std::find(metadata.structure.children.begin(), metadata.structure.children.end(), childEntry->localName) == metadata.structure.children.end())) return false;
  if (restriction.maxPerParent) {
    const auto count = std::count_if(parent.children.begin(), parent.children.end(), [&](const auto &node) {
      return node.kind == NodeKind::Component && node.type == child.type && (child.id.empty() || node.id != child.id);
    });
    if (static_cast<size_t>(count) >= restriction.maxPerParent) return false;
  }
  return true;
}

bool ComponentCatalog::create(const std::string &type, DesignNode &node, std::string &error) const {
  node = {};
  node.type = type;
  const auto *entry = find(type);
  if (!entry) { error = "组件尚未注册：" + type; return false; }
  if (!entry->metadata.design.xml.empty() &&
      !parseNodeXml(entry->metadata.design.xml, *this, node, error, entry)) return false;
  if (node.type != type) { error = "初始 XML 的标签与组件注册不一致：" + type; return false; }
  node.prefix = entry->prefix;
  if (!entry->metadata.resourceOnly) {
    for (const auto &spec : attributes(type)) {
      if (spec.name == "width" && !node.attributes.contains("width")) node.attributes[spec.name] = std::to_string(entry->width);
      if (spec.name == "height" && !node.attributes.contains("height")) node.attributes[spec.name] = std::to_string(entry->height);
    }
  }
  return true;
}

void ComponentCatalog::setGeometry(DesignNode &node, Rect bounds, bool position) const {
  hiydesign::setGeometry(node, bounds, position);
  const auto specs = attributes(node.type);
  for (const char *name : {"x", "y", "width", "height"})
    if (std::none_of(specs.begin(), specs.end(), [&](const auto &spec) { return spec.name == name; }))
      node.attributes.erase(name);
}

bool ComponentCatalog::createItem(const ComponentEntry &entry, DesignNode &node, std::string &error) const {
  node = {};
  node.type = componentKey(entry.namespaceUri, entry.metadata.itemTag);
  node.kind = entry.metadata.childMode == XmlChildMode::Template ? NodeKind::Template : NodeKind::Item;
  node.prefix = entry.prefix;
  return entry.metadata.design.itemXml.empty() ||
         parseNodeXml(entry.metadata.design.itemXml, *this, node, error, &entry, node.kind);
}

bool ComponentCatalog::assignItemIds(const DesignDocument &document, const DesignNode &parent,
                                      DesignNode &node, std::string &error) const {
  if (node.kind != NodeKind::Item) return true;
  const auto *component = &parent;
  while (component && component->kind != NodeKind::Component) component = document.parent(component->id);
  const auto *entry = component ? find(component->type) : nullptr;
  if (!entry) return false;
  std::unordered_set<uint64_t> used;
  const auto collect = [&](auto &&self, const DesignNode &value) -> void {
    if (&value == &node) return;
    if (value.kind == NodeKind::Item) {
      uint64_t id = 0;
      if (xmlTryParseUInt(attribute(value, "id"), id)) used.insert(id);
      for (const auto &child : value.children) self(self, child);
    }
  };
  for (const auto &child : component->children) collect(collect, child);
  const auto assign = [&](auto &&self, DesignNode &value, bool top) -> bool {
    if (value.kind != NodeKind::Item) return true;
    const auto &fields = top && !entry->metadata.topItemAttributes.empty() ?
        entry->metadata.topItemAttributes : entry->metadata.itemAttributes;
    for (const auto &field : fields) {
      if (field.name != "id" || (!field.required && !value.attributes.contains("id"))) continue;
      if (field.kind != XmlAttributeKind::Integer && field.kind != XmlAttributeKind::UnsignedInteger) continue;
      uint64_t id = field.kind == XmlAttributeKind::Integer ? uint64_t(std::max<int64_t>(1, field.minInteger)) : 1;
      const uint64_t maximum = field.kind == XmlAttributeKind::Integer ? uint64_t(std::max<int64_t>(0, field.maxInteger)) : field.maxUnsigned;
      while (id < maximum && used.contains(id)) ++id;
      if (id > maximum || used.contains(id)) { error = "子项 id 已无可用值"; return false; }
      value.attributes["id"] = std::to_string(id);
      used.insert(id);
    }
    for (auto &child : value.children) if (!self(self, child, false)) return false;
    return true;
  };
  return assign(assign, node, parent.kind == NodeKind::Component);
}

std::vector<XmlAttributeSpec> ComponentCatalog::attributes(const std::string &type) const {
  const auto *entry = find(type);
  if (!entry) return {};
  auto result = entry->metadata.attributes;
  if (!entry->metadata.resourceOnly)
    for (const auto &value : schema_.commonAttributes)
      if (std::none_of(result.begin(), result.end(), [&](const auto &item) { return item.name == value.name; }))
        result.push_back(value);
  return result;
}

std::vector<XmlAttributeSpec> ComponentCatalog::attributes(const DesignDocument &document, const DesignNode &node) const {
  if (node.kind == NodeKind::Component) return attributes(node.type);
  const auto *entry = owner(document, node);
  if (!entry || node.kind == NodeKind::Template) return {};
  const auto *parent = document.parent(node.id);
  return parent && parent->kind == NodeKind::Component && !entry->metadata.topItemAttributes.empty() ?
      entry->metadata.topItemAttributes : entry->metadata.itemAttributes;
}

std::string defaultText(const XmlAttributeSpec &spec) {
  const auto &value = spec.defaultValue;
  if (!value.present || value.nullValue) return {};
  switch (spec.kind) {
  case XmlAttributeKind::Integer: return std::to_string(value.integer);
  case XmlAttributeKind::UnsignedInteger: return std::to_string(value.unsignedInteger);
  case XmlAttributeKind::Boolean: return value.boolean ? "true" : "false";
  case XmlAttributeKind::Number: return std::to_string(value.number);
  case XmlAttributeKind::Enumeration:
    for (const auto &entry : spec.enumValues) if (entry.value == value.integer) return entry.name;
    return {};
  case XmlAttributeKind::Color: {
    std::ostringstream stream;
    stream << "0x" << std::hex << value.unsignedInteger;
    return stream.str();
  }
  default: return value.string;
  }
}

std::string ComponentCatalog::validate(const std::string &xml) const {
  const auto result = xmlValidate(xml, schema_);
  if (result) return {};
  std::string message;
  for (const auto &diagnostic : result.diagnostics) {
    if (!message.empty()) message += "\n";
    message += diagnostic.element + (diagnostic.attribute.empty() ? "" : "." + diagnostic.attribute) + ": " + diagnostic.message;
  }
  return message.empty() ? "布局无效" : message;
}
}
