#pragma once

#include "design_surface.h"
#include "property_inspector.h"
#include "editor_palette.h"
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
  hiyui::Button actionButton(hiyui::Widget &parent, const char *text, uint32_t icon,
                            std::function<void()> action = {});
  hiyui::Label caption(hiyui::Widget &parent, const char *text,
                       EditorColor role = EditorColor::Text, int size = 12);
  hiyui::HBox strip(hiyui::Widget &parent, int height, EditorColor role = EditorColor::Panel);
  uint32_t color(EditorColor role) const { return appearance_.color(role); }
  void refreshColors();
  void refreshThemeButton();
  void toggleTheme();
  void responsive();
  void refresh(bool properties = true);
  void selectionChanged(bool preserveEditor = false);
  void refreshTree();
  void queueInspector();
  bool commitEdits(bool cancelGesture = true);
  bool apply(DocumentState value, std::string selected, std::string &error, bool properties = true);
  void changeDevice(int index);
  void changeResolution();
  void changeFrame();
  void undo(bool redo = false);
  void erase();
  void copy();
  void paste();
  void insertComponent(const std::string &type);
  void insertItem();
  void previewObject();
  void editPreviewData();
  void order(int direction);
  void newProject(bool sample);
  void openProject();
  void save(bool saveAs = false, std::function<void()> after = {});
  void exportLayout(bool withWindow = false);
  void preview();
  hiyui::TitleBarProps previewTitleBar() const;
  void confirmDiscard(std::function<void()> action);
  void completeConfirmation(bool saveFirst);
  void cancelConfirmation();
  void showXml(bool value);
  void togglePanel(bool properties);
  void status(const std::string &text);
  void log(const std::string &text);
  std::filesystem::path resourceBase() const;

  DesignDocument document_;
  EditorAppearance appearance_;
  hiyui::ScopedEventHandle rootDelete_;
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
  hiyui::Ref<hiyui::Button> frameButton_;
  hiyui::Ref<hiyui::Button> themeButton_;
  hiyui::Ref<hiyui::Button> objectPreview_;
  std::string paletteType_;
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
  bool themeAnimation_ = true;
};

}
