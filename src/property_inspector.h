#pragma once

#include "component_catalog.h"
#include "editor_palette.h"
#include <hiyui/widget/color_picker.h>
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
  std::function<void()> refreshRequested;

private:
  DesignDocument &document_;
  const ComponentCatalog &catalog_;
  EditorAppearance &appearance_;
  hiyui::Ref<hiyui::PropertyGrid> grid_;
  hiyui::Ref<hiyui::Input> textInput_;
  hiyui::Ref<hiyui::ColorPicker> colorInput_;
  std::map<hiyui::PropertyId, std::string> fields_;
  std::map<hiyui::PropertyId, hiyui::XmlAttributeSpec> specs_;
  std::string nodeId_;
  int ruleIndex_ = -1;
  void buildTextEditor(hiyui::Widget &host, hiyui::PropertyEditor editor);
  hiyui::PropertyItem attributeItem(hiyui::PropertyId id, const hiyui::XmlAttributeSpec &spec,
      std::string value, const char *omittedLabel);
  void appendLayoutProperties(std::vector<hiyui::PropertyItem> &groups, const DesignNode &node);
  void submitLayout(hiyui::PropertyCommitRequest &request, const std::string &field);
  void submit(hiyui::PropertyCommitRequest &request);
};

}
