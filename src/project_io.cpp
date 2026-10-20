#include "project_io.h"
#include "component_catalog.h"
#include <pugixml.hpp>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <hiyui/xml/xml_registry.h>

namespace hiydesign {
namespace {
bool uniqueAttributes(pugi::xml_node element, std::string &error) {
  std::unordered_set<std::string> names;
  for (const auto field : element.attributes()) {
    if (!names.insert(field.name()).second) {
      error = std::string(element.name()) + " 的属性重复：" + field.name();
      return false;
    }
  }
  return true;
}

template <typename Number>
bool readNumber(pugi::xml_attribute field, Number &value) {
  const std::string_view text = field.value();
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

void writeNode(pugi::xml_node parent, const DesignNode &node, bool editor, bool inTemplate = false) {
  const auto name = nodeName(node);
  const auto qualified = node.prefix.empty() ? name : node.prefix + ":" + name;
  auto element = parent.append_child(qualified.c_str());
  const auto uri = nodeNamespace(node);
  element.append_attribute("xmlns") = uri.c_str();
  if (!node.prefix.empty()) element.append_attribute(("xmlns:" + node.prefix).c_str()) = uri.c_str();
  inTemplate = inTemplate || node.kind == NodeKind::Template;
  if (node.kind == NodeKind::Component && !inTemplate)
    element.append_attribute("id") = node.id.c_str();
  if (editor) element.append_attribute("designId") = node.id.c_str();
  for (const auto &[name, value] : node.attributes)
    element.append_attribute(name.c_str()) = value.c_str();
  if (editor && node.locked) element.append_attribute("designLocked") = true;
  for (const auto &child : node.children) writeNode(element, child, editor, inTemplate);
}

std::string toString(const pugi::xml_document &document) {
  std::ostringstream stream;
  document.save(stream, "  ", pugi::format_default, pugi::encoding_utf8);
  return stream.str();
}

using NamespaceMap = std::map<std::string, std::string>;

bool readNode(pugi::xml_node element, DesignNode &node, const ComponentCatalog &catalog,
              const ComponentEntry *itemOwner, NodeKind kind, NamespaceMap namespaces,
              std::unordered_set<std::string> &ids, size_t &count,
              int depth, bool inTemplate, std::string &error) {
  if (++count > 4096 || depth > 64) { error = "节点数量或嵌套深度超出限制"; return false; }
  if (!uniqueAttributes(element, error)) return false;
  for (const auto field : element.attributes()) {
    const std::string name = field.name();
    if (name == "xmlns") namespaces[""] = field.value();
    else if (name.starts_with("xmlns:")) namespaces[name.substr(6)] = field.value();
  }
  const std::string qualified = element.name();
  const auto colon = qualified.find(':');
  node.prefix = colon == std::string::npos ? "" : qualified.substr(0, colon);
  const auto name = colon == std::string::npos ? qualified : qualified.substr(colon + 1);
  const auto found = namespaces.find(node.prefix);
  if (found == namespaces.end()) { error = "未声明的 XML 前缀：" + node.prefix; return false; }
  node.type = componentKey(found->second, name);
  node.kind = kind;
  const auto *entry = kind == NodeKind::Component ? catalog.find(node.type) : itemOwner;
  if (!entry) { error = "未注册的组件：" + qualified; return false; }
  if (kind != NodeKind::Component &&
      (entry->namespaceUri != found->second || entry->metadata.itemTag != name)) {
    error = "子项标签与组件注册不一致：" + qualified; return false;
  }
  inTemplate = inTemplate || kind == NodeKind::Template;
  node.id = element.attribute("designId").as_string();
  if (kind == NodeKind::Component && !inTemplate && element.attribute("id")) {
    const std::string id = element.attribute("id").value();
    if (!node.id.empty() && node.id != id) { error = "组件 id 与编辑标识不一致"; return false; }
    node.id = id;
  }
  if (node.id.empty()) {
    size_t next = count;
    do { node.id = "_design" + std::to_string(next++); } while (ids.contains(node.id));
  }
  if (!ids.insert(node.id).second) { error = "节点编辑标识重复：" + node.id; return false; }
  const std::string_view locked = element.attribute("designLocked").value();
  if (element.attribute("designLocked") && locked != "true" && locked != "false") {
    error = "designLocked 必须为 true 或 false"; return false;
  }
  node.locked = locked == "true";
  for (auto field : element.attributes()) {
    const std::string attributeName = field.name();
    if (attributeName == "xmlns" || attributeName.starts_with("xmlns:") ||
        attributeName == "designId" || attributeName == "designLocked") continue;
    if (kind == NodeKind::Component && attributeName == "id") {
      if (inTemplate) { error = "模板中的组件不能声明运行时 id"; return false; }
      continue;
    }
    node.attributes.emplace(attributeName, field.value());
  }
  for (auto child : element.children()) {
    if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
      error = "节点文本请写入对应属性"; return false;
    }
    if (child.type() != pugi::node_element) continue;
    NodeKind childKind = NodeKind::Component;
    const auto mode = entry->metadata.childMode;
    if (kind == NodeKind::Component && mode == hiyui::XmlChildMode::Template) {
      const std::string childName = child.name();
      const auto split = childName.find(':');
      const auto local = split == std::string::npos ? childName : childName.substr(split + 1);
      if (local == entry->metadata.itemTag) childKind = NodeKind::Template;
    } else if ((kind == NodeKind::Component &&
                (mode == hiyui::XmlChildMode::ItemList || mode == hiyui::XmlChildMode::ItemTree)) ||
               (kind == NodeKind::Item && mode == hiyui::XmlChildMode::ItemTree)) {
      childKind = NodeKind::Item;
    }
    if (childKind != NodeKind::Component && node.children.size() >= 256) {
      error = "每个节点最多 256 个子项"; return false;
    }
    DesignNode value;
    if (!readNode(child, value, catalog, entry, childKind, namespaces, ids, count,
                   depth + 1, inTemplate, error)) return false;
    node.children.push_back(std::move(value));
  }
  return true;
}

}

std::string layoutXml(const DocumentState &document) {
  pugi::xml_document xml;
  writeNode(xml, document.root, false);
  return toString(xml);
}

std::string windowXml(const DocumentState &document) {
  pugi::xml_document xml;
  auto window = xml.append_child("Window");
  window.append_attribute("xmlns") = "urn:hiyui:ui";
  window.append_attribute("title") = document.title.c_str();
  window.append_attribute("width") = document.width;
  window.append_attribute("height") = document.height;
  window.append_attribute("borderless") = document.device >= 2 && document.borderless;
  writeNode(window, document.root, false);
  return toString(xml);
}

std::string projectXml(const DocumentState &document) {
  pugi::xml_document xml;
  auto project = xml.append_child("HIYDesignUI");
  project.append_attribute("version") = 1;
  project.append_attribute("title") = document.title.c_str();
  project.append_attribute("device") = devices[document.device].id;
  project.append_attribute("width") = document.width;
  project.append_attribute("height") = document.height;
  project.append_attribute("nextId") = document.nextId;
  project.append_attribute("borderless") = document.borderless;
  if (!document.guides.empty()) {
    auto guides = project.append_child("Guides");
    for (const auto &guide : document.guides) {
      auto item = guides.append_child("Guide");
      item.append_attribute("axis") = guide.vertical ? "x" : "y";
      item.append_attribute("position") = guide.position;
    }
  }
  if (!document.collections.empty()) {
    pugi::xml_document data;
    data.load_string(collectionsXml(document).c_str());
    project.append_copy(data.document_element());
  }
  writeNode(project, document.root, true);
  return toString(xml);
}

bool parseProject(const std::string &text, DocumentState &document, std::string &error) {
  pugi::xml_document xml;
  const auto parsed = xml.load_buffer(text.data(), text.size());
  if (!parsed) { error = parsed.description(); return false; }
  auto project = xml.document_element();
  if (std::string_view(project.name()) != "HIYDesignUI" || std::string_view(project.attribute("version").value()) != "1") {
    error = "请选择 HIYDesignUI 项目文件";
    return false;
  }
  if (!uniqueAttributes(project, error)) return false;
  int projects = 0;
  for (const auto child : xml.children()) if (child.type() == pugi::node_element) ++projects;
  if (projects != 1) { error = "文件必须只包含一个项目"; return false; }
  for (const auto field : project.attributes()) {
    const std::string_view name = field.name();
    if (name != "version" && name != "title" && name != "device" &&
        name != "width" && name != "height" && name != "nextId" && name != "borderless") {
      error = "不支持的项目属性：" + std::string(name);
      return false;
    }
  }
  DocumentState value;
  if (const auto field = project.attribute("borderless")) {
    const std::string_view text = field.value();
    if (text != "true" && text != "false") { error = "borderless 必须为 true 或 false"; return false; }
    value.borderless = text == "true";
  }
  value.title = project.attribute("title").as_string("未命名界面");
  const std::string device = project.attribute("device").as_string();
  bool knownDevice = false;
  for (int i = 0; i < 5; ++i)
    if (device == devices[i].id) { value.device = i; knownDevice = true; }
  if (!readNumber(project.attribute("width"), value.width) ||
      !readNumber(project.attribute("height"), value.height) ||
      (project.attribute("nextId") && !readNumber(project.attribute("nextId"), value.nextId)) ||
      !knownDevice || value.width < 240 || value.width > 3840 ||
      value.height < 240 || value.height > 2160 || value.nextId == 0) {
    error = "项目视口尺寸或设备配置无效";
    return false;
  }
  pugi::xml_node root;
  bool hasGuides = false;
  bool hasCollections = false;
  int roots = 0;
  for (const auto child : project.children()) {
    if (child.type() == pugi::node_element && std::string_view(child.name()) == "Guides") {
      if (hasGuides || child.first_attribute()) { error = "辅助线配置无效"; return false; }
      hasGuides = true;
      for (const auto item : child.children()) {
        if (item.type() == pugi::node_comment || item.type() == pugi::node_pi) continue;
        const std::string_view axis = item.attribute("axis").value();
        Guide guide;
        guide.vertical = axis == "x";
        if (item.type() != pugi::node_element || std::string_view(item.name()) != "Guide" ||
            item.first_child() || (axis != "x" && axis != "y") ||
            !uniqueAttributes(item, error) || !readNumber(item.attribute("position"), guide.position) ||
            guide.position < 0 || guide.position > (guide.vertical ? 3840 : 2160) || value.guides.size() >= 256) {
          error = "辅助线方向、坐标或数量无效"; return false;
        }
        for (const auto field : item.attributes()) {
          const std::string_view name = field.name();
          if (name != "axis" && name != "position") { error = "不支持的辅助线属性"; return false; }
        }
        value.guides.push_back(guide);
      }
    } else if (child.type() == pugi::node_element && std::string_view(child.name()) == "Collections") {
      if (hasCollections) { error = "重复的预览数据"; return false; }
      hasCollections = true;
      std::ostringstream data;
      child.print(data);
      if (!parseCollections(data.str(), value, error)) return false;
    } else if (child.type() == pugi::node_element) { root = child; ++roots; }
    else if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
      error = "项目节点不支持文本内容";
      return false;
    }
  }
  if (!root || roots != 1) { error = "项目必须包含一个根组件"; return false; }
  std::unordered_set<std::string> ids;
  size_t count = 0;
  const ComponentCatalog catalog;
  if (!readNode(root, value.root, catalog, nullptr, NodeKind::Component,
                {{"", hiyui::XmlBuiltinNamespaceUri}}, ids, count, 1, false, error)) return false;
  document = std::move(value);
  return true;
}

