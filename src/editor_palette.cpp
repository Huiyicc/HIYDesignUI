#include "editor_palette.h"
#include <hiyui/anim/anim.h>
#include <hiyui/style/theme.h>
#include <algorithm>

namespace hiydesign {
using namespace hiyui;
using C = EditorColor;

EditorPalette EditorPalette::theme(bool dark) {
  if (dark) return {{0x1e1e1e, 0x252526, 0x2d2d30, 0x3f3f46, 0xcccccc, 0x9d9d9d,
      0x0078d4, 0x3e3e42, 0x04395e, 0x094771, 0x1b1b1e, 0x73c991, 0x9cdcfe,
      0x9fb3c8, 0x7fb3e0, 0x1a1a1c, 0x303034, 0x626268, 0x929299, 0x00b4d8,
      0x6296b5, 0x8bc9df, 0xffffff}};
  return {{0xf4f5f7, 0xffffff, 0xeceef1, 0xd3d7dd, 0x24292f, 0x626c78,
      0x0078d4, 0xe1e8f0, 0xd6e9fa, 0xcce4f8, 0xe7e9ed, 0x287345, 0x005a9e,
      0x40566d, 0x226a9e, 0xe6e8ec, 0xc6cbd3, 0x9ca4af, 0x626c78, 0x007fa8,
      0x487997, 0x22617f, 0xffffff}};
}

EditorAppearance::~EditorAppearance() { stop(); }
void EditorAppearance::stop() { Anim::stop(this, animate); }

void EditorAppearance::bind(Widget &widget, Binding apply) {
  bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
      [](const Entry &entry) { return !entry.widget; }), bindings_.end());
  apply(widget, current_);
  bindings_.push_back({widget.ref(), std::move(apply)});
}

void EditorAppearance::background(Widget &widget, C role) {
  bind(widget, [role](Widget &widget, const EditorPalette &palette) { widget.bg(palette[role]); });
}

void EditorAppearance::foreground(Widget &widget, C role) {
  bind(widget, [role](Widget &widget, const EditorPalette &palette) { widget.textColor(palette[role]); });
}

void EditorAppearance::input(Widget &widget, C text) {
  bind(widget, [text](Widget &widget, const EditorPalette &p) {
    widget.style(StyleSet{
        .normal = {.color = p[text], .bgFill = {.color = p[C::Background]}, .border = {1, p[C::Border]}},
        .hovered = {.color = p[text]}, .pressed = {.color = p[text]},
        .focused = {.color = p[text], .bgFill = {.color = p[C::Background]}, .border = {1, p[C::Accent]}},
        .disabled = {.color = p[C::Muted], .bgFill = {.color = p[C::Background]}}});
  });
}

void EditorAppearance::button(Widget &widget) {
  bind(widget, [](Widget &widget, const EditorPalette &p) {
    widget.style(StyleSet{
        .normal = {.color = p[C::Text], .bgFill = {.color = p[C::Raised]}},
        .hovered = {.color = p[C::Text], .bgFill = {.color = p[C::Hover]}},
        .pressed = {.color = p[C::Text], .bgFill = {.color = p[C::Selected]}},
        .focused = {.color = p[C::Text]},
        .disabled = {.color = p[C::Muted], .bgFill = {.color = p[C::Raised]}}});
  });
}

void EditorAppearance::apply() {
  bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
      [](const Entry &entry) { return !entry.widget; }), bindings_.end());
  for (const auto &entry : bindings_)
    entry.widget.with([&](Widget &widget) { entry.apply(widget, current_); });
  if (onChange) onChange();
}

void EditorAppearance::animate(void *target, int32_t value) {
  auto &self = *static_cast<EditorAppearance *>(target);
  for (size_t i = 0; i < self.current_.colors.size(); ++i) {
    uint32_t color = 0;
    for (int shift : {0, 8, 16}) {
      const int from = (self.from_.colors[i] >> shift) & 255;
      const int to = (self.to_.colors[i] >> shift) & 255;
      color |= uint32_t((from * (255 - value) + to * value + 127) / 255) << shift;
    }
    self.current_.colors[i] = color;
  }
  self.apply();
}

void EditorAppearance::change(bool dark, bool animateColors) {
  stop();
  dark_ = dark;
  from_ = current_;
  to_ = EditorPalette::theme(dark);
  const auto duration = animateColors ? currentTheme().resolveAnimDuration(200) : 0;
  if (!duration) { current_ = to_; apply(); return; }
  Anim(this, animate).values(0, 255).duration(duration).start();
}

}
