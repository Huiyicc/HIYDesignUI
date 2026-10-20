#include "design_surface.h"
#include "project_io.h"
#include <hiyui/misc/hotkey.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace hiydesign {
using namespace hiyui;
namespace {
bool contains(Rect r, Point p) {
  return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;
}

Rect transformed(Widget &widget) {
  const auto a = widget.toScreenPx({0, 0});
  const auto b = widget.toScreenPx({widget.widthPx(), widget.heightPx()});
  return {a.x, a.y, b.x - a.x, b.y - a.y};
}

float scaleOf(Widget &widget) {
  return layoutEnvironment(widget).scale;
}

std::array<Point, 8> handles(Rect r) {
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  return {{{r.x, r.y}, {cx, r.y}, {r.x + r.w, r.y},
           {r.x + r.w, cy}, {r.x + r.w, r.y + r.h},
           {cx, r.y + r.h}, {r.x, r.y + r.h}, {r.x, cy}}};
}

bool screenPointer(Event &event, Point &point) {
  if (!event.pointerPositionPx(point)) return false;
  point = event.currentTargetWidget().toScreenPx(point);
  return true;
}
}

class DesignSurface::CanvasPainter final : public Painter {
  DesignSurface &surface_;
  bool overlay_;
public:
  CanvasPainter(DesignSurface &surface, bool overlay) : surface_(surface), overlay_(overlay) {}
  void paint(DrawCtx &draw, const PaintCtx &ctx) override { surface_.paint(draw, ctx, overlay_); }
};

class DesignSurface::FramePainter final : public Painter {
  DesignSurface &surface_;
public:
  explicit FramePainter(DesignSurface &surface) : surface_(surface) {}
  void paint(DrawCtx &draw, const PaintCtx &ctx) override { surface_.paintFrame(draw, ctx); }
};

DesignSurface::DesignSurface(DesignDocument &document, const ComponentCatalog &catalog, EditorAppearance &appearance)
    : document_(document), catalog_(catalog), appearance_(appearance) {}

DesignSurface::~DesignSurface() {
  preview_.reset();
  host_.destroy();
}

void DesignSurface::init(Widget &parent, const TitleBarProps &titleBar) {
  Box host({.bg = 0x1e1e1e});
  host.init(parent).fill().ref(host_).scrollMode(ScrollMode::Off);
  appearance_.background(host, EditorColor::Background);
  CustomDraw backdrop;
  backdrop.init(host).fill().clickable().pressLock().ref(backdrop_)
      .painter(std::make_unique<CanvasPainter>(*this, false));
  for (const auto code : {EventCode::Pressed, EventCode::Pressing, EventCode::Released, EventCode::PressLost})
    backdrop.on(code, [this](Event &event) { rulerEvent(event); });
  Box stage({.bg = 0x1a1a1c});
  stage.init(host).pos(20, 20).ref(stage_).scrollMode(ScrollMode::Off);
  appearance_.background(stage, EditorColor::Stage);
  Box device({.bg = 0x333338, .radius = 8});
  device.init(stage).pos(14, 14).ref(device_).scrollMode(ScrollMode::Off).clipCorner();
  CustomDraw chrome;
  chrome.init(device).pos(0, 0).ref(chrome_).clickable(false)
      .painter(std::make_unique<FramePainter>(*this));
  device(TitleBar(titleBar).ref(titleBar_));
  Box content({.bg = 0xf3f4f6});
  content.init(device).ref(content_).scrollMode(ScrollMode::Off);
  preview_ = std::make_unique<XmlReloadHost>(content);
  VBox viewport({.bg = HIYUI_TRANSPARENT, .radius = 0, .scroll = ScrollMode::Both});
  viewport.init(stage).fill().ref(viewport_).scrollElastic(false).scrollMomentum(false);
  Box extent({.bg = HIYUI_TRANSPARENT});
  extent.init(viewport).ref(extent_).clickable(false).scrollMode(ScrollMode::Off);
  CustomDraw overlay;
  overlay.init(viewport).ref(overlay_).floating().clickable().pressLock().addToGroup()
      .scrollOnFocus(false).scrollChain().painter(std::make_unique<CanvasPainter>(*this, true));
  for (const auto code : {EventCode::Pressed, EventCode::Pressing, EventCode::Released, EventCode::PressLost})
    overlay.on(code, [this](Event &event) { pointerEvent(event); });
  overlay.on(keyPreviewEventCode(), [this](Event &event) {
    switch (event.keyCode()) {
    case KeyCode::Escape: cancel(); break;
    case KeyCode::Left: nudge(-1, 0); break;
    case KeyCode::Right: nudge(1, 0); break;
    case KeyCode::Up: nudge(0, -1); break;
    case KeyCode::Down: nudge(0, 1); break;
    case KeyCode::Delete: if (onDelete) onDelete(); break;
    default: return;
    }
    event.stopProcessing();
  });
  overlay.on(HotkeyManager::eventCode(HotkeyId::Undo), [this] { if (onUndo) onUndo(); });
  overlay.on(HotkeyManager::eventCode(HotkeyId::Redo), [this] { if (onRedo) onRedo(); });
  host.on(EventCode::SizeChanged, [this] { arrange(); });
  host.onDpiChange([this] { arrange(); });
  viewport.on(EventCode::Scroll, [this] { scroll(); });
  host.on(EventCode::LayoutCompleted, [this] { refresh(); });
  arrange();
}

