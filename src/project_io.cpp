#include "project_io.h"
#include <pugixml.hpp>
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

void writeNode(pugi::xml_node parent, const DesignNode &node, bool editor) {
  auto element = parent.append_child(node.type.c_str());
  element.append_attribute("id").set_value(node.id.c_str());
  for (const auto &[name, value] : node.attributes)
    element.append_attribute(name.c_str()).set_value(value.c_str());
  if (editor && node.locked) element.append_attribute("designLocked") = true;
  for (const auto &item : node.items) {
    auto child = element.append_child("Item");
    for (const auto &[name, value] : item)
      child.append_attribute(name.c_str()).set_value(value.c_str());
  }
  for (const auto &child : node.children) writeNode(element, child, editor);
}

std::string toString(const pugi::xml_document &document) {
  std::ostringstream stream;
  document.save(stream, "  ", pugi::format_default, pugi::encoding_utf8);
  return stream.str();
}

bool readNode(pugi::xml_node element, DesignNode &node,
              std::unordered_set<std::string> &ids, size_t &count,
              int depth, std::string &error) {
  if (++count > 4096 || depth > 64) {
    error = "组件数量或嵌套深度超出限制";
    return false;
  }
  if (!uniqueAttributes(element, error)) return false;
  node.type = element.name();
  node.id = element.attribute("id").as_string();
  if (node.id.empty() || !ids.insert(node.id).second) {
    error = "每个组件必须有唯一的 id";
    return false;
  }
  const std::string_view locked = element.attribute("designLocked").value();
  if (element.attribute("designLocked") && locked != "true" && locked != "false") {
    error = "designLocked 必须为 true 或 false";
    return false;
  }
  node.locked = locked == "true";
  if (element.attribute("xmlns") && std::string_view(element.attribute("xmlns").value()) != "urn:hiyui:ui") {
    error = "项目组件命名空间无效";
    return false;
  }
  for (auto field : element.attributes()) {
    const std::string name = field.name();
    if (name != "id" && name != "designLocked" && name != "xmlns")
      node.attributes.emplace(name, field.value());
  }
  for (auto child : element.children()) {
    if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
      error = "组件文本请写入 text 属性";
      return false;
    }
    if (child.type() != pugi::node_element) continue;
    if (std::string_view(child.name()) == "Item") {
      if (++count > 4096 || node.items.size() >= 256) { error = "项目条目过多，每个组件最多 256 项"; return false; }
      if (!uniqueAttributes(child, error)) return false;
      for (const auto nested : child.children()) {
        if (nested.type() != pugi::node_comment && nested.type() != pugi::node_pi) {
          error = "Item 只支持属性，不支持嵌套内容";
          return false;
        }
      }
      Attributes item;
      for (auto field : child.attributes()) item.emplace(field.name(), field.value());
      node.items.push_back(std::move(item));
    } else {
      DesignNode value;
      if (!readNode(child, value, ids, count, depth + 1, error)) return false;
      node.children.push_back(std::move(value));
    }
  }
  return true;
}
}

std::string layoutXml(const DocumentState &document) {
  pugi::xml_document xml;
  writeNode(xml, document.root, false);
  xml.document_element().append_attribute("xmlns") = "urn:hiyui:ui";
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
        name != "width" && name != "height" && name != "nextId") {
      error = "不支持的项目属性：" + std::string(name);
      return false;
    }
  }
  DocumentState value;
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
  auto root = project.first_child();
  while (root && root.type() != pugi::node_element) root = root.next_sibling();
  int roots = 0;
  for (const auto child : project.children()) {
    if (child.type() == pugi::node_element) ++roots;
    else if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
      error = "项目节点不支持文本内容";
      return false;
    }
  }
  if (!root || roots != 1) { error = "项目必须包含一个根组件"; return false; }
  std::unordered_set<std::string> ids;
  size_t count = 0;
  if (!readNode(root, value.root, ids, count, 1, error)) return false;
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
  const auto registry = hiyui::XmlRegistry::instance().snapshot();
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
    for (const auto &module : registry.modules) {
      if (module.namespaceUri != hiyui::XmlBuiltinNamespaceUri) continue;
      for (const auto &widget : module.widgets) {
        if (widget.localName != node.type) continue;
        for (const auto &field : widget.attributes)
          if (field.kind == hiyui::XmlAttributeKind::Source && node.attributes.contains(field.name))
            relocate(node.attributes[field.name]);
        for (const auto &field : widget.itemAttributes)
          if (field.kind == hiyui::XmlAttributeKind::Source)
            for (auto &item : node.items) if (item.contains(field.name)) relocate(item[field.name]);
      }
    }
    for (auto &child : node.children) self(self, child);
  };
  visit(visit, document.root);
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
