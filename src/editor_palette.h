#pragma once

#include <hiyui/widget/widget.h>
#include <array>
#include <functional>
#include <vector>

namespace hiydesign {

enum class EditorColor {
  Background, Panel, Raised, Border, Text, Muted, Accent, Hover, Selected,
  ToolSelected, Inset, Success, Code, Output, Icon, Stage, Dot, RulerTick,
  RulerText, Guide, PositionLine, PositionText, White, Count
};

struct EditorPalette {
  std::array<uint32_t, static_cast<size_t>(EditorColor::Count)> colors{};
  uint32_t operator[](EditorColor role) const { return colors[static_cast<size_t>(role)]; }
  static EditorPalette theme(bool dark);
};

class EditorAppearance {
public:
  using Binding = std::function<void(hiyui::Widget &, const EditorPalette &)>;
  ~EditorAppearance();
  const EditorPalette &palette() const { return current_; }
  uint32_t color(EditorColor role) const { return current_[role]; }
  bool dark() const { return dark_; }
  void change(bool dark, bool animate);
  void stop();
  void bind(hiyui::Widget &widget, Binding apply);
  void background(hiyui::Widget &widget, EditorColor role);
  void foreground(hiyui::Widget &widget, EditorColor role);
  void input(hiyui::Widget &widget, EditorColor text = EditorColor::Text);
  void button(hiyui::Widget &widget);
  std::function<void()> onChange;

private:
  struct Entry { hiyui::Ref<hiyui::Widget> widget; Binding apply; };
  std::vector<Entry> bindings_;
  EditorPalette current_ = EditorPalette::theme(true);
  EditorPalette from_, to_;
  bool dark_ = true;
  static void animate(void *target, int32_t value);
  void apply();
};

}