void DesignSurface::arrange() {
  if (arranging_ || !host_ || !overlay_) return;
  arranging_ = true;
  const int width = std::max(1, host_->width() - 20);
  const int height = std::max(1, host_->height() - 20);
  stage_->size(width, height);
  overlay_->size(width, height);
  const auto &state = document_.state;
  const bool customTitle = state.device >= 2 && state.borderless;
  const int border = state.device == 0 ? 8 : customTitle ? 0 : 1;
  const int chromeHeight = devices[state.device].chromeHeight;
  const int frameWidth = state.width + border * 2;
  const int frameHeight = state.height + chromeHeight + border;
  if (fit_)
    zoom_ = std::clamp(std::min(float(std::max(1, width - 28)) / frameWidth,
                               float(std::max(1, height - 28)) / frameHeight), 0.1f, 1.0f);
  device_->size(frameWidth, frameHeight).radius(state.device == 0 ? 40 : state.device == 3 ? 6 : 8).zoom(zoom_);
  zoom_ = device_->zoom();
  chrome_->size(frameWidth, chromeHeight).hidden(customTitle);
  titleBar_->hidden(!customTitle).width(frameWidth);
  refreshTitle();
  content_->pos(border, chromeHeight).size(state.width, state.height);
  extent_->size(std::max(width, int(std::ceil(frameWidth * zoom_)) + 28),
                std::max(height, int(std::ceil(frameHeight * zoom_)) + 28));
  host_->updateLayout();
  arranging_ = false;
  scroll();
}

void DesignSurface::scroll() {
  if (!viewport_ || !device_) return;
  const auto offset = viewport_->scrollMetrics();
  float scale = 1;
  viewport_.with([&](VBox &view) { scale = scaleOf(view); });
  device_->posPx(int(std::lround(14 * scale)) - offset.x, int(std::lround(14 * scale)) - offset.y);
  refresh();
}

bool DesignSurface::reload(std::string &error) {
  if (!preview_) return false;
  const auto xml = layoutXml(document_.state);
  error = catalog_.validate(xml);
  if (!error.empty()) return false;
  if (!preview_->reloadFromString(xml)) {
    for (const auto &diagnostic : preview_->diagnostics()) {
      if (!error.empty()) error += "\n";
      error += diagnostic.message;
    }
    if (error.empty()) error = "无法创建预览组件";
    return false;
  }
  auto root = widget(document_.state.root.id);
  const auto redirect = [overlay = overlay_](auto &&self, Widget &node) -> void {
    node.on(EventCode::Focused, [overlay] { if (overlay) overlay->focus(); });
    for (int i = 0; i < node.childCount(); ++i)
      node.childAt(i).with([&](Widget &child) { self(self, child); });
  };
  root.with([&](Widget &node) { redirect(redirect, node); });
  arrange();
  return true;
}

Ref<Widget> DesignSurface::widget(const std::string &id) const {
  return preview_ ? preview_->get<Widget>(id.c_str()) : Ref<Widget>{};
}

Rect DesignSurface::geometry(const std::string &id) const {
  Rect result;
  widget(id).with([&](Widget &node) {
    const auto p = node.positionPx();
    const auto scale = scaleOf(node);
    result = {int(std::lround(p.x / scale)), int(std::lround(p.y / scale)),
              int(std::lround(node.widthPx() / scale)), int(std::lround(node.heightPx() / scale))};
  });
  return result;
}

Rect DesignSurface::screenBounds(const std::string &id) const {
  Rect result;
  widget(id).with([&](Widget &node) { result = transformed(node); });
  return result;
}

