#pragma once

#include "component_catalog.h"
#include <hiyui/widget/box.h>
#include <hiyui/widget/custom_draw.h>
#include <hiyui/widget/vbox.h>
#include <hiyui/xml/xml_reload.h>
#include <functional>
#include <memory>

namespace hiydesign {

class DesignSurface {
public:
  DesignSurface(DesignDocument &document, const ComponentCatalog &catalog);
  ~DesignSurface();
  void init(hiyui::Widget &parent);
  bool reload(std::string &error);
  bool select(std::string id);
  void tool(std::string type);
  const std::string &tool() const { return tool_; }
  void paletteDrag(const std::string &type, hiyui::Event &event, bool overToolbox);
  void cancel();
  void fit();
  void zoom(float value);
  float zoom() const { return zoom_; }
  void refresh();
  hiyui::Rect geometry(const std::string &id) const;
  void nudge(int dx, int dy);
  std::function<bool()> beforeEdit;
  std::function<void()> onSelection;
  std::function<void()> onChange;
  std::function<void()> onToolChanged;
  std::function<void(const std::string &)> onStatus;
  std::function<void()> onDelete;
  std::function<void()> onUndo;
  std::function<void()> onRedo;

private:
  class CanvasPainter;
  class FramePainter;
  enum class Gesture { None, Create, Move, Resize, Reorder, Palette };
  DesignDocument &document_;
  const ComponentCatalog &catalog_;
  hiyui::Ref<hiyui::Box> host_, stage_, extent_, device_, content_;
  hiyui::Ref<hiyui::VBox> viewport_;
  hiyui::Ref<hiyui::CustomDraw> backdrop_, overlay_, chrome_;
  std::unique_ptr<hiyui::XmlReloadHost> preview_;
  std::string tool_, owner_, dropTarget_;
  Gesture gesture_ = Gesture::None;
  std::optional<DocumentState> before_;
  hiyui::Point anchor_, pointer_, paletteStart_;
  hiyui::Rect startRect_, draftRect_;
  int handle_ = -1;
  float zoom_ = 1;
  bool fit_ = true;
  bool arranging_ = false;
  bool moved_ = false;
  void arrange();
  void scroll();
  void pointerEvent(hiyui::Event &event);
  void begin(hiyui::Point screen);
  void drag(hiyui::Point screen);
  void finish(hiyui::Point screen);
  void create(hiyui::Rect bounds, const std::string &owner);
  bool commitGesture();
  hiyui::Ref<hiyui::Widget> widget(const std::string &id) const;
  hiyui::Point inParent(const std::string &id, hiyui::Point screen) const;
  hiyui::Rect screenBounds(const std::string &id) const;
  std::string hit(hiyui::Point screen, bool containerOnly = false) const;
  std::string hitNode(const DesignNode &node, hiyui::Point screen, bool containerOnly) const;
  bool inside(hiyui::Point screen) const;
  void paint(hiyui::DrawCtx &draw, const hiyui::PaintCtx &ctx, bool overlay);
  void paintFrame(hiyui::DrawCtx &draw, const hiyui::PaintCtx &ctx);
};

}