bool readProject(const std::filesystem::path &path, DocumentState &document, std::string &error) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream || stream.tellg() < 0 || stream.tellg() > 4 * 1024 * 1024) {
    error = "无法读取项目，或文件超过 4 MiB";
    return false;
  }
  std::string text(static_cast<size_t>(stream.tellg()), '\0');
  stream.seekg(0);
  if (!stream.read(text.data(), static_cast<std::streamsize>(text.size()))) {
    error = "项目读取失败";
    return false;
  }
  return parseProject(text, document, error);
}

bool writeText(const std::filesystem::path &path, const std::string &text, std::string &error) {
  std::error_code ec;
  std::filesystem::path staging;
  for (unsigned i = 0; i < 1000; ++i) {
    staging = path;
    staging += ".save-" + std::to_string(i);
    if (std::filesystem::create_directory(staging, ec)) break;
    if (ec) { error = "无法创建保存目录：" + ec.message(); return false; }
    if (i == 999) { error = "保存临时目录已占用"; return false; }
  }
  const auto temporary = staging / "new.xml";
  const auto backup = staging / "previous.xml";
  const auto cleanup = [&] {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    std::filesystem::remove(staging, ignored);
  };
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  stream.flush();
  stream.close();
  if (!stream) { error = "文件写入失败"; cleanup(); return false; }
  const bool existing = std::filesystem::exists(path, ec);
  if (ec) { error = ec.message(); cleanup(); return false; }
  if (existing) {
    if (!std::filesystem::is_regular_file(path, ec)) {
      error = "保存目标不是普通文件"; cleanup(); return false;
    }
    std::filesystem::rename(path, backup, ec);
    if (ec) { error = "无法替换原文件：" + ec.message(); cleanup(); return false; }
  }
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    error = "保存失败：" + ec.message();
    if (existing) {
      std::filesystem::rename(backup, path, ec);
      if (ec) error += "；原文件保存在 " + pathText(backup);
    }
    cleanup();
    return false;
  }
  if (existing) std::filesystem::remove(backup, ec);
  cleanup();
  return true;
}