Point DesignSurface::inParent(const std::string &id, Point screen) const {
  Point result;
  widget(id).with([&](Widget &node) {
    const auto p = node.toLocalPx(screen);
    const auto content = node.contentBoundsPx();
    const auto scroll = node.scrollMetrics();
    const auto scale = scaleOf(node);
    result = {int(std::lround((p.x - content.x + scroll.x) / scale)),
              int(std::lround((p.y - content.y + scroll.y) / scale))};
  });
  return result;
}

bool DesignSurface::inside(Point screen) const {
  bool result = false;
  if (!content_) return false;
  viewport_.with([&](VBox &view) { result = contains(transformed(view), screen); });
  content_.with([&](Box &view) { result = result && contains(transformed(view), screen); });
  return result;
}

std::string DesignSurface::hitNode(const DesignNode &node, Point screen, bool containerOnly) const {
  auto ref = widget(node.id);
  if (!ref || !ref->isVisible() || !contains(screenBounds(node.id), screen)) return {};
  for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
    auto result = hitNode(*it, screen, containerOnly);
    if (!result.empty()) return result;
  }
  return !containerOnly || (catalog_.container(node.type) && !node.locked) ? node.id : std::string{};
}

std::string DesignSurface::hit(Point screen, bool containerOnly) const {
  return inside(screen) ? hitNode(document_.state.root, screen, containerOnly) : std::string{};
}

bool DesignSurface::select(std::string id) {
  if (beforeEdit && !beforeEdit()) return false;
  document_.selected = std::move(id);
  refresh();
  if (onSelection) onSelection();
  return true;
}

void DesignSurface::tool(std::string type) {
  cancel();
  tool_ = std::move(type);
  if (onToolChanged) onToolChanged();
  if (overlay_) overlay_->focus();
  if (onStatus) onStatus(tool_.empty() ? "选择工具" : "在目标容器内拖出区域以创建 " + tool_);
}

void DesignSurface::fit() { fit_ = true; arrange(); }
void DesignSurface::zoom(float value) {
  fit_ = false;
  zoom_ = std::clamp(value, 0.1f, 4.0f);
  arrange();
}
void DesignSurface::refresh() {
  if (backdrop_) backdrop_->redraw();
  if (overlay_) overlay_->redraw();
  if (chrome_) chrome_->redraw();
}

void DesignSurface::refreshTitle() {
  if (titleBar_) titleBar_->title(document_.state.title.c_str());
  if (chrome_) chrome_->redraw();
}

void DesignSurface::pointerEvent(Event &event) {
  Point screen;
  if (event.code() == EventCode::PressLost) { cancel(); return; }
  if (!screenPointer(event, screen)) return;
  switch (event.code()) {
  case EventCode::Pressed: begin(screen); break;
  case EventCode::Pressing: drag(screen); break;
  case EventCode::Released: finish(screen); break;
  default: break;
  }
}

void DesignSurface::toggleGuides() {
  cancel();
  guidesVisible_ = !guidesVisible_;
  refresh();
  if (onStatus) onStatus(guidesVisible_ ? "辅助线已显示" : "辅助线已隐藏");
}

void DesignSurface::clearGuides() {
  if (beforeEdit && !beforeEdit()) return;
  cancel();
  if (document_.state.guides.empty()) return;
  document_.state.guides.clear();
  document_.checkpoint();
  refresh();
  if (onChange) onChange();
}

int DesignSurface::guidePosition(bool vertical, Point screen) const {
  const auto point = content_->toLocalPx(screen);
  float scale = 1;
  content_.with([&](Box &content) { scale = scaleOf(content); });
  return int(std::lround((vertical ? point.x : point.y) / scale));
}

int DesignSurface::guideAt(Point screen) const {
  if (!guidesVisible_ || !inside(screen)) return -1;
  float scale = 1;
  content_.with([&](Box &content) { scale = scaleOf(content); });
  const int tolerance = std::max(4, int(std::lround(4 * scale)));
  int found = -1, nearest = tolerance + 1;
  for (size_t i = 0; i < document_.state.guides.size(); ++i) {
    const auto &guide = document_.state.guides[i];
    if (guide.position > (guide.vertical ? document_.state.width : document_.state.height)) continue;
    const auto point = content_->toScreenPx(guide.vertical
        ? Point{int(std::lround(guide.position * scale)), 0}
        : Point{0, int(std::lround(guide.position * scale))});
    const int distance = std::abs(guide.vertical ? screen.x - point.x : screen.y - point.y);
    if (distance < nearest) { nearest = distance; found = static_cast<int>(i); }
  }
  return found;
}

