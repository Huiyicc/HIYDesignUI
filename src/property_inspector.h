#pragma once

#include "component_catalog.h"
#include "editor_palette.h"
#include <hiyui/widget/input.h>
#include <hiyui/widget/property_grid.h>
#include <functional>

namespace hiydesign {

class PropertyInspector {
public:
  PropertyInspector(DesignDocument &document, const ComponentCatalog &catalog, EditorAppearance &appearance)
      : document_(document), catalog_(catalog), appearance_(appearance) {}
  void init(hiyui::Widget &parent);
  void refresh(bool preserveEditor = false);
  bool commit();
  void resetSelected();
  std::function<bool(DocumentState, std::string, std::string &)> apply;

private:
  DesignDocument &document_;
  const ComponentCatalog &catalog_;
  EditorAppearance &appearance_;
  hiyui::Ref<hiyui::PropertyGrid> grid_;
  hiyui::Ref<hiyui::Input> textInput_;
  std::map<hiyui::PropertyId, std::string> fields_;
  std::string nodeId_;
  void submit(hiyui::PropertyCommitRequest &request);
};

}