std::string pathText(const std::filesystem::path &path) {
  const auto text = path.generic_u8string();
  return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

void relocateResources(DocumentState &document, const std::filesystem::path &sourceBase,
                       const std::filesystem::path &destinationBase) {
  const ComponentCatalog catalog;
  DesignDocument model;
  model.state = document;
  const auto relocate = [&](std::string &text) {
    if (text.empty() || text.find("://") != std::string::npos || text.front() == '{') return;
    auto path = std::filesystem::u8path(text);
    if (path.is_relative()) path = sourceBase / path;
    path = path.lexically_normal();
    if (!destinationBase.empty()) {
      const auto relative = path.lexically_relative(destinationBase);
      if (!relative.empty()) path = relative;
    }
    text = pathText(path);
  };
  const auto visit = [&](auto &&self, DesignNode &node) -> void {
    for (const auto &field : catalog.attributes(model, node))
      if (field.kind == hiyui::XmlAttributeKind::Source && node.attributes.contains(field.name))
        relocate(node.attributes[field.name]);
    for (auto &child : node.children) self(self, child);
  };
  visit(visit, model.state.root);
  document = std::move(model.state);
}

bool parseNodeXml(const std::string &text, const ComponentCatalog &catalog, DesignNode &node,
                  std::string &error, const ComponentEntry *owner, NodeKind kind) {
  pugi::xml_document xml;
  const auto parsed = xml.load_buffer(text.data(), text.size());
  if (!parsed || !xml.document_element()) { error = parsed.description(); return false; }
  size_t roots = 0;
  for (const auto element : xml.children())
    if (element.type() == pugi::node_element) ++roots;
  if (roots != 1) { error = "初始 XML 必须包含一个根节点"; return false; }
  DesignNode value;
  std::unordered_set<std::string> ids;
  size_t count = 0;
  NamespaceMap namespaces{{"", owner ? owner->namespaceUri : hiyui::XmlBuiltinNamespaceUri}};
  if (owner && !owner->prefix.empty()) namespaces[owner->prefix] = owner->namespaceUri;
  if (!readNode(xml.document_element(), value, catalog, owner, kind, std::move(namespaces),
                ids, count, 1, false, error)) return false;
  node = std::move(value);
  return true;
}

std::string collectionsXml(const DocumentState &document) {
  pugi::xml_document xml;
  auto root = xml.append_child("Collections");
  for (const auto &[name, records] : document.collections) {
    auto collection = root.append_child("Collection");
    collection.append_attribute("name") = name.c_str();
    for (const auto &record : records) {
      auto item = collection.append_child("Record");
      for (const auto &[key, value] : record) {
        auto field = item.append_child("Field");
        field.append_attribute("name") = key.c_str();
        field.append_attribute("value") = value.c_str();
      }
    }
  }
  return toString(xml);
}

bool parseCollections(const std::string &text, DocumentState &document, std::string &error) {
  pugi::xml_document xml;
  if (text.size() > 4 * 1024 * 1024 || !xml.load_buffer(text.data(), text.size()) ||
      std::string_view(xml.document_element().name()) != "Collections") {
    error = "预览数据需要 Collections XML"; return false;
  }
  const auto shape = [&](pugi::xml_node node, std::initializer_list<std::string_view> fields, bool children) {
    if (!uniqueAttributes(node, error)) return false;
    for (const auto field : node.attributes())
      if (std::find(fields.begin(), fields.end(), field.name()) == fields.end()) {
        error = "不支持的数据属性：" + std::string(field.name()); return false;
      }
    for (const auto child : node.children())
      if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata ||
          (!children && child.type() == pugi::node_element)) {
        error = "数据节点含有不支持的内容：" + std::string(node.name()); return false;
      }
    return true;
  };
  size_t roots = 0;
  for (const auto child : xml.children()) if (child.type() == pugi::node_element) ++roots;
  if (roots != 1) { error = "数据文件必须只包含一个 Collections"; return false; }
  if (!shape(xml.document_element(), {}, true)) return false;
  std::map<std::string, std::vector<Attributes>> collections;
  size_t count = 0;
  for (const auto collection : xml.document_element().children()) {
    if (collection.type() != pugi::node_element) continue;
    const std::string name = collection.attribute("name").value();
    if (!shape(collection, {"name"}, true)) return false;
    if (std::string_view(collection.name()) != "Collection" || name.empty() ||
        collections.contains(name) || !uniqueAttributes(collection, error)) {
      error = "集合必须有唯一的名称"; return false;
    }
    auto &records = collections[name];
    for (const auto record : collection.children()) {
      if (record.type() != pugi::node_element) continue;
      if (!shape(record, {}, true)) return false;
      if (std::string_view(record.name()) != "Record" || ++count > 4096) {
        error = "集合只允许 Record 节点，总数不超过 4096"; return false;
      }
      Attributes values;
      for (const auto field : record.children()) {
        if (field.type() != pugi::node_element) continue;
        if (!shape(field, {"name", "value"}, false)) return false;
        const std::string key = field.attribute("name").value();
        if (std::string_view(field.name()) != "Field" || key.empty() || key == "index" ||
            !field.attribute("value") || !uniqueAttributes(field, error) ||
            values.size() >= 256 || !values.emplace(key, field.attribute("value").value()).second) {
          error = "记录字段须有唯一的 name 和 value，index 为保留名称"; return false;
        }
      }
      records.push_back(std::move(values));
    }
  }
  document.collections = std::move(collections);
  return true;
}