void DesignSurface::beginGuide(bool vertical, int index, Point screen) {
  if (gesture_ != Gesture::None || (beforeEdit && !beforeEdit())) return;
  if (index < 0 && document_.state.guides.size() >= 256) {
    if (onStatus) onStatus("最多支持 256 条辅助线");
    return;
  }
  overlay_->focus();
  before_ = document_.state;
  guideIndex_ = index;
  if (guideIndex_ < 0) {
    guideIndex_ = static_cast<int>(document_.state.guides.size());
    document_.state.guides.push_back({vertical, guidePosition(vertical, screen)});
  }
  guidesVisible_ = true;
  gesture_ = Gesture::Guide;
  viewport_->scrollMode(ScrollMode::Off);
  pointer_ = screen;
  refresh();
}

void DesignSurface::rulerEvent(Event &event) {
  if (gesture_ == Gesture::Guide) { pointerEvent(event); return; }
  if (event.code() != EventCode::Pressed) return;
  Point screen;
  if (!screenPointer(event, screen)) return;
  const auto point = host_->toLocalPx(screen);
  float scale = 1;
  host_.with([&](Box &host) { scale = scaleOf(host); });
  const int ruler = int(std::lround(20 * scale));
  if (point.x < ruler && point.y >= ruler) beginGuide(true, -1, screen);
  else if (point.y < ruler && point.x >= ruler) beginGuide(false, -1, screen);
}

void DesignSurface::finishGuide(Point screen) {
  drag(screen);
  const auto guide = document_.state.guides[guideIndex_];
  if (!inside(screen) || guide.position < 0 ||
      guide.position > (guide.vertical ? document_.state.width : document_.state.height))
    document_.state.guides.erase(document_.state.guides.begin() + guideIndex_);
  const bool changed = before_ && *before_ != document_.state;
  before_.reset();
  gesture_ = Gesture::None;
  guideIndex_ = -1;
  viewport_->scrollMode(ScrollMode::Both);
  if (changed) document_.checkpoint();
  refresh();
  if (changed && onChange) onChange();
}

void DesignSurface::begin(Point screen) {
  if (gesture_ != Gesture::None) return;
  if (tool_.empty()) {
    const int guide = guideAt(screen);
    if (guide >= 0) { beginGuide(document_.state.guides[guide].vertical, guide, screen); return; }
  }
  if (beforeEdit && !beforeEdit()) return;
  overlay_->focus();
  pointer_ = screen;
  moved_ = false;
  handle_ = -1;
  const auto selected = widget(document_.selected);
  if (tool_.empty() && selected && selected->isVisible()) {
    const auto rect = screenBounds(document_.selected);
    const auto points = handles(rect);
    int radius = 5;
    overlay_.with([&](CustomDraw &view) { radius = std::max(5, int(std::lround(scaleOf(view) * 5))); });
    for (int i = 0; i < 8; ++i)
      if (contains({points[i].x - radius, points[i].y - radius, radius * 2, radius * 2}, screen)) handle_ = i;
  }
  if (!tool_.empty()) {
    owner_ = hit(screen, true);
    if (owner_.empty()) return;
    gesture_ = Gesture::Create;
    anchor_ = inParent(owner_, screen);
    draftRect_ = {anchor_.x, anchor_.y, 0, 0};
  } else {
    if (handle_ < 0 && !select(hit(screen))) return;
    auto *node = document_.find(document_.selected);
    auto *parent = document_.parent(document_.selected);
    if (!node || !parent || node->locked || parent->locked) return;
    owner_ = parent->id;
    startRect_ = geometry(node->id);
    anchor_ = inParent(owner_, screen);
    gesture_ = handle_ >= 0 ? Gesture::Resize : automaticLayout(*parent) ? Gesture::Reorder : Gesture::Move;
  }
  before_ = document_.state;
  viewport_->scrollMode(ScrollMode::Off);
  refresh();
}

