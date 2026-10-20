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
  VBox viewport({.bg = HIYUI_TRANSPARENT, .radius = 0, .scroll = ScrollMode::Both});
  viewport.init(stage).fill().ref(viewport_).scrollElastic(false).scrollMomentum(false);
  Box extent({.bg = HIYUI_TRANSPARENT});
  extent.init(viewport).ref(extent_).clickable(false).scrollMode(ScrollMode::Off);
  Box device({.bg = 0x333338, .radius = 8});
  device.init(viewport).floating().pos(14, 14).ref(device_).scrollMode(ScrollMode::Off).clipCorner();
  CustomDraw chrome;
  chrome.init(device).pos(0, 0).ref(chrome_).clickable(false)
      .painter(std::make_unique<FramePainter>(*this));
  device(TitleBar(titleBar).ref(titleBar_));
  VBox content;
  content.init(device).ref(content_).surface(SurfaceRole::Background)
      .scrollMode(ScrollMode::Off).scrollable(false);
  preview_ = std::make_unique<XmlReloadHost>(content);
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
  float scale = 1;
  host_.with([&](Box &host) { scale = scaleOf(host); });
  const auto px = [scale](int value) { return int(std::lround(value * scale)); };
  const int ruler = px(20), margin = px(14);
  const int width = std::max(1, host_->widthPx() - ruler);
  const int height = std::max(1, host_->heightPx() - ruler);
  const auto offset = viewport_->scrollMetrics();
  stage_->posPx(ruler, ruler).sizePx(width, height);
  overlay_->sizePx(width, height);
  const auto &state = document_.state;
  const bool customTitle = state.device >= 2 && state.borderless;
  const int border = px(state.device == 0 ? 8 : 1);
  const int top = state.device == 0 ? 0 : border;
  const int chromeHeight = px(customTitle ? TitleBarProps{}.height : devices[state.device].chromeHeight);
  const int clientWidth = px(state.width), clientHeight = px(state.height);
  const int frameWidth = clientWidth + border * 2;
  const int frameHeight = top + chromeHeight + clientHeight + border;
  if (fit_) {
    const float fit = std::min({float(std::max(1, width - margin * 2)) / frameWidth,
                               float(std::max(1, height - margin * 2)) / frameHeight, 1.0f});
    zoom_ = std::max(26.0f, std::floor(fit * 256.0f)) / 256.0f;
  }
  device_->sizePx(frameWidth, frameHeight)
      .radius(customTitle ? 0 : state.device == 0 ? 40 : state.device == 2 ? 8 : 6).zoom(zoom_);
  device_->bg(customTitle ? HIYUI_TRANSPARENT : state.device == 0 ? 0x161619 : state.device == 1 ? 0x2d2d30 : 0xb8b8b8);
  zoom_ = device_->zoom();
  chrome_->posPx(border, top).sizePx(clientWidth, chromeHeight).hidden(customTitle);
  titleBar_->hidden(!customTitle).posPx(border, top).sizePx(clientWidth, chromeHeight);
  refreshTitle();
  content_->posPx(border, top + chromeHeight).sizePx(clientWidth, clientHeight);
  const int extentWidth = std::max(width, int(std::ceil(frameWidth * zoom_)) + margin * 2);
  const int extentHeight = std::max(height, int(std::ceil(frameHeight * zoom_)) + margin * 2);
  extent_->sizePx(extentWidth, extentHeight);
  host_->updateLayout();
  viewport_->scrollToXPx(fit_ ? 0 : std::clamp(offset.x, 0, extentWidth - width), false)
      .scrollToYPx(fit_ ? 0 : std::clamp(offset.y, 0, extentHeight - height), false);
  arranging_ = false;
  scroll();
}

void DesignSurface::scroll() {
  if (arranging_ || !viewport_ || !device_) return;
  const auto offset = viewport_->scrollMetrics();
  float scale = 1;
  viewport_.with([&](VBox &view) { scale = scaleOf(view); });
  const int origin = int(std::lround(14 * scale));
  device_->posPx(origin - offset.x, origin - offset.y);
  refresh();
}

