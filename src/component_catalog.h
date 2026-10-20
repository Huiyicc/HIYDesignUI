#pragma once

#include "design_document.h"
#include <hiyui/xml/xml_schema.h>

namespace hiydesign {

struct ComponentEntry {
  std::string type;
  std::string category;
  int width = 120;
  int height = 36;
  hiyui::XmlWidgetMetadata metadata;
};

class ComponentCatalog {
public:
  ComponentCatalog();
  const ComponentEntry *find(const std::string &type) const;
  const std::vector<ComponentEntry> &entries() const { return entries_; }
  const hiyui::XmlSchema &schema() const { return schema_; }
  bool container(const std::string &type) const;
  DesignNode create(const std::string &type) const;
  std::vector<hiyui::XmlAttributeSpec> attributes(const std::string &type) const;
  std::string validate(const std::string &xml) const;

private:
  hiyui::XmlSchema schema_;
  std::vector<ComponentEntry> entries_;
};

}