void DesignSurface::drag(Point screen) {
  pointer_ = screen;
  if (gesture_ == Gesture::None || gesture_ == Gesture::Palette) return;
  if (gesture_ == Gesture::Guide) {
    auto &guide = document_.state.guides[guideIndex_];
    guide.position = guidePosition(guide.vertical, screen);
    if (onStatus) onStatus(std::string(guide.vertical ? "辅助线 X " : "辅助线 Y ") +
                           std::to_string(guide.position) + " · 拖回标尺删除");
    refresh();
    return;
  }
  const auto point = inParent(owner_, screen);
  const int dx = point.x - anchor_.x, dy = point.y - anchor_.y;
  if (!moved_ && std::abs(dx) + std::abs(dy) < 2) return;
  moved_ = true;
  if (gesture_ == Gesture::Create) {
    draftRect_ = {std::min(anchor_.x, point.x), std::min(anchor_.y, point.y), std::abs(dx), std::abs(dy)};
    refresh();
    return;
  }
  if (gesture_ == Gesture::Reorder) {
    dropTarget_ = hit(screen);
    while (!dropTarget_.empty()) {
      const auto *parent = document_.parent(dropTarget_);
      if (!parent || parent->id == owner_) break;
      dropTarget_ = parent->id;
    }
    refresh();
    return;
  }
  auto *node = document_.find(document_.selected);
  auto *owner = document_.find(owner_);
  if (!node || !owner) return;
  const auto previous = document_.state;
  auto rect = startRect_;
  if (gesture_ == Gesture::Move) {
    node->attributes["x"] = std::to_string(rect.x + dx);
    node->attributes["y"] = std::to_string(rect.y + dy);
    node->attributes.erase("center");
  } else {
    if (handle_ == 0 || handle_ == 6 || handle_ == 7) { rect.x += dx; rect.w -= dx; }
    if (handle_ == 2 || handle_ == 3 || handle_ == 4) rect.w += dx;
    if (handle_ == 0 || handle_ == 1 || handle_ == 2) { rect.y += dy; rect.h -= dy; }
    if (handle_ == 4 || handle_ == 5 || handle_ == 6) rect.h += dy;
    rect.w = std::clamp(rect.w, 8, 3840);
    rect.h = std::clamp(rect.h, 8, 2160);
    if (handle_ == 0 || handle_ == 6 || handle_ == 7) rect.x = startRect_.x + startRect_.w - rect.w;
    if (handle_ == 0 || handle_ == 1 || handle_ == 2) rect.y = startRect_.y + startRect_.h - rect.h;
    setGeometry(*node, rect, !automaticLayout(*owner));
  }
  if (previous == document_.state) return;
  std::string error;
  if (!reload(error)) {
    document_.state = previous;
    if (onStatus) onStatus(error);
  }
}

void DesignSurface::finish(Point screen) {
  if (gesture_ == Gesture::None || gesture_ == Gesture::Palette) return;
  if (gesture_ == Gesture::Guide) { finishGuide(screen); return; }
  drag(screen);
  if (gesture_ == Gesture::Create && moved_ && inside(screen)) {
    create(draftRect_, owner_);
    tool_.clear();
    if (onToolChanged) onToolChanged();
  } else if (gesture_ == Gesture::Reorder && moved_) {
    auto *owner = document_.find(owner_);
    const auto target = std::find_if(owner->children.begin(), owner->children.end(),
                                    [&](const auto &child) { return child.id == dropTarget_; });
    const auto source = std::find_if(owner->children.begin(), owner->children.end(),
                                    [&](const auto &child) { return child.id == document_.selected; });
    if (target != owner->children.end() && source != owner->children.end() && source != target) {
      const auto index = std::distance(owner->children.begin(), target);
      DesignNode node = std::move(*source);
      owner->children.erase(source);
      owner->children.insert(owner->children.begin() + index, std::move(node));
    }
  }
  commitGesture();
}

void DesignSurface::create(Rect bounds, const std::string &parentId) {
  auto *owner = document_.find(parentId);
  if (!owner || owner->locked || !catalog_.find(tool_)) return;
  auto node = catalog_.create(tool_);
  setGeometry(node, bounds, !automaticLayout(*owner));
  if (automaticLayout(*owner)) { node.attributes.erase("x"); node.attributes.erase("y"); }
  document_.assignIds(node);
  document_.selected = node.id;
  owner->children.push_back(std::move(node));
}

bool DesignSurface::commitGesture() {
  std::string error;
  const bool changed = before_ && *before_ != document_.state;
  bool accepted = true;
  if (changed && !reload(error)) {
    document_.state = *before_;
    if (!document_.find(document_.selected)) document_.selected = owner_;
    std::string restoreError;
    reload(restoreError);
    accepted = false;
    if (onStatus) onStatus(error);
  } else if (changed) {
    document_.checkpoint();
  }
  before_.reset();
  gesture_ = Gesture::None;
  dropTarget_.clear();
  viewport_->scrollMode(ScrollMode::Both);
  refresh();
  if (changed && accepted && onChange) onChange();
  else if (onSelection) onSelection();
  return accepted;
}

