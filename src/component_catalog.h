#pragma once

#include "design_document.h"
#include <hiyui/xml/xml_schema.h>

namespace hiydesign {

struct ComponentEntry {
  std::string type;
  std::string namespaceUri;
  std::string localName;
  std::string prefix;
  std::string category;
  int width = 120;
  int height = 36;
  hiyui::XmlWidgetMetadata metadata;
  uint32_t icon() const;
};

class ComponentCatalog {
public:
  ComponentCatalog();
  const ComponentEntry *find(const std::string &type) const;
  const std::vector<ComponentEntry> &entries() const { return entries_; }
  const hiyui::XmlSchema &schema() const { return schema_; }
  bool container(const std::string &type) const;
  bool container(const DesignDocument &document, const DesignNode &node) const;
  bool automaticLayout(const DesignNode &node) const;
  bool visual(const DesignDocument &document, const DesignNode &node) const;
  bool accepts(const DesignDocument &document, const DesignNode &parent, const DesignNode &child) const;
  const ComponentEntry *owner(const DesignDocument &document, const DesignNode &node) const;
  bool create(const std::string &type, DesignNode &node, std::string &error) const;
  void setGeometry(DesignNode &node, hiyui::Rect bounds, bool position) const;
  bool createItem(const ComponentEntry &entry, DesignNode &node, std::string &error) const;
  bool assignItemIds(const DesignDocument &document, const DesignNode &parent,
                     DesignNode &node, std::string &error) const;
  std::vector<hiyui::XmlAttributeSpec> attributes(const std::string &type) const;
  std::vector<hiyui::XmlAttributeSpec> attributes(const DesignDocument &document, const DesignNode &node) const;
  std::string validate(const std::string &xml) const;

private:
  hiyui::XmlSchema schema_;
  std::vector<ComponentEntry> entries_;
};

std::string defaultText(const hiyui::XmlAttributeSpec &spec);

}