void ensurePreviewCollections(DocumentState &document, const ComponentCatalog &catalog) {
  const auto visit = [&](auto &&self, const DesignNode &node) -> void {
    const auto *entry = node.kind == NodeKind::Component ? catalog.find(node.type) : nullptr;
    if (entry && entry->metadata.childMode == hiyui::XmlChildMode::Template) {
      const auto expression = attribute(node, "items");
      if (expression.starts_with("{collection:") && expression.ends_with('}')) {
        const auto name = expression.substr(12, expression.size() - 13);
        if (!document.collections.contains(name)) {
          Attributes fields;
          const auto collect = [&](auto &&self, const DesignNode &value) -> void {
            const auto specs = catalog.attributes(value.type);
            for (const auto &[key, text] : value.attributes) {
              size_t offset = 0;
              while ((offset = text.find("{item:", offset)) != std::string::npos) {
                const auto end = text.find('}', offset + 6);
                if (end == std::string::npos) break;
                const auto name = text.substr(offset + 6, end - offset - 6);
                const auto spec = std::find_if(specs.begin(), specs.end(), [&](const auto &s) { return s.name == key; });
                std::string sample = "示例内容";
                if (spec != specs.end() && spec->kind != hiyui::XmlAttributeKind::String) {
                  sample = defaultText(*spec);
                  if (sample.empty()) sample = "1";
                }
                if (name != "index") fields.try_emplace(name, sample);
                offset = end + 1;
              }
            }
            for (const auto &child : value.children) self(self, child);
          };
          for (const auto &child : node.children) collect(collect, child);
          document.collections[name] = {fields, fields, fields};
        }
      }
    }
    for (const auto &child : node.children) self(self, child);
  };
  visit(visit, document.root);
}