void DesignSurface::cancel() {
  const bool changed = before_ && *before_ != document_.state;
  if (changed) {
    document_.state = *before_;
    std::string error;
    if (gesture_ != Gesture::Guide) reload(error);
  }
  before_.reset();
  gesture_ = Gesture::None;
  guideIndex_ = -1;
  if (!tool_.empty()) {
    tool_.clear();
    if (onToolChanged) onToolChanged();
  }
  dropTarget_.clear();
  if (viewport_) viewport_->scrollMode(ScrollMode::Both);
  refresh();
  if (changed && onSelection) onSelection();
}

void DesignSurface::paletteDrag(const std::string &type, Event &event, bool overToolbox) {
  Point screen;
  if (event.code() == EventCode::PressLost) { cancel(); return; }
  if (!screenPointer(event, screen)) return;
  if (event.code() == EventCode::Pressed) {
    if (beforeEdit && !beforeEdit()) return;
    cancel();
    tool_ = type;
    if (onToolChanged) onToolChanged();
    paletteStart_ = screen;
    moved_ = false;
    gesture_ = Gesture::Palette;
  } else if (gesture_ == Gesture::Palette && event.code() == EventCode::Pressing) {
    moved_ = moved_ || std::abs(screen.x - paletteStart_.x) + std::abs(screen.y - paletteStart_.y) > 5;
    pointer_ = screen;
    dropTarget_ = overToolbox ? std::string{} : hit(screen, true);
    refresh();
  } else if (gesture_ == Gesture::Palette && event.code() == EventCode::Released) {
    gesture_ = Gesture::None;
    if (moved_) {
      owner_ = overToolbox ? std::string{} : hit(screen, true);
      if (!owner_.empty()) {
        before_ = document_.state;
        const auto p = inParent(owner_, screen);
        const auto *entry = catalog_.find(type);
        create({p.x, p.y, entry->width, entry->height}, owner_);
        commitGesture();
      }
      tool_.clear();
      if (onToolChanged) onToolChanged();
    } else if (onStatus) {
      onStatus("在目标容器内拖出区域以创建 " + tool_);
    }
    dropTarget_.clear();
    if (overlay_) overlay_->focus();
    refresh();
  }
}

void DesignSurface::nudge(int dx, int dy) {
  if (gesture_ != Gesture::None || (beforeEdit && !beforeEdit())) return;
  auto *node = document_.find(document_.selected);
  auto *parent = document_.parent(document_.selected);
  if (!node || !parent || node->locked || parent->locked) return;
  before_ = document_.state;
  owner_ = parent->id;
  if (automaticLayout(*parent)) {
    const auto it = std::find_if(parent->children.begin(), parent->children.end(),
                               [&](const auto &child) { return child.id == node->id; });
    const auto index = std::distance(parent->children.begin(), it);
    const auto next = index + (dx + dy > 0 ? 1 : -1);
    if (next >= 0 && next < static_cast<ptrdiff_t>(parent->children.size()))
      std::swap(parent->children[index], parent->children[next]);
  } else {
    const auto rect = geometry(node->id);
    node->attributes["x"] = std::to_string(rect.x + dx);
    node->attributes["y"] = std::to_string(rect.y + dy);
    node->attributes.erase("center");
  }
  commitGesture();
}