bool DesignSurface::reload(std::string &error) {
  if (!preview_) return false;
  const auto xml = layoutXml(document_.state);
  error = catalog_.validate(xml);
  if (!error.empty()) return false;
  auto candidate = std::make_unique<XmlReloadHost>(content_, previewContext(document_.state));
  if (!candidate->reloadFromString(xml)) {
    for (const auto &diagnostic : candidate->diagnostics()) {
      if (!error.empty()) error += "\n";
      error += diagnostic.message;
    }
    if (error.empty()) error = "无法创建预览组件";
    return false;
  }
  activation_.close();
  preview_ = std::move(candidate);
  auto root = widget(document_.state.root.id);
  const auto redirect = [overlay = overlay_](auto &&self, Widget &node) -> void {
    node.on(EventCode::Focused, [overlay] { if (overlay) overlay->focus(); });
    node.on(EventCode::Scroll, [overlay] { if (overlay) overlay->redraw(); });
    for (int i = 0; i < node.childCount(); ++i)
      node.childAt(i).with([&](Widget &child) { self(self, child); });
  };
  root.with([&](Widget &node) { redirect(redirect, node); });
  arrange();
  return true;
}

Ref<Widget> DesignSurface::widget(const std::string &id) const {
  const auto *node = document_.find(id);
  if (!preview_ || !node || !catalog_.visual(document_, *node)) return {};
  if (node->kind != NodeKind::Item) return preview_->get<Widget>(id.c_str());
  const auto *parent = document_.parent(id);
  if (!parent) return {};
  size_t index = 0;
  for (const auto &child : parent->children) {
    if (child.id == id) return preview_->layout().itemHost(parent->id.c_str(), index);
    if (child.kind == NodeKind::Item) ++index;
  }
  return {};
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

std::optional<Point> DesignSurface::positionIn(const std::string &id, const std::string &parentId) const {
  const auto node = widget(id);
  const auto parent = widget(parentId);
  if (!node || !parent || !node->isVisible() || !parent->isVisible()) return std::nullopt;
  return inParent(parentId, node->toScreenPx({0, 0}));
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

Point DesignSurface::inScreen(const std::string &id, Point point) const {
  Point result;
  widget(id).with([&](Widget &node) {
    const auto content = node.contentBoundsPx();
    const auto scroll = node.scrollMetrics();
    const auto scale = scaleOf(node);
    result = node.toScreenPx({content.x - scroll.x + int(std::lround(point.x * scale)),
                              content.y - scroll.y + int(std::lround(point.y * scale))});
  });
  return result;
}

Point DesignSurface::snapOffset(const std::string &id, Rect bounds,
                                bool left, bool right, bool top, bool bottom) const {
  Point offset;
  const auto *owner = document_.find(id);
  if (!guidesVisible_ || HotkeyManager::ctrlPressed() || !owner || catalog_.automaticLayout(*owner)) return offset;
  float scale = 1;
  content_.with([&](VBox &content) { scale = scaleOf(content); });
  Point parentOrigin;
  float parentScale = 1;
  widget(id).with([&](Widget &parent) {
    const auto bounds = parent.boundsPx();
    const auto content = parent.contentBoundsPx();
    const auto scroll = parent.scrollMetrics();
    parentOrigin = {bounds.x + content.x - scroll.x, bounds.y + content.y - scroll.y};
    parentScale = scaleOf(parent);
  });
  const auto client = content_->boundsPx();
  const int tolerance = std::max(1, int(std::lround(6 * scale)));
  int nearestX = tolerance + 1, nearestY = tolerance + 1;
  const auto start = inScreen(id, {bounds.x, bounds.y});
  const auto end = inScreen(id, {bounds.x + bounds.w, bounds.y + bounds.h});
  Rect viewport;
  viewport_.with([&](VBox &view) { viewport = transformed(view); });
  for (const auto &guide : document_.state.guides) {
    if (guide.position < 0 || guide.position > (guide.vertical ? document_.state.width : document_.state.height)) continue;
    const int position = int(std::lround(guide.position * scale));
    const auto point = content_->toScreenPx(guide.vertical ? Point{position, 0} : Point{0, position});
    const int aligned = int(std::lround((position + (guide.vertical
        ? client.x - parentOrigin.x : client.y - parentOrigin.y)) / parentScale));
    if (guide.vertical) {
      if (point.x < viewport.x || point.x >= viewport.x + viewport.w) continue;
      if (left && std::abs(start.x - point.x) < nearestX) {
        nearestX = std::abs(start.x - point.x);
        offset.x = aligned - bounds.x;
      }
      if (right && std::abs(end.x - point.x) < nearestX) {
        nearestX = std::abs(end.x - point.x);
        offset.x = aligned - bounds.x - bounds.w;
      }
    } else {
      if (point.y < viewport.y || point.y >= viewport.y + viewport.h) continue;
      if (top && std::abs(start.y - point.y) < nearestY) {
        nearestY = std::abs(start.y - point.y);
        offset.y = aligned - bounds.y;
      }
      if (bottom && std::abs(end.y - point.y) < nearestY) {
        nearestY = std::abs(end.y - point.y);
        offset.y = aligned - bounds.y - bounds.h;
      }
    }
  }
  return offset;
}

Point DesignSurface::snapPoint(const std::string &id, Point point) const {
  const auto offset = snapOffset(id, {point.x, point.y, 0, 0}, true, false, true, false);
  return {point.x + offset.x, point.y + offset.y};
}

Rect DesignSurface::paletteBounds(const std::string &id, Point screen) const {
  const auto point = inParent(id, screen);
  const auto *entry = catalog_.find(tool_);
  Rect bounds{point.x, point.y, entry->width, entry->height};
  const auto offset = snapOffset(id, bounds, true, true, true, true);
  bounds.x += offset.x;
  bounds.y += offset.y;
  return bounds;
}

bool DesignSurface::inside(Point screen) const {
  bool result = false;
  if (!content_) return false;
  viewport_.with([&](VBox &view) { result = contains(transformed(view), screen); });
  content_.with([&](VBox &view) { result = result && contains(transformed(view), screen); });
  return result;
}

std::string DesignSurface::hitNode(const DesignNode &node, Point screen, bool containerOnly) const {
  auto ref = widget(node.id);
  if (!ref || !ref->isVisible() || !contains(screenBounds(node.id), screen)) return {};
  for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
    auto result = hitNode(*it, screen, containerOnly);
    if (!result.empty()) return result;
  }
  if (!containerOnly) return node.id;
  if (node.locked || !catalog_.container(document_, node)) return {};
  DesignNode child;
  child.type = tool_;
  return tool_.empty() || catalog_.accepts(document_, node, child) ? node.id : std::string{};
}

std::string DesignSurface::hit(Point screen, bool containerOnly) const {
  return inside(screen) ? hitNode(document_.state.root, screen, containerOnly) : std::string{};
}

bool DesignSurface::select(std::string id) {
  if (beforeEdit && !beforeEdit()) return false;
  const auto before = document_.state;
  const auto *node = document_.find(id);
  for (auto *current = node; current; current = document_.parent(current->id)) {
    if (current->kind != NodeKind::Item) continue;
    auto *parent = document_.parent(current->id);
    const auto *entry = parent ? catalog_.find(parent->type) : nullptr;
    if (!entry || entry->metadata.design.selectionAttribute.empty()) continue;
    size_t index = 0;
    for (const auto &child : parent->children) {
      if (child.id == current->id) break;
      if (child.kind == NodeKind::Item) ++index;
    }
    parent->attributes[entry->metadata.design.selectionAttribute] = std::to_string(index);
  }
  if (before != document_.state) {
    std::string error;
    if (!reload(error)) { document_.state = before; if (onStatus) onStatus(error); return false; }
    document_.checkpoint();
  }
  document_.selected = std::move(id);
  if (before != document_.state && onChange) onChange();
  refresh();
  if (onSelection) onSelection();
  return true;
}

bool DesignSurface::previewSelection(std::string &error) {
  const auto *node = document_.find(document_.selected);
  const auto *entry = node && node->kind == NodeKind::Component ? catalog_.find(node->type) : nullptr;
  if (!preview_ || !entry || !entry->metadata.activatable) {
    error = "所选节点没有独立预览操作";
    return false;
  }
  Ref<Widget> anchor;
  for (const auto *parent = document_.parent(node->id); parent && !anchor; parent = document_.parent(parent->id))
    anchor = widget(parent->id);
  if (!anchor) { error = "请先在画布上创建该对象的宿主"; return false; }
  activation_.close();
  anchor.with([&](Widget &value) { activation_ = preview_->layout().activate(node->id.c_str(), value); });
  for (const auto &diagnostic : activation_.diagnostics()) {
    if (!error.empty()) error += "\n";
    error += diagnostic.message;
  }
  return static_cast<bool>(activation_);
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
  content_.with([&](VBox &content) { scale = scaleOf(content); });
  int position = int(std::lround((vertical ? point.x : point.y) / scale));
  if (HotkeyManager::ctrlPressed() || !inside(screen)) return position;

  const auto intersect = [](Rect a, Rect b) {
    const int x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return Rect{x, y, std::max(0, std::min(a.x + a.w, b.x + b.w) - x),
                      std::max(0, std::min(a.y + a.h, b.y + b.h) - y)};
  };
  Rect viewport, client;
  viewport_.with([&](VBox &view) { viewport = transformed(view); });
  content_.with([&](VBox &content) { client = transformed(content); });
  const auto origin = content_->boundsPx();
  const int limit = vertical ? document_.state.width : document_.state.height;
  const int pointer = vertical ? screen.x : screen.y;
  int nearest = std::max(1, int(std::lround(6 * scale))) + 1;
  const auto visit = [&](auto &&self, const DesignNode &node, Rect clip) -> void {
    const auto ref = widget(node.id);
    if (!ref || !ref->isVisible()) return;
    const auto bounds = screenBounds(node.id);
    clip = intersect(clip, bounds);
    if (clip.w <= 0 || clip.h <= 0) return;
    const auto layout = ref->boundsPx();
    const int start = vertical ? bounds.x : bounds.y;
    const int length = vertical ? bounds.w : bounds.h;
    const int visibleStart = vertical ? clip.x : clip.y;
    const int visibleEnd = visibleStart + (vertical ? clip.w : clip.h);
    const int layoutStart = vertical ? layout.x - origin.x : layout.y - origin.y;
    const int layoutLength = vertical ? layout.w : layout.h;
    for (int part = 0; part < 3; ++part) {
      const int target = int(std::lround(start + length * part / 2.0));
      if (target < visibleStart || target > visibleEnd) continue;
      const int aligned = int(std::lround((layoutStart + layoutLength * part / 2.0) / scale));
      if (aligned < 0 || aligned > limit) continue;
      const int distance = std::abs(pointer - target);
      if (distance < nearest) {
        nearest = distance;
        position = aligned;
      }
    }
    for (const auto &child : node.children) self(self, child, clip);
  };
  visit(visit, document_.state.root, intersect(viewport, client));
  return position;
}

int DesignSurface::guideAt(Point screen) const {
  if (!guidesVisible_ || !inside(screen)) return -1;
  float scale = 1;
  content_.with([&](VBox &content) { scale = scaleOf(content); });
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
  if (beforeEdit && !beforeEdit()) return;
  overlay_->focus();
  pointer_ = screen;
  moved_ = false;
  handle_ = -1;
  const auto selected = widget(document_.selected);
  const auto *selectedNode = document_.find(document_.selected);
  if (tool_.empty() && selected && selected->isVisible() && selectedNode && !responsiveSize(*selectedNode)) {
    const auto rect = screenBounds(document_.selected);
    const auto points = handles(rect);
    int radius = 5;
    overlay_.with([&](CustomDraw &view) { radius = std::max(5, int(std::lround(scaleOf(view) * 5))); });
    for (int i = 0; i < 8; ++i)
      if (contains({points[i].x - radius, points[i].y - radius, radius * 2, radius * 2}, screen)) handle_ = i;
  }
  if (tool_.empty() && handle_ < 0) {
    const int guide = guideAt(screen);
    if (guide >= 0) { beginGuide(document_.state.guides[guide].vertical, guide, screen); return; }
  }
  if (!tool_.empty()) {
    owner_ = hit(screen, true);
    if (owner_.empty()) return;
    gesture_ = Gesture::Create;
    anchor_ = inParent(owner_, screen);
    const auto start = snapPoint(owner_, anchor_);
    draftRect_ = {start.x, start.y, 0, 0};
  } else {
    if (handle_ < 0 && !select(hit(screen))) return;
    auto *node = document_.find(document_.selected);
    auto *parent = document_.parent(document_.selected);
    if (!node || !parent || node->locked || parent->locked || node->kind != NodeKind::Component ||
        !widget(node->id)) return;
    const auto *parentEntry = catalog_.find(parent->type);
    if (parentEntry && parentEntry->metadata.design.layout == XmlDesignLayout::Managed) return;
    if (handle_ >= 0 && responsiveSize(*node)) return;
    owner_ = parent->id;
    startRect_ = geometry(node->id);
    anchor_ = inParent(owner_, screen);
    gesture_ = handle_ >= 0 ? Gesture::Resize : catalog_.automaticLayout(*parent) ? Gesture::Reorder : Gesture::Move;
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
                           std::to_string(guide.position) + " · Ctrl 忽略吸附 · 拖回标尺删除");
    refresh();
    return;
  }
  const auto point = inParent(owner_, screen);
  const int dx = point.x - anchor_.x, dy = point.y - anchor_.y;
  if (!moved_ && std::abs(dx) + std::abs(dy) < 2) return;
  moved_ = true;
  if (gesture_ == Gesture::Create) {
    const auto start = snapPoint(owner_, anchor_);
    const auto end = snapPoint(owner_, point);
    draftRect_ = {std::min(start.x, end.x), std::min(start.y, end.y),
                  std::abs(end.x - start.x), std::abs(end.y - start.y)};
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
    rect.x += dx;
    rect.y += dy;
    const auto offset = snapOffset(owner_, rect, true, true, true, true);
    node->attributes["x"] = std::to_string(rect.x + offset.x);
    node->attributes["y"] = std::to_string(rect.y + offset.y);
    node->attributes.erase("center");
  } else {
    const bool left = handle_ == 0 || handle_ == 6 || handle_ == 7;
    const bool right = handle_ == 2 || handle_ == 3 || handle_ == 4;
    const bool top = handle_ == 0 || handle_ == 1 || handle_ == 2;
    const bool bottom = handle_ == 4 || handle_ == 5 || handle_ == 6;
    if (left) { rect.x += dx; rect.w -= dx; }
    if (right) rect.w += dx;
    if (top) { rect.y += dy; rect.h -= dy; }
    if (bottom) rect.h += dy;
    const auto offset = snapOffset(owner_, rect, left, right, top, bottom);
    if (left) { rect.x += offset.x; rect.w -= offset.x; }
    if (right) rect.w += offset.x;
    if (top) { rect.y += offset.y; rect.h -= offset.y; }
    if (bottom) rect.h += offset.y;
    rect.w = std::clamp(rect.w, 8, 3840);
    rect.h = std::clamp(rect.h, 8, 2160);
    if (left) rect.x = startRect_.x + startRect_.w - rect.w;
    if (top) rect.y = startRect_.y + startRect_.h - rect.h;
    catalog_.setGeometry(*node, rect, !catalog_.automaticLayout(*owner));
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
  DesignNode node;
  std::string error;
  if (!catalog_.create(tool_, node, error)) { if (onStatus) onStatus(error); return; }
  node.id.clear();
  if (!catalog_.accepts(document_, *owner, node)) {
    if (onStatus) onStatus("该节点不接受此组件，或已达到数量限制");
    return;
  }
  catalog_.setGeometry(node, bounds, !catalog_.automaticLayout(*owner));
  if (catalog_.automaticLayout(*owner)) { node.attributes.erase("x"); node.attributes.erase("y"); }
  document_.assignIds(node);
  document_.selected = node.id;
  owner->children.push_back(std::move(node));
  ensurePreviewCollections(document_.state, catalog_);
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
    if (!dropTarget_.empty()) draftRect_ = paletteBounds(dropTarget_, screen);
    refresh();
  } else if (gesture_ == Gesture::Palette && event.code() == EventCode::Released) {
    gesture_ = Gesture::None;
    if (moved_) {
      owner_ = overToolbox ? std::string{} : hit(screen, true);
      if (!owner_.empty()) {
        before_ = document_.state;
        create(paletteBounds(owner_, screen), owner_);
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
  if (!node || !parent || node->locked || parent->locked || node->kind != NodeKind::Component ||
        !widget(node->id)) return;
  const auto *parentEntry = catalog_.find(parent->type);
  if (parentEntry && parentEntry->metadata.design.layout == XmlDesignLayout::Managed) return;
  before_ = document_.state;
  owner_ = parent->id;
  if (catalog_.automaticLayout(*parent)) {
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
    const float scale = ctx.dpiScale * zoom_;
    const int step = scale < 0.5f ? 100 : 50;
    draw.pushClip({ruler, 0, ctx.width - ruler, ruler});
    for (int x = 0; x <= document_.state.width; x += step) {
      const int sx = host_->toLocalPx(content_->toScreenPx({ctx.px(x), 0})).x;
      draw.drawLine({sx, ruler - ctx.px(6)}, {sx, ruler}, colors[C::RulerTick], 1);
      draw.drawText(std::to_string(x).c_str(), {sx + ctx.px(3), 0, ctx.px(40), ruler - ctx.px(3)}, colors[C::RulerText], ctx.px(9), false);
    }
    draw.popClip();
    draw.pushClip({0, ruler, ruler, ctx.height - ruler});
    for (int y = 0; y <= document_.state.height; y += step) {
      const int sy = host_->toLocalPx(content_->toScreenPx({0, ctx.px(y)})).y;
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
    content_.with([&](VBox &view) { content = localRect(transformed(view)); });
    draw.pushClip(content);
    for (size_t i = 0; i < document_.state.guides.size(); ++i) {
      const auto &guide = document_.state.guides[i];
      const auto point = overlay_->toLocalPx(content_->toScreenPx(guide.vertical
          ? Point{ctx.px(guide.position), 0} : Point{0, ctx.px(guide.position)}));
      const uint32_t color = gesture_ == Gesture::Guide && static_cast<int>(i) == guideIndex_ ? 0xffa726 : colors[C::Guide];
      if (guide.vertical) draw.drawLine({point.x, content.y}, {point.x, content.y + content.h}, color, ctx.px(1));
      else draw.drawLine({content.x, point.y}, {content.x + content.w, point.y}, color, ctx.px(1));
    }
    draw.popClip();
  }
  if (!dropTarget_.empty()) draw.strokeRect(localRect(screenBounds(dropTarget_)), 0x0078d4, ctx.px(2));
  if (gesture_ == Gesture::Create || (gesture_ == Gesture::Palette && !dropTarget_.empty())) {
    const auto &owner = gesture_ == Gesture::Create ? owner_ : dropTarget_;
    if (gesture_ == Gesture::Create || !catalog_.automaticLayout(*document_.find(owner))) {
      const auto start = inScreen(owner, {draftRect_.x, draftRect_.y});
      const auto end = inScreen(owner, {draftRect_.x + draftRect_.w, draftRect_.y + draftRect_.h});
      draw.strokeRect(localRect({start.x, start.y, end.x - start.x, end.y - start.y}), 0x00b4d8, ctx.px(1));
    }
    if (gesture_ == Gesture::Create) return;
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
  const auto *parent = document_.parent(node->id);
  const auto *parentEntry = parent ? catalog_.find(parent->type) : nullptr;
  if (parent && !parent->locked && !node->locked && !responsiveSize(*node) && node->kind == NodeKind::Component &&
      (!parentEntry || parentEntry->metadata.design.layout != XmlDesignLayout::Managed)) {
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
