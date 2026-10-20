#pragma once

#include "design_surface.h"
#include "property_inspector.h"
#include <hiyui/hiyui.h>
#include <array>
#include <deque>
#include <filesystem>

namespace hiydesign {

class DesignerApp final : public hiyui::Application {
public:
  ~DesignerApp() override;

private:
  void setup() override;
  void buildMenus(hiyui::Widget &parent);
  void buildToolbar(hiyui::Widget &parent);
  void buildPalette(hiyui::Widget &parent);
  void buildWorkspace(hiyui::Widget &parent);
  void buildInspector(hiyui::Widget &parent);
  void responsive();
  void refresh(bool properties = true);
  void selectionChanged(bool preserveEditor = false);
  void refreshTree();
  void queueInspector();
  bool commitEdits(bool cancelGesture = true);
  bool apply(DocumentState value, std::string selected, std::string &error, bool properties = true);
  void changeDevice(int index);
  void changeResolution();
  void undo(bool redo = false);
  void erase();
  void copy();
  void paste();
  void order(int direction);
  void newProject(bool sample);
  void openProject();
  void save(bool saveAs = false, std::function<void()> after = {});
  void exportLayout();
  void preview();
  void confirmDiscard(std::function<void()> action);
  void completeConfirmation(bool saveFirst);
  void cancelConfirmation();
  void showXml(bool value);
  void togglePanel(bool properties);
  void status(const std::string &text);
  void log(const std::string &text);
  std::filesystem::path resourceBase() const;

  DesignDocument document_;
  ComponentCatalog catalog_;
  std::unique_ptr<DesignSurface> surface_;
  std::unique_ptr<PropertyInspector> inspector_;
  hiyui::Window *window_ = nullptr;
  hiyui::Window *previewWindow_ = nullptr;
  hiyui::Ref<hiyui::Box> root_, canvasHost_, propertyHost_, treeHost_, confirmShade_;
  hiyui::Ref<hiyui::VBox> shell_, left_, center_, right_, bottom_;
  hiyui::Ref<hiyui::HBox> body_;
  hiyui::Ref<hiyui::Tree> tree_;
  hiyui::Ref<hiyui::Input> xml_, output_, width_, height_, projectTitle_;
  hiyui::Ref<hiyui::Label> status_, position_, selectionLabel_, documentTab_, zoomLabel_;
  hiyui::Ref<hiyui::Button> undoButton_, redoButton_, designTab_, xmlTab_, propsTab_, treeTab_;
  std::array<hiyui::Ref<hiyui::Button>, 5> deviceButtons_;
  std::vector<std::pair<std::string, hiyui::Ref<hiyui::Button>>> paletteButtons_;
  std::map<hiyui::TreeNodeId, std::string> treeIds_;
  std::deque<std::string> treeLabels_;
  std::filesystem::path projectPath_;
  std::string outputText_;
  std::function<void()> confirmedAction_;
  bool fileDialog_ = false;
  bool closeApproved_ = false;
  bool showXml_ = false;
  bool leftVisible_ = true;
  bool rightVisible_ = true;
  bool compact_ = false;
  bool inspectorQueued_ = false;
  bool arranging_ = false;
};

}