void DesignSurface::paint(DrawCtx &draw, const PaintCtx &ctx, bool overlay) {
  const auto &colors = appearance_.palette();
  using C = EditorColor;
  const int ruler = ctx.px(20);
  if (!overlay) {
    draw.fillRect({0, 0, ctx.width, ruler}, colors[C::Panel]);
    draw.fillRect({0, 0, ruler, ctx.height}, colors[C::Panel]);
    if (!content_ || !host_) return;
    const auto screen = content_->toScreenPx({0, 0});
    const auto origin = host_->toLocalPx(screen);
    const float scale = ctx.dpiScale * zoom_;
    const int step = scale < 0.5f ? 100 : 50;
    draw.pushClip({ruler, 0, ctx.width - ruler, ruler});
    for (int x = 0; x <= document_.state.width; x += step) {
      const int sx = origin.x + int(std::lround(x * scale));
      draw.drawLine({sx, ruler - ctx.px(6)}, {sx, ruler}, colors[C::RulerTick], 1);
      draw.drawText(std::to_string(x).c_str(), {sx + ctx.px(3), 0, ctx.px(40), ruler - ctx.px(3)}, colors[C::RulerText], ctx.px(9), false);
    }
    draw.popClip();
    draw.pushClip({0, ruler, ruler, ctx.height - ruler});
    for (int y = 0; y <= document_.state.height; y += step) {
      const int sy = origin.y + int(std::lround(y * scale));
      draw.drawLine({ruler - ctx.px(5), sy}, {ruler, sy}, colors[C::RulerTick], 1);
      draw.drawText(std::to_string(y).c_str(), {0, sy + 2, ruler, ctx.px(12)}, colors[C::RulerText], ctx.px(8));
    }
    draw.popClip();
    return;
  }
  if (!overlay_ || !content_) return;
  const auto localRect = [&](Rect rect) {
    auto p = overlay_->toLocalPx({rect.x, rect.y});
    rect.x = p.x; rect.y = p.y;
    return rect;
  };
  Rect frame;
  device_.with([&](Box &device) { frame = localRect(transformed(device)); });
  const int dotSpacing = std::max(1, ctx.px(18));
  for (int y = ctx.px(8); y < ctx.height; y += dotSpacing)
    for (int x = ctx.px(8); x < ctx.width; x += dotSpacing)
      if (!contains(frame, {x, y})) draw.fillRect({x, y, ctx.px(1), ctx.px(1)}, colors[C::Dot]);
  if (guidesVisible_) {
    Rect content;
    content_.with([&](Box &view) { content = localRect(transformed(view)); });
    draw.pushClip(content);
    for (size_t i = 0; i < document_.state.guides.size(); ++i) {
      const auto &guide = document_.state.guides[i];
      const int offset = int(std::lround(guide.position * ctx.dpiScale * zoom_));
      const uint32_t color = gesture_ == Gesture::Guide && static_cast<int>(i) == guideIndex_ ? 0xffa726 : colors[C::Guide];
      if (guide.vertical) draw.drawLine({content.x + offset, content.y}, {content.x + offset, content.y + content.h}, color, ctx.px(1));
      else draw.drawLine({content.x, content.y + offset}, {content.x + content.w, content.y + offset}, color, ctx.px(1));
    }
    draw.popClip();
  }
  if (!dropTarget_.empty()) draw.strokeRect(localRect(screenBounds(dropTarget_)), 0x0078d4, ctx.px(2));
  if (gesture_ == Gesture::Create) {
    widget(owner_).with([&](Widget &owner) {
      const auto c = owner.contentBoundsPx();
      const auto s = owner.scrollMetrics();
      const auto dpi = scaleOf(owner);
      const auto a = owner.toScreenPx({c.x - s.x + int(draftRect_.x * dpi), c.y - s.y + int(draftRect_.y * dpi)});
      draw.strokeRect(localRect({a.x, a.y, int(draftRect_.w * dpi * zoom_), int(draftRect_.h * dpi * zoom_)}), 0x00b4d8, ctx.px(1));
    });
    return;
  }
  if (gesture_ == Gesture::Palette && !dropTarget_.empty()) {
    const auto p = overlay_->toLocalPx(pointer_);
    draw.fillRoundRect({p.x + ctx.px(12), p.y + ctx.px(12), ctx.px(100), ctx.px(28)}, 0x0078d4, ctx.px(4));
    draw.drawText(tool_.c_str(), {p.x + ctx.px(12), p.y + ctx.px(12), ctx.px(100), ctx.px(28)}, 0xffffff, ctx.px(12));
  }
  auto *node = document_.find(document_.selected);
  auto ref = widget(document_.selected);
  if (!node || !ref || !ref->isVisible()) return;
  const auto rect = localRect(screenBounds(node->id));
  if (rect.w <= 0 || rect.h <= 0) return;
  const uint32_t color = colors[node->locked ? C::Muted : C::Guide];
  draw.strokeRect(rect, color, ctx.px(1));
  if (document_.parent(node->id) && !node->locked) {
    const int half = ctx.px(3);
    for (const auto p : handles(rect)) {
      Rect handle{p.x - half, p.y - half, half * 2 + 1, half * 2 + 1};
      draw.fillRect(handle, colors[C::Panel]);
      draw.strokeRect(handle, color, ctx.px(1));
    }
  }
  const auto bounds = geometry(node->id);
  const std::string label = node->id + "   " + std::to_string(bounds.w) + " × " + std::to_string(bounds.h);
  const int labelWidth = std::min(ctx.width, draw.measureText(label.c_str(), ctx.px(11)).width + ctx.px(12));
  Rect badge{std::clamp(rect.x, 0, std::max(0, ctx.width - labelWidth)),
             std::max(0, rect.y - ctx.px(25)), labelWidth, ctx.px(20)};
  draw.fillRoundRect(badge, 0x0078d4, ctx.px(3));
  draw.drawText(label.c_str(), badge, 0xffffff, ctx.px(11));
  if (const auto *owner = document_.parent(node->id)) {
    auto parent = localRect(screenBounds(owner->id));
    widget(owner->id).with([&](Widget &value) {
      const auto c = value.contentBoundsPx();
      const auto s = value.scrollMetrics();
      const auto p = overlay_->toLocalPx(value.toScreenPx({c.x - s.x, c.y - s.y}));
      parent.x = p.x; parent.y = p.y;
    });
    draw.drawLine({parent.x, rect.y + rect.h / 2}, {rect.x, rect.y + rect.h / 2}, colors[C::PositionLine], 1);
    draw.drawLine({rect.x + rect.w / 2, parent.y}, {rect.x + rect.w / 2, rect.y}, colors[C::PositionLine], 1);
    const std::string position = "X " + std::to_string(bounds.x) + "   Y " + std::to_string(bounds.y);
    draw.drawText(position.c_str(), {rect.x, rect.y + rect.h + ctx.px(7), ctx.px(160), ctx.px(18)}, colors[C::PositionText], ctx.px(10), false);
  }
}

