#include "designer_app.h"
#include "project_io.h"
#include <hiyui/misc/async.h>
#include <hiyui/misc/hotkey.h>
#include <icon_codepoints.h>
#include <algorithm>
#include <cctype>
#include <utility>

namespace hiydesign {
using namespace hiyui;
namespace {
constexpr uint32_t background = 0x1e1e1e;
constexpr uint32_t panel = 0x252526;
constexpr uint32_t raised = 0x2d2d30;
constexpr uint32_t line = 0x3f3f46;
constexpr uint32_t foreground = 0xcccccc;
constexpr uint32_t muted = 0x9d9d9d;
constexpr uint32_t accent = 0x0078d4;

uint32_t componentIcon(const std::string &type) {
  static const std::map<std::string, uint32_t> icons = {
      {"Button", HIYUI_ICON_SMART_BUTTON}, {"Label", HIYUI_ICON_TITLE},
      {"Input", HIYUI_ICON_INPUT}, {"Checkbox", HIYUI_ICON_CHECK_BOX},
      {"Radio", HIYUI_ICON_RADIO_BUTTON_CHECKED}, {"Switch", HIYUI_ICON_TOGGLE_ON},
      {"Dropdown", HIYUI_ICON_ARROW_DROP_DOWN_CIRCLE}, {"Slider", HIYUI_ICON_TUNE},
      {"Progress", HIYUI_ICON_LINEAR_SCALE}, {"Image", HIYUI_ICON_IMAGE},
      {"Box", HIYUI_ICON_CROP_SQUARE}, {"VBox", HIYUI_ICON_VIEW_AGENDA},
      {"HBox", HIYUI_ICON_VIEW_COLUMN}, {"FlowBox", HIYUI_ICON_VIEW_QUILT},
      {"Card", HIYUI_ICON_WEB_ASSET}};
  return icons.at(type);
}

Button actionButton(Widget &parent, const char *text, uint32_t icon,
                    std::function<void()> action = {}) {
  Button button({.text = text, .iconCodepoint = icon, .iconSize = 16,
                 .variant = ButtonVariant::Secondary, .height = 28,
                 .bg = raised, .textColor = foreground, .fontSize = 12, .radius = 3});
  button.init(parent).padding(8, 0).borderWidth(0)
      .style(StyleSet{.hovered = {.color = foreground, .bgFill = {.color = 0x3e3e42}},
                      .pressed = {.color = foreground, .bgFill = {.color = 0x04395e}},
                      .focused = {.color = foreground},
                      .disabled = {.color = muted, .bgFill = {.color = raised}}});
  if (action) button.onClick(std::move(action));
  return button;
}

Label caption(Widget &parent, const char *text, uint32_t color = foreground, int size = 12) {
  Label label({.text = text, .fontSize = size, .textColor = color});
  label.init(parent);
  return label;
}

HBox strip(Widget &parent, int height, uint32_t color = panel) {
  HBox row({.gap = 4, .bg = color, .radius = 0, .scroll = ScrollMode::Horizontal});
  row.init(parent).fillWidth().height(height).padding(6, 0)
      .scrollbars(ScrollbarMode::Off);
  return row;
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

bool supportedTree(const DesignNode &node, const ComponentCatalog &catalog, std::string &error) {
  if (!catalog.find(node.type)) { error = "当前组件箱不支持 " + node.type; return false; }
  for (const auto &child : node.children) if (!supportedTree(child, catalog, error)) return false;
  return true;
}
}

DesignerApp::~DesignerApp() {
  surface_.reset();
  inspector_.reset();
}

void DesignerApp::setup() {
  setTheme(Theme::fromSeed(accent));
  document_.reset(loginDocument(), false);
  document_.selected = "login";
  window_ = &createWindow({.title = "HIYDesignUI", .width = 1440, .height = 900,
      .borderless = true, .titleBar = {.height = 32, .bg = 0x2d2d30, .textColor = foreground, .fontSize = 12}});
  Box root({.bg = background});
  root.init(*window_).fill().ref(root_).scrollMode(ScrollMode::Off);
  VBox shell({.bg = background, .radius = 0});
  shell.init(root).fill().ref(shell_);
  buildMenus(shell);
  buildToolbar(shell);
  HBox body({.gap = 1, .bg = line, .radius = 0});
  body.init(shell).fillWidth().height(0).flex(1).ref(body_).scrollMode(ScrollMode::Off);
  buildPalette(body);
  buildWorkspace(body);
  buildInspector(body);
  auto statusBar = strip(shell, 24, accent);
  caption(statusBar, "就绪", 0xffffff, 11).ref(status_).width(0).flex(1).textOverflow(TextOverflow::Ellipsis);
  caption(statusBar, "", 0xffffff, 11).ref(position_);
  caption(statusBar, "UTF-8  ·  XML", 0xffffff, 11);
  root.on(EventCode::SizeChanged, [this] { responsive(); });
  root.onDpiChange([this] { responsive(); });
  window_->onCloseRequest([this](WindowCloseEvent &event) {
    if (closeApproved_) return;
    if (commitEdits() && !document_.dirty()) {
      if (previewWindow_) previewWindow_->close();
      return;
    }
    event.cancel();
    if (fileDialog_) { status("请先完成文件选择"); return; }
    confirmDiscard([this] { closeApproved_ = true; quit(); });
  });
  root.on(EventCode::Delete, [this] { window_ = nullptr; });
  window_->root(std::move(root));
  std::string error;
  if (!surface_->reload(error)) log(error);
  refresh();
  responsive();
  log("项目已加载。拖入组件，或选择组件后在画布内拖出创建区域。");
}

void DesignerApp::buildMenus(Widget &parent) {
  MenuBar menu({.height = 28, .bg = panel, .textColor = foreground,
                .hoverColor = 0x3e3e42, .activeColor = 0x04395e, .fontSize = 12});
  menu.init(parent).fillWidth();
  const auto item = [this](const char *text, std::function<void()> action,
                       HotkeySpec key = {}) {
    ContextMenuItem value;
    value.text = text;
    value.action = [this, action = std::move(action)](MenuContext &) {
      if (!fileDialog_ && !confirmShade_) action();
    };
    value.accelerator = key;
    return value;
  };
  menu.items({
      {.text = "文件", .children = {
          item("新建项目", [this] { newProject(false); }, {.sdlKey = 'n', .primary = true}),
          item("打开项目…", [this] { openProject(); }, {.sdlKey = 'o', .primary = true}),
          item("保存", [this] { save(); }, {.sdlKey = 's', .primary = true}),
          item("另存为…", [this] { save(true); }, {.sdlKey = 's', .primary = true, .shift = true}),
          item("导出布局 XML…", [this] { exportLayout(); }),
          item("打开登录示例", [this] { newProject(true); })}},
      {.text = "编辑", .children = {
          item("撤销", [this] { undo(); }), item("重做", [this] { undo(true); }),
          item("复制组件", [this] { copy(); }), item("粘贴组件", [this] { paste(); }),
          item("删除组件", [this] { erase(); }),
          item("前移一层 / 前一个位置", [this] { order(-1); }),
          item("后移一层 / 后一个位置", [this] { order(1); })}},
      {.text = "视图", .children = {
          item("组件箱", [this] { togglePanel(false); }),
          item("属性面板", [this] { togglePanel(true); }),
          item("适合画布", [this] { surface_->fit(); refresh(false); }),
          item("100%", [this] { surface_->zoom(1); refresh(false); }),
          item("布局 XML", [this] { showXml(!showXml_); })}},
      {.text = "运行", .children = {item("交互预览", [this] { preview(); })}},
      {.text = "帮助", .children = {
          item("画布操作", [this] { log("拖入或框选创建；拖动主体移动；边框手柄调整尺寸；方向键微调；Delete 删除；Esc 取消。自动布局中的拖动改变顺序。"); }),
          item("响应式布局", [this] { log("Box 使用绝对定位。VBox / HBox / FlowBox 使用自动布局；fill、flex、maxWidth 控制随视口变化的尺寸。XML 页展示当前真实布局。"); })}}
  });
}

void DesignerApp::buildToolbar(Widget &parent) {
  auto row = strip(parent, 38, raised);
  actionButton(row, nullptr, HIYUI_ICON_ADD, [this] { newProject(false); }).tooltip("新建项目");
  actionButton(row, nullptr, HIYUI_ICON_FOLDER_OPEN, [this] { openProject(); }).tooltip("打开项目");
  actionButton(row, nullptr, HIYUI_ICON_SAVE, [this] { save(); }).tooltip("保存项目 XML");
  caption(row, "│", line);
  actionButton(row, nullptr, HIYUI_ICON_UNDO, [this] { undo(); }).ref(undoButton_).tooltip("撤销");
  actionButton(row, nullptr, HIYUI_ICON_REDO, [this] { undo(true); }).ref(redoButton_).tooltip("重做");
  actionButton(row, nullptr, HIYUI_ICON_CONTENT_COPY, [this] { copy(); }).tooltip("复制组件");
  actionButton(row, nullptr, HIYUI_ICON_CONTENT_PASTE, [this] { paste(); }).tooltip("粘贴组件");
  actionButton(row, nullptr, HIYUI_ICON_DELETE, [this] { erase(); }).tooltip("删除组件");
  caption(row, "│", line);
  actionButton(row, "选择", HIYUI_ICON_NEAR_ME, [this] { surface_->tool({}); });
  actionButton(row, "预览", HIYUI_ICON_PLAY_ARROW, [this] { preview(); }).textColor(0x73c991);
  actionButton(row, "导出 XML", 0, [this] { exportLayout(); });
  caption(row, "│", line);
  actionButton(row, "组件", 0, [this] { togglePanel(false); });
  actionButton(row, "属性", 0, [this] { togglePanel(true); });
}

void DesignerApp::buildPalette(Widget &parent) {
  VBox left({.gap = 0, .bg = panel, .radius = 0});
  left.init(parent).width(248).fillHeight().ref(left_);
  auto title = strip(left, 34);
  caption(title, "组件箱").flex(1);
  actionButton(title, "×", 0, [this] { togglePanel(false); }).width(24);
  Input search({.placeholder = "搜索组件…", .height = 28, .bg = background,
                .textColor = foreground, .placeholderColor = muted, .fontSize = 12, .radius = 3});
  search.init(left).fillWidth();
  search.onChange([this](const std::string &text) {
    for (const auto &[type, button] : paletteButtons_)
      if (button) button->hidden(lower(type).find(lower(text)) == std::string::npos);
  });
  VBox list({.padding = 6, .gap = 2, .bg = panel, .radius = 0, .scroll = ScrollMode::Vertical});
  list.init(left).fillWidth().height(0).flex(1);
  for (const char *category : {"常用组件", "容器与布局"}) {
    caption(list, category, muted, 11).fillWidth().height(24);
    VBox group({.gap = 0, .bg = panel, .radius = 0});
    group.init(list).fillWidth();
    for (const auto &entry : catalog_.entries()) {
      if (entry.category != category) continue;
      auto button = actionButton(group, nullptr, 0);
      Ref<Button> ref;
      button.height(26).fillWidth().bg(panel).ref(ref).pressLock().scrollChain(false);
      HBox content({.gap = 9, .bg = HIYUI_TRANSPARENT, .radius = 0});
      content.init(button).fill().clickable(false);
      Icon icon({.codepoint = componentIcon(entry.type), .size = 16});
      icon.init(content).size(16, 16).color(0x7fb3e0).clickable(false);
      caption(content, entry.type.c_str()).clickable(false);
      button.tooltip(catalog_.container(entry.type) ? "拖入容器；Box 可自由定位，其他容器自动排列" : "拖入画布，或单击后拖出创建区域");
      paletteButtons_.push_back({entry.type, ref});
      for (const auto code : {EventCode::Pressed, EventCode::Pressing, EventCode::Released, EventCode::PressLost})
        button.on(code, [this, type = entry.type](Event &event) {
          if (showXml_) showXml(false);
          Point point;
          bool overToolbox = false;
          if (event.pointerPositionPx(point)) {
            point = left_->toLocalPx(event.currentTargetWidget().toScreenPx(point));
            overToolbox = point.x >= 0 && point.y >= 0 && point.x < left_->widthPx() && point.y < left_->heightPx();
          }
          surface_->paletteDrag(type, event, overToolbox);
        });
    }
  }
  caption(left, "拖入组件 · 单击后框选创建", muted, 11).fillWidth().height(32).textAlign(Align::Center);
}

void DesignerApp::buildWorkspace(Widget &parent) {
  VBox center({.bg = background, .radius = 0});
  center.init(parent).width(0).flex(1).fillHeight().ref(center_);
  auto tabs = strip(center, 32, panel);
  actionButton(tabs, "设计", 0, [this] { showXml(false); }).ref(designTab_).bg(background);
  actionButton(tabs, "XML", 0, [this] { showXml(true); }).ref(xmlTab_);
  caption(tabs, "用户登录", muted, 11).ref(documentTab_).width(0).flex(1).textOverflow(TextOverflow::Ellipsis);
  auto previewBar = strip(center, 38, panel);
  HBox modes({.padding = 2, .gap = 2, .bg = 0x1b1b1e, .radius = 5});
  modes.init(previewBar);
  const uint32_t deviceIcons[] = {HIYUI_ICON_SMARTPHONE, HIYUI_ICON_LANGUAGE,
      HIYUI_ICON_DESKTOP_WINDOWS, HIYUI_ICON_COMPUTER, HIYUI_ICON_LAPTOP_MAC};
  for (int i = 0; i < 5; ++i)
    actionButton(modes, devices[i].name, deviceIcons[i], [this, i] { changeDevice(i); })
        .height(24).fontSize(11).ref(deviceButtons_[i]);
  caption(previewBar, "│", line);
  Input width({.text = "1280", .maxLength = 4, .width = 56, .height = 26,
                .bg = background, .textColor = foreground, .fontSize = 11, .radius = 3});
  width.init(previewBar).ref(width_).on(EventCode::Ready, [this] { changeResolution(); });
  caption(previewBar, "×", muted, 11);
  Input height({.text = "800", .maxLength = 4, .width = 56, .height = 26,
                 .bg = background, .textColor = foreground, .fontSize = 11, .radius = 3});
  height.init(previewBar).ref(height_).on(EventCode::Ready, [this] { changeResolution(); });
  actionButton(previewBar, "应用", 0, [this] { changeResolution(); });
  actionButton(previewBar, "−", 0, [this] { surface_->zoom(surface_->zoom() - 0.1f); refresh(false); });
  caption(previewBar, "100%", muted, 11).ref(zoomLabel_).width(42);
  actionButton(previewBar, "+", 0, [this] { surface_->zoom(surface_->zoom() + 0.1f); refresh(false); });
  actionButton(previewBar, "适合", 0, [this] { surface_->fit(); refresh(false); });
  Box work({.bg = background});
  work.init(center).fillWidth().height(0).flex(1);
  Box canvas({.bg = background});
  canvas.init(work).fill().ref(canvasHost_);
  surface_ = std::make_unique<DesignSurface>(document_, catalog_);
  surface_->init(canvas);
  surface_->onSelection = [this] { selectionChanged(); };
  surface_->onChange = [this] { refresh(); };
  surface_->onToolChanged = [this] {
    for (const auto &[type, button] : paletteButtons_)
      if (button) button->bg(surface_->tool() == type ? 0x094771 : panel);
  };
  surface_->onStatus = [this](const std::string &text) { status(text); };
  surface_->onDelete = [this] { erase(); };
  surface_->onUndo = [this] { undo(); };
  surface_->onRedo = [this] { undo(true); };
  surface_->beforeEdit = [this] { return !inspector_ || commitEdits(false); };
  Input xml({.multiline = true, .horizontalScroll = true, .bg = background,
              .textColor = 0x9cdcfe, .fontSize = 12, .radius = 0, .readOnly = true});
  xml.init(work).fill().hidden().ref(xml_);
  VBox bottom({.bg = panel, .radius = 0});
  bottom.init(center).fillWidth().height(150).ref(bottom_);
  auto outputBar = strip(bottom, 30);
  caption(outputBar, "输出", foreground, 11).flex(1);
  actionButton(outputBar, "清空", 0, [this] { outputText_.clear(); output_->text(""); });
  Input output({.multiline = true, .bg = background, .textColor = 0x9fb3c8,
                 .fontSize = 11, .radius = 0, .readOnly = true});
  output.init(bottom).fillWidth().height(0).flex(1).ref(output_);
}

void DesignerApp::buildInspector(Widget &parent) {
  VBox right({.bg = panel, .radius = 0});
  right.init(parent).width(286).fillHeight().ref(right_);
  auto tabs = strip(right, 34);
  actionButton(tabs, "属性", 0, [this] {
    propertyHost_->hidden(false); treeHost_->hidden(); propsTab_->bg(0x04395e); treeTab_->bg(raised);
  }).ref(propsTab_).bg(0x04395e);
  actionButton(tabs, "组件树", 0, [this] {
    propertyHost_->hidden(); treeHost_->hidden(false); treeTab_->bg(0x04395e); propsTab_->bg(raised);
  }).ref(treeTab_);
  actionButton(tabs, "×", 0, [this] { togglePanel(true); }).width(24);
  Input title({.text = document_.state.title.c_str(), .placeholder = "界面标题", .height = 28,
                .bg = background, .textColor = foreground, .fontSize = 12, .radius = 3});
  title.init(right).fillWidth().ref(projectTitle_);
  title.on(EventCode::Ready, [this] { commitEdits(false); });
  title.on(EventCode::Defocused, [this] { commitEdits(false); });
  caption(right, "", muted, 11).ref(selectionLabel_).fillWidth().height(30).textOverflow(TextOverflow::Ellipsis);
  Box host({.bg = panel});
  host.init(right).fillWidth().height(0).flex(1);
  Box property({.bg = panel});
  property.init(host).fill().ref(propertyHost_);
  inspector_ = std::make_unique<PropertyInspector>(document_, catalog_);
  inspector_->init(property);
  inspector_->apply = [this](DocumentState state, std::string selected, std::string &error) {
    if (fileDialog_ || confirmShade_) {
      error = "请先完成当前对话框操作";
      return false;
    }
    const bool result = apply(std::move(state), std::move(selected), error, false);
    if (result) queueInspector();
    return result;
  };
  Box treeHost({.bg = panel});
  treeHost.init(host).fill().hidden().ref(treeHost_);
  Tree tree({.rowHeight = 28, .indent = 14, .showConnectLine = true,
             .bg = panel, .textColor = foreground, .selectedBg = 0x04395e,
             .hoverBg = raised, .connectLineColor = line, .fontSize = 12});
  tree.init(treeHost).fill().ref(tree_);
  tree.onChange([this](const std::vector<TreeNodeId> &ids) {
    if (!ids.empty() && treeIds_.contains(ids.front())) surface_->select(treeIds_[ids.front()]);
  });
  auto actions = strip(right, 32);
  actionButton(actions, "重置属性", 0, [this] { inspector_->resetSelected(); });
  actionButton(actions, "↑", 0, [this] { order(-1); });
  actionButton(actions, "↓", 0, [this] { order(1); });
}

void DesignerApp::responsive() {
  if (arranging_ || !body_ || !right_) return;
  arranging_ = true;
  root_->updateLayout();
  const int width = root_->width();
  const bool compact = width < 960;
  if (compact != compact_) {
    compact_ = compact;
    leftVisible_ = !compact;
    rightVisible_ = !compact;
  }
  left_->width(width < 1200 ? 196 : 248);
  right_->width(width < 1200 ? 264 : 286);
  left_->floating(compact).hidden(!leftVisible_);
  right_->floating(compact).hidden(!rightVisible_);
  if (compact) {
    left_->pos(0, 0).maxWidth(std::max(100, width));
    right_->pos(std::max(0, width - 264), 0).maxWidth(std::max(100, width));
    if (leftVisible_) left_->moveToIndex(-1);
    if (rightVisible_) right_->moveToIndex(-1);
  } else {
    left_->maxWidth(HIYUI_AUTO).moveToIndex(0);
    center_->moveToIndex(1);
    right_->maxWidth(HIYUI_AUTO).moveToIndex(2);
  }
  bottom_->height(root_->height() < 650 ? 88 : 150);
  root_->updateLayout();
  if (zoomLabel_) zoomLabel_->text(std::to_string(int(surface_->zoom() * 100)) + "%");
  arranging_ = false;
}

void DesignerApp::togglePanel(bool properties) {
  if (properties) { rightVisible_ = !rightVisible_; if (compact_) leftVisible_ = false; }
  else { leftVisible_ = !leftVisible_; if (compact_) rightVisible_ = false; }
  responsive();
}

void DesignerApp::showXml(bool value) {
  if (!commitEdits()) return;
  showXml_ = value;
  canvasHost_->hidden(value);
  xml_->hidden(!value);
  designTab_->bg(value ? raised : background);
  xmlTab_->bg(value ? background : raised);
  if (value) xml_->text(layoutXml(document_.state));
}

void DesignerApp::refresh(bool properties) {
  if (!root_) return;
  if (window_) window_->title((document_.state.title + (document_.dirty() ? " *" : "") + " — HIYDesignUI").c_str());
  documentTab_->text(document_.state.title + (document_.dirty() ? "  ●" : ""));
  projectTitle_->text(document_.state.title);
  width_->text(std::to_string(document_.state.width));
  height_->text(std::to_string(document_.state.height));
  zoomLabel_->text(std::to_string(int(surface_->zoom() * 100)) + "%");
  for (int i = 0; i < 5; ++i) deviceButtons_[i]->bg(i == document_.state.device ? accent : 0x1b1b1e);
  undoButton_->enabled(document_.canUndo());
  redoButton_->enabled(document_.canRedo());
  if (showXml_) xml_->text(layoutXml(document_.state));
  refreshTree();
  if (properties) selectionChanged();
  else {
    const auto *node = document_.find(document_.selected);
    selectionLabel_->text(node ? node->id + " : " + node->type : "未选择组件");
  }
}

void DesignerApp::selectionChanged(bool preserveEditor) {
  if (!inspector_) return;
  inspector_->refresh(preserveEditor);
  const auto *node = document_.find(document_.selected);
  selectionLabel_->text(node ? node->id + " : " + node->type : "未选择组件");
  const auto rect = surface_->geometry(document_.selected);
  position_->text(node ? "X " + std::to_string(rect.x) + "   Y " + std::to_string(rect.y) +
      "   W " + std::to_string(rect.w) + "   H " + std::to_string(rect.h) : "");
  for (const auto &[id, name] : treeIds_)
    if (name == document_.selected) { tree_->selectNode(id); break; }
}

void DesignerApp::refreshTree() {
  treeIds_.clear();
  treeLabels_.clear();
  TreeNodeId next = 1;
  const auto make = [&](auto &&self, const DesignNode &node) -> TreeNodeConfig {
    TreeNodeConfig value;
    value.id = next++;
    treeIds_[value.id] = node.id;
    treeLabels_.push_back(node.id + "  : " + node.type + (node.locked ? "  [锁定]" : ""));
    value.text = treeLabels_.back().c_str();
    value.expanded = true;
    for (const auto &child : node.children) value.children.push_back(self(self, child));
    return value;
  };
  tree_->nodes({make(make, document_.state.root)});
}

void DesignerApp::queueInspector() {
  if (inspectorQueued_) return;
  inspectorQueued_ = true;
  const auto result = async::postUi([this, owner = root_] {
    if (!owner) return;
    inspectorQueued_ = false;
    selectionChanged(!fileDialog_ && !confirmShade_);
  });
  if (result != async::PostStatus::Accepted) inspectorQueued_ = false;
}

bool DesignerApp::apply(DocumentState value, std::string selected, std::string &error, bool properties) {
  relocateResources(value, resourceBase());
  error = catalog_.validate(layoutXml(value));
  if (!error.empty()) return false;
  const auto old = document_.state;
  const auto oldSelection = document_.selected;
  document_.state = std::move(value);
  document_.selected = std::move(selected);
  if (!surface_->reload(error)) {
    document_.state = old;
    document_.selected = oldSelection;
    return false;
  }
  document_.checkpoint();
  refresh(properties);
  return true;
}

bool DesignerApp::commitEdits(bool cancelGesture) {
  if (!window_ || !projectTitle_ || fileDialog_ || confirmShade_) return false;
  if (cancelGesture) surface_->cancel();
  const auto title = projectTitle_->text();
  if (!inspector_->commit()) {
    projectTitle_->text(title);
    return false;
  }
  if (title != document_.state.title) {
    document_.state.title = title;
    document_.checkpoint();
    surface_->refresh();
    refresh(false);
  }
  return true;
}

void DesignerApp::changeDevice(int index) {
  if (!commitEdits()) return;
  auto state = document_.state;
  state.device = index;
  state.width = devices[index].width;
  state.height = devices[index].height;
  std::string error;
  if (!apply(std::move(state), document_.selected, error)) log(error);
}

void DesignerApp::changeResolution() {
  int64_t width = 0, height = 0;
  if (!xmlTryParseInt(width_->text(), width) || !xmlTryParseInt(height_->text(), height) ||
      width < 240 || width > 3840 || height < 240 || height > 2160) {
    log("宽度范围 240–3840，高度范围 240–2160。"); return;
  }
  if (!commitEdits()) return;
  auto state = document_.state;
  state.width = static_cast<int>(width);
  state.height = static_cast<int>(height);
  std::string error;
  if (!apply(std::move(state), document_.selected, error)) log(error);
}

void DesignerApp::undo(bool redo) {
  if (!commitEdits()) return;
  if (!(redo ? document_.redo() : document_.undo())) return;
  std::string error;
  if (!surface_->reload(error)) {
    if (redo) document_.undo(); else document_.redo();
    log(error);
  }
  refresh();
}

void DesignerApp::erase() {
  if (!commitEdits()) return;
  DesignDocument candidate = document_;
  if (!candidate.eraseSelected()) { status("根组件或锁定组件不能删除"); return; }
  std::string error;
  if (!apply(std::move(candidate.state), candidate.selected, error)) log(error);
}

void DesignerApp::copy() {
  if (!commitEdits()) return;
  document_.copySelected();
  status("已复制 " + document_.selected);
}

void DesignerApp::paste() {
  if (!commitEdits()) return;
  DesignDocument candidate = document_;
  auto *owner = candidate.find(candidate.selected);
  if (owner && !catalog_.container(owner->type)) owner = candidate.parent(owner->id);
  if (!owner || !catalog_.container(owner->type) || !candidate.paste(owner->id)) return;
  auto *node = candidate.find(candidate.selected);
  if (automaticLayout(*owner)) { node->attributes.erase("x"); node->attributes.erase("y"); node->attributes.erase("center"); }
  std::string error;
  if (!apply(std::move(candidate.state), candidate.selected, error)) log(error);
}

void DesignerApp::order(int direction) {
  if (!commitEdits()) return;
  DesignDocument candidate = document_;
  if (!candidate.reorder(candidate.selected, direction)) return;
  std::string error;
  if (!apply(std::move(candidate.state), candidate.selected, error)) log(error);
}

std::filesystem::path DesignerApp::resourceBase() const {
  return projectPath_.empty() ? std::filesystem::current_path() : projectPath_.parent_path();
}

void DesignerApp::newProject(bool sample) {
  if (fileDialog_ || confirmShade_) return;
  confirmDiscard([this, sample] {
    auto state = sample ? loginDocument() : emptyDocument();
    std::string error;
    if (!apply(state, state.root.id, error)) { log(error); return; }
    document_.reset(std::move(state), false);
    projectPath_.clear();
    surface_->fit();
    refresh();
    log(sample ? "已打开登录示例" : "已创建空白项目");
  });
}

void DesignerApp::openProject() {
  if (!commitEdits()) return;
  fileDialog_ = true;
  const FileFilter filter{"HIYDesignUI 项目", "*.xml"};
  FileDialog::open(root_, {.mode = FileDialogMode::OpenFile, .title = "打开 HIYDesignUI 项目", .filters = &filter, .filterCount = 1},
      [this](const FileDialogResult &result) {
    fileDialog_ = false;
    if (result.status == FileDialogStatus::Cancelled) return;
    if (result.status != FileDialogStatus::Ok) { log("文件选择器不可用"); return; }
    const auto path = std::filesystem::u8path(result.path());
    DocumentState state;
    std::string error;
    if (!readProject(path, state, error) || !supportedTree(state.root, catalog_, error)) { log(error); return; }
    relocateResources(state, path.parent_path());
    error = catalog_.validate(layoutXml(state));
    if (!error.empty()) { log(error); return; }
    confirmDiscard([this, state = std::move(state), path]() mutable {
      std::string error;
      if (!apply(state, state.root.id, error)) { log(error); return; }
      document_.reset(std::move(state), true);
      projectPath_ = path;
      surface_->fit();
      refresh();
      log("已打开 " + pathText(path));
    });
  });
}

void DesignerApp::save(bool saveAs, std::function<void()> after) {
  if (!commitEdits()) return;
  const auto write = [this, after = std::move(after)](std::filesystem::path path) {
    if (!commitEdits()) return;
    if (path.extension().empty()) path += ".hiydesign.xml";
    auto state = document_.state;
    relocateResources(state, resourceBase(), path.parent_path());
    std::string error;
    if (!writeText(path, projectXml(state), error)) { log(error); return; }
    projectPath_ = std::move(path);
    document_.markSaved();
    refresh(false);
    log("已保存 " + pathText(projectPath_));
    if (after) after();
  };
  if (!saveAs && !projectPath_.empty()) { write(projectPath_); return; }
  fileDialog_ = true;
  const FileFilter filter{"HIYDesignUI 项目", "*.xml"};
  const auto folder = pathText(resourceBase());
  FileDialog::open(root_, {.mode = FileDialogMode::SaveFile, .title = "保存 HIYDesignUI 项目",
      .defaultPath = folder.c_str(), .defaultFilename = "界面.hiydesign.xml", .filters = &filter, .filterCount = 1},
      [this, write](const FileDialogResult &result) {
    fileDialog_ = false;
    if (result.status == FileDialogStatus::Ok) write(std::filesystem::u8path(result.path()));
    else if (result.status == FileDialogStatus::Error) log("文件选择器不可用");
  });
}

void DesignerApp::exportLayout() {
  if (!commitEdits()) return;
  fileDialog_ = true;
  const FileFilter filter{"HIY-UI 布局 XML", "*.xml"};
  FileDialog::open(root_, {.mode = FileDialogMode::SaveFile, .title = "导出布局 XML",
      .defaultFilename = "layout.xml", .filters = &filter, .filterCount = 1},
      [this](const FileDialogResult &result) {
    fileDialog_ = false;
    if (result.status == FileDialogStatus::Cancelled) return;
    if (result.status != FileDialogStatus::Ok) { log("文件选择器不可用"); return; }
    if (!commitEdits()) return;
    auto path = std::filesystem::u8path(result.path());
    if (path.extension().empty()) path += ".xml";
    auto state = document_.state;
    relocateResources(state, resourceBase(), path.parent_path());
    const auto xml = layoutXml(state);
    auto error = catalog_.validate(xml);
    if (!error.empty() || !writeText(path, xml, error)) { log(error); return; }
    log("已导出 " + pathText(path));
  });
}

void DesignerApp::preview() {
  if (!commitEdits()) return;
  if (previewWindow_) { previewWindow_->close(); previewWindow_ = nullptr; }
  auto &window = createWindow({.title = document_.state.title.c_str(),
      .width = document_.state.width, .height = document_.state.height, .initiallyHidden = true});
  Box root;
  root.init(window).fill();
  auto layout = XmlLayout::fromString(root, layoutXml(document_.state));
  if (layout.hasErrors()) {
    for (const auto &diagnostic : layout.diagnostics()) log(diagnostic.message);
    window.close();
    return;
  }
  previewWindow_ = &window;
  window.onCloseRequest([this, preview = &window](WindowCloseEvent &) {
    if (previewWindow_ == preview) previewWindow_ = nullptr;
  });
  window.root(std::move(root));
  window.show();
  log("交互预览已打开；修改窗口大小可查看实际布局响应。");
}

void DesignerApp::confirmDiscard(std::function<void()> action) {
  if (!commitEdits()) return;
  if (!document_.dirty()) { action(); return; }
  if (confirmShade_) return;
  confirmedAction_ = std::move(action);
  root_.with([&](Box &root) {
    Box shade;
    shade.init(root).fill().floating().clickable().ref(confirmShade_)
        .style(Style{.bgFill = {.color = RGBA(0x000000, 155)}});
    VBox dialog({.padding = 20, .gap = 16, .bg = panel, .radius = 8});
    dialog.init(shade).fillWidth().maxWidth(420).center();
    caption(dialog, "保存当前项目？", foreground, 18).fillWidth();
    caption(dialog, "当前修改尚未保存。", muted, 13).fillWidth();
    FlowBox buttons({.gap = 8, .bg = panel, .radius = 0});
    buttons.init(dialog).fillWidth();
    std::array<Ref<Button>, 3> choices;
    actionButton(buttons, "保存并继续", 0, [this] { completeConfirmation(true); }).bg(accent).ref(choices[0]);
    actionButton(buttons, "放弃修改", 0, [this] { completeConfirmation(false); }).ref(choices[1]);
    actionButton(buttons, "取消", 0, [this] { cancelConfirmation(); }).ref(choices[2]).focus();
    for (int i = 0; i < 3; ++i) choices[i]->on(keyPreviewEventCode(), [this, choices, i](Event &event) {
      const auto key = event.keyCode();
      if (key == KeyCode::Escape) cancelConfirmation();
      else if (key == KeyCode::Next || key == KeyCode::Previous)
        choices[(i + (key == KeyCode::Next ? 1 : 2)) % 3]->focus();
      else return;
      event.stopProcessing();
    });
  });
}

void DesignerApp::completeConfirmation(bool saveFirst) {
  auto action = std::move(confirmedAction_);
  confirmShade_.destroy();
  if (saveFirst) save(false, std::move(action));
  else if (action) action();
}

void DesignerApp::cancelConfirmation() {
  confirmedAction_ = {};
  confirmShade_.destroy();
}

void DesignerApp::status(const std::string &text) {
  if (status_) status_->text(text);
}

void DesignerApp::log(const std::string &text) {
  if (text.empty()) return;
  if (!outputText_.empty()) outputText_ += "\n";
  outputText_ += text;
  if (outputText_.size() > 16000) outputText_.erase(0, outputText_.find('\n', outputText_.size() - 12000) + 1);
  if (output_) output_->text(outputText_);
  status(text.substr(0, text.find('\n')));
}

}