hiyui::XmlContext previewContext(const DocumentState &document) {
  hiyui::XmlContext context;
  for (const auto &[name, records] : document.collections) {
    std::vector<std::vector<hiyui::XmlCollectionField>> fields(records.size());
    std::vector<hiyui::XmlCollectionItem> items;
    for (size_t i = 0; i < records.size(); ++i) {
      for (const auto &[key, value] : records[i]) fields[i].push_back({key.c_str(), value.c_str()});
      items.push_back({fields[i].data(), fields[i].size()});
    }
    context.setCollection(name.c_str(), items);
  }
  return context;
}

DocumentState emptyDocument() {
  DocumentState value;
  value.title = "未命名界面";
  value.root = {"Box", "root", {{"fill", "both"}, {"bg", "#f3f4f6"}}};
  return value;
}

DocumentState loginDocument() {
  const std::string xml = R"xml(<HIYDesignUI version="1" title="用户登录" device="web" width="1280" height="800" nextId="1">
  <Box id="root" fill="both" padding="24" bg="#f3f4f6">
    <Card id="loginCard" fill="width" maxWidth="340" center="true" padding="30" gap="8" bg="#ffffff" radius="16" shadow="true">
      <Label id="title" text="用户登录" fontSize="22" color="#16181d"/>
      <Label id="subtitle" text="欢迎回来，请登录您的账户以继续" fontSize="12" color="#8e939c" fill="width"/>
      <Box id="titleGap" height="10" fill="width" bg="#ffffff"/>
      <Label id="accountLabel" text="用户名" fontSize="12" color="#3d434e"/>
      <Input id="account" placeholder="请输入用户名" height="38" fill="width" bg="#fafbfc" color="#3d434e" placeholderColor="#adb2bc" fontSize="13" radius="9"/>
      <Label id="passwordLabel" text="密码" fontSize="12" color="#3d434e"/>
      <Input id="password" placeholder="••••••••" password="true" height="38" fill="width" bg="#fafbfc" color="#3d434e" placeholderColor="#adb2bc" fontSize="13" radius="9"/>
      <Checkbox id="remember" text="记住我" checked="true" fontSize="12" color="#4a505b" fill="width"/>
      <HBox id="actions" fill="width" gap="10" bg="#ffffff">
        <Button id="login" text="登录" width="0" height="40" flex="1" bg="#0e7ad4" color="#ffffff" fontSize="13" radius="9"/>
        <Button id="cancel" text="取消" variant="Secondary" width="0" height="40" flex="1" bg="#f1f2f5" color="#3d434e" fontSize="13" radius="9"/>
      </HBox>
    </Card>
  </Box>
</HIYDesignUI>)xml";
  DocumentState value;
  std::string error;
  if (!parseProject(xml, value, error)) return emptyDocument();
  return value;
}

}