void DesignSurface::paintFrame(DrawCtx &draw, const PaintCtx &ctx) {
  const int mode = document_.state.device;
  const auto width = ctx.width, height = ctx.height;
  const uint32_t frameBg = mode == 0 ? 0x161619 : mode == 1 ? 0x2d2d30 : mode == 4 ? 0xe6e6e6 : 0xf3f3f3;
  const uint32_t text = mode <= 1 ? 0xcccccc : 0x242424;
  draw.fillRect({0, 0, width, height}, frameBg);
  if (mode == 0) {
    draw.drawText("9:41", {ctx.px(18), 0, ctx.px(50), height}, 0xffffff, ctx.px(11));
    draw.fillRoundRect({width / 2 - ctx.px(38), ctx.px(6), ctx.px(76), ctx.px(18)}, 0x080808, ctx.px(9));
    draw.drawText("LTE  ▰", {width - ctx.px(76), 0, ctx.px(62), height}, 0xffffff, ctx.px(10));
  } else {
    if (mode == 4 || mode == 1) {
      const uint32_t colors[] = {0xff5f57, 0xfebc2e, 0x28c840};
      for (int i = 0; i < 3; ++i) draw.fillCircle({ctx.px(15 + i * 17), height / 2}, ctx.px(5), colors[i]);
    }
    if (mode == 1) {
      const Rect address{ctx.px(86), ctx.px(7), std::max(ctx.px(40), width - ctx.px(130)), height - ctx.px(14)};
      draw.fillRoundRect(address, 0x3c3c40, ctx.px(5));
      draw.drawText("localhost / " "preview", address, 0xaaaaaf, ctx.px(11));
    } else {
      const int left = ctx.px(mode == 4 ? 78 : 12);
      const int buttons = ctx.px(mode == 4 ? 78 : 138);
      draw.drawText(document_.state.title.c_str(), {left, 0, std::max(1, width - left - buttons), height}, text, ctx.px(12), mode == 4);
      if (mode != 4) {
        const int buttonWidth = ctx.px(46), half = ctx.px(5), cy = height / 2;
        const int closeX = width - buttonWidth / 2;
        const int maxX = closeX - buttonWidth;
        const int minX = maxX - buttonWidth;
        if (mode == 3) draw.fillCircle({closeX, cy}, ctx.px(10), 0xe1e1e1);
        draw.drawLine({minX - half, cy + half / 2}, {minX + half, cy + half / 2}, text, ctx.px(1));
        draw.strokeRect({maxX - half, cy - half, half * 2, half * 2}, text, ctx.px(1));
        draw.drawLine({closeX - half, cy - half}, {closeX + half, cy + half}, text, ctx.px(1));
        draw.drawLine({closeX - half, cy + half}, {closeX + half, cy - half}, text, ctx.px(1));
      }
    }
  }
}

}
