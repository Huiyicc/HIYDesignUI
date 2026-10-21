#include "designer_app.h"
#include "layout_presets.h"
#include "project_io.h"
#include <hiyui/misc/async.h>
#include <hiyui/misc/hotkey.h>
#include <hiyui/xml/xml_window.h>
#include <icon_codepoints.h>
#include <algorithm>
#include <cctype>
#include <utility>

namespace hiydesign {
using namespace hiyui;
namespace {
constexpr auto background = EditorColor::Background;
constexpr auto panel = EditorColor::Panel;
constexpr auto raised = EditorColor::Raised;
constexpr auto line = EditorColor::Border;
constexpr auto foreground = EditorColor::Text;
constexpr auto muted = EditorColor::Muted;
constexpr auto accent = EditorColor::Accent;

}

Button DesignerApp::actionButton(Widget &parent, const char *text, uint32_t icon,
                                 std::function<void()> action) {
  Button button({.text = text, .iconCodepoint = icon, .iconSize = 16,
                 .variant = ButtonVariant::Secondary, .height = 28,
                 .fontSize = 12, .radius = 3});
  button.init(parent).padding(8, 0).borderWidth(0);
  appearance_.button(button);
  if (action) button.onClick(std::move(action));
  return button;
}

Label DesignerApp::caption(Widget &parent, const char *text, EditorColor role, int size) {
  Label label({.text = text, .fontSize = size, .textColor = color(role)});
  label.init(parent);
  appearance_.foreground(label, role);
  return label;
}

HBox DesignerApp::strip(Widget &parent, int height, EditorColor role) {
  HBox row({.gap = 4, .bg = color(role), .radius = 0, .scroll = ScrollMode::Horizontal});
  row.init(parent).fillWidth().height(height).padding(6, 0)
      .scrollbars(ScrollbarMode::Off);
  appearance_.background(row, role);
  return row;
}

namespace {
std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

bool supportedTree(const DesignNode &node, const ComponentCatalog &catalog, std::string &error) {
  if (node.kind == NodeKind::Component && !catalog.find(node.type)) { error = "未注册组件 " + node.type; return false; }
  for (const auto &child : node.children) if (!supportedTree(child, catalog, error)) return false;
  return true;
}
}

DesignerApp::~DesignerApp() {
  appearance_.stop();
  rootDelete_.off();
  surface_.reset();
  inspector_.reset();
}

void DesignerApp::setup() {
  setTheme(Theme::fromSeed(0x0078d4));
  document_.reset(adaptiveDocument(), false);
  document_.selected = "projectSettings";
  window_ = &createWindow({.title = "HIYDesignUI", .width = 1440, .height = 900,
      .borderless = true, .titleBar = {.height = 32, .bg = 0x2d2d30, .textColor = color(foreground), .fontSize = 12}});
  Box root({.bg = color(background)});
  root.init(*window_).fill().ref(root_).scrollMode(ScrollMode::Off);
  appearance_.background(root, background);
  VBox shell({.bg = color(background), .radius = 0});
  shell.init(root).fill().ref(shell_);
  appearance_.background(shell, background);
  buildMenus(shell);
  buildToolbar(shell);
  HBox body({.gap = 1, .bg = color(line), .radius = 0});
  body.init(shell).fillWidth().height(0).flex(1).ref(body_).scrollMode(ScrollMode::Off);
  appearance_.background(body, line);
  buildPalette(body);
  buildWorkspace(body);
  buildInspector(body);
  auto statusBar = strip(shell, 24, accent);
  caption(statusBar, "就绪", EditorColor::White, 11).ref(status_).width(0).flex(1).textOverflow(TextOverflow::Ellipsis);
  caption(statusBar, "", EditorColor::White, 11).ref(position_);
  caption(statusBar, "UTF-8  ·  XML", EditorColor::White, 11);
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
  root.on(EventCode::Delete, [this] { appearance_.stop(); window_ = nullptr; }, rootDelete_.out());
  appearance_.onChange = [this] { refreshColors(); };
  window_->root(std::move(root));
  std::string error;
  if (!surface_->reload(error)) log(error);
  refresh();
  refreshColors();
  responsive();
  log("项目已加载。拖入组件，或选择组件后在画布内拖出创建区域。");
}

void DesignerApp::buildMenus(Widget &parent) {
  MenuBar menu({.height = 28, .bg = color(panel), .textColor = color(foreground),
                .hoverColor = 0x3e3e42, .activeColor = 0x04395e, .fontSize = 12});
  menu.init(parent).fillWidth();
  Ref<MenuBar> menuRef;
  menu.ref(menuRef);
  appearance_.bind(menu, [menuRef](Widget &, const EditorPalette &p) {
    menuRef->palette({.bg = p[EditorColor::Panel], .textColor = p[EditorColor::Text],
        .hoverColor = p[EditorColor::Hover], .activeColor = p[EditorColor::Selected],
        .activeTextColor = p[EditorColor::Text], .disabledTextColor = p[EditorColor::Muted]});
  });
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
  std::vector<ContextMenuItem> presets;
  for (const auto &preset : layoutPresets()) {
    presets.push_back(item(preset.name, [this, preset] {
      auto state = emptyDocument();
      std::string error;
      if (!parseNodeXml(preset.xml, catalog_, state.root, error)) { log(error); return; }
      state.title = preset.name;
      newProject(std::move(state));
    }));
  }
  menu.items({
      {.text = "文件", .children = {
          item("新建项目", [this] { newProject(emptyDocument()); }, {.sdlKey = 'n', .primary = true}),
          {.text = "从布局预设新建", .children = std::move(presets)},
          item("打开项目…", [this] { openProject(); }, {.sdlKey = 'o', .primary = true}),
          item("保存", [this] { save(); }, {.sdlKey = 's', .primary = true}),
          item("另存为…", [this] { save(true); }, {.sdlKey = 's', .primary = true, .shift = true}),
          item("导出窗口 XML…", [this] { exportLayout(true); }),
          item("导出布局 XML…", [this] { exportLayout(); }),
          item("打开多端工作台示例", [this] { newProject(adaptiveDocument()); }),
          item("打开登录示例", [this] { newProject(loginDocument()); })}},
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
          item("显示 / 隐藏辅助线", [this] { surface_->toggleGuides(); }),
          item("清空辅助线", [this] { surface_->clearGuides(); }),
          item("切换深色 / 浅色主题", [this] { toggleTheme(); }),
          item("开启 / 关闭主题动画", [this] {
            themeAnimation_ = !themeAnimation_;
            if (!themeAnimation_) appearance_.change(appearance_.dark(), false);
            status(themeAnimation_ ? "主题动画已开启" : "主题动画已关闭");
          }),
          item("窗口 XML", [this] { showXml(!showXml_); })}},
      {.text = "运行", .children = {item("交互预览", [this] { preview(); }),
          item("预览所选对象", [this] { previewObject(); }), item("编辑预览数据", [this] { editPreviewData(); })}},
      {.text = "帮助", .children = {
          item("画布操作", [this] { log("拖入或框选创建；拖动主体移动；边框手柄调整尺寸；靠近辅助线吸附，按住 Ctrl 临时关闭；方向键微调；Delete 删除；Esc 取消。自动布局中的拖动改变顺序。"); }),
          item("响应式布局", [this] { log("Box 使用绝对定位。VBox / HBox / FlowBox 使用自动布局；fill、flex、maxWidth 控制随视口变化的尺寸。XML 页展示当前真实布局。"); })}}
  });
}

void DesignerApp::buildToolbar(Widget &parent) {
  auto toolbar = strip(parent, 38, raised);
  toolbar.scrollMode(ScrollMode::Off);
  auto row = strip(toolbar, 38, raised);
  row.width(0).flex(1).padding(0);
  actionButton(row, nullptr, HIYUI_ICON_ADD, [this] { newProject(emptyDocument()); }).tooltip("新建项目");
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
  auto previewButton = actionButton(row, "预览", HIYUI_ICON_PLAY_ARROW, [this] { preview(); });
  appearance_.foreground(previewButton, EditorColor::Success);
  actionButton(row, "导出 XML", 0, [this] { exportLayout(true); }).tooltip("导出窗口 XML");
  caption(row, "│", line);
  actionButton(row, "组件", 0, [this] { togglePanel(false); });
  actionButton(row, "属性", 0, [this] { togglePanel(true); });
  actionButton(toolbar, nullptr, HIYUI_ICON_LIGHT_MODE, [this] { toggleTheme(); })
      .width(32).ref(themeButton_);
  refreshThemeButton();
}

void DesignerApp::buildPalette(Widget &parent) {
  VBox left({.gap = 0, .bg = color(panel), .radius = 0});
  left.init(parent).width(248).fillHeight().ref(left_);
  appearance_.background(left, panel);
  auto title = strip(left, 34);
  caption(title, "组件箱").flex(1);
  actionButton(title, "×", 0, [this] { togglePanel(false); }).width(24);
  Input search({.placeholder = "搜索组件…", .height = 28, .bg = color(background),
                .textColor = color(foreground), .placeholderColor = 0x888888, .fontSize = 12, .radius = 3});
  search.init(left).fillWidth().padding(6, 3);
  appearance_.input(search);
  search.onChange([this](const std::string &text) {
    for (const auto &[type, button] : paletteButtons_)
      if (button) button->hidden(lower(type).find(lower(text)) == std::string::npos);
  });
  VBox list({.padding = 6, .gap = 2, .bg = color(panel), .radius = 0, .scroll = ScrollMode::Vertical});
  list.init(left).fillWidth().height(0).flex(1);
  appearance_.background(list, panel);
  std::vector<std::string> categories;
  for (const auto &entry : catalog_.entries())
    if (std::find(categories.begin(), categories.end(), entry.category) == categories.end()) categories.push_back(entry.category);
  for (const auto &category : categories) {
    caption(list, category.c_str(), muted, 11).fillWidth().height(24);
    VBox group({.gap = 0, .bg = color(panel), .radius = 0});
    group.init(list).fillWidth();
    appearance_.background(group, panel);
    for (const auto &entry : catalog_.entries()) {
      if (entry.category != category) continue;
      auto button = actionButton(group, nullptr, 0);
      Ref<Button> ref;
      button.height(26).fillWidth().bg(color(panel)).ref(ref).pressLock().scrollChain(false);
      HBox content({.gap = 9, .bg = HIYUI_TRANSPARENT, .radius = 0});
      content.init(button).fill().clickable(false);
      Icon icon({.codepoint = entry.icon(), .size = 16});
      icon.init(content).size(16, 16).color(0x7fb3e0).clickable(false);
      Ref<Icon> iconRef;
      icon.ref(iconRef);
      appearance_.bind(icon, [iconRef](Widget &, const EditorPalette &p) { iconRef->color(p[EditorColor::Icon]); });
      const auto label = entry.namespaceUri == XmlBuiltinNamespaceUri ? entry.localName : entry.prefix + ":" + entry.localName;
      caption(content, label.c_str()).clickable(false);
      button.tooltip(entry.metadata.resourceOnly ? "添加配置对象，在属性面板中单独预览" : "拖入画布，或选择目标节点后点击添加组件");
      paletteButtons_.push_back({entry.type, ref});
      for (const auto code : {EventCode::Pressed, EventCode::Pressing, EventCode::Released, EventCode::PressLost})
        button.on(code, [this, type = entry.type](Event &event) {
          if (event.code() == EventCode::Pressed) paletteType_ = type;
          const auto *entry = catalog_.find(type);
          const auto *selected = document_.find(document_.selected);
          if (entry && (entry->metadata.resourceOnly || (selected && !catalog_.visual(document_, *selected)))) {
            if (event.code() == EventCode::Released) insertComponent(type);
            return;
          }
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
  VBox center({.bg = color(background), .radius = 0});
  center.init(parent).width(0).flex(1).fillHeight().ref(center_);
  appearance_.background(center, background);
  auto tabs = strip(center, 32, panel);
  actionButton(tabs, "设计", 0, [this] { showXml(false); }).ref(designTab_).bg(color(background));
  actionButton(tabs, "XML", 0, [this] { showXml(true); }).ref(xmlTab_);
  caption(tabs, "用户登录", muted, 11).ref(documentTab_).width(0).flex(1).textOverflow(TextOverflow::Ellipsis);
  auto deviceBar = strip(center, 36, panel);
  HBox modes({.padding = 2, .gap = 2, .bg = 0x1b1b1e, .radius = 5});
  modes.init(deviceBar);
  appearance_.background(modes, EditorColor::Inset);
  const uint32_t deviceIcons[] = {HIYUI_ICON_SMARTPHONE, HIYUI_ICON_LANGUAGE,
      HIYUI_ICON_DESKTOP_WINDOWS, HIYUI_ICON_COMPUTER, HIYUI_ICON_LAPTOP_MAC};
  for (int i = 0; i < 5; ++i)
    actionButton(modes, devices[i].name, deviceIcons[i], [this, i] { changeDevice(i); })
        .height(24).fontSize(11).ref(deviceButtons_[i]);
  actionButton(deviceBar, "系统边框", 0, [this] { changeFrame(); }).ref(frameButton_)
      .tooltip("桌面预览切换系统边框与 HIY-UI 自绘标题栏");
  auto previewBar = strip(center, 34, panel);
  caption(previewBar, "内容尺寸", muted, 11);
  Input width({.text = "1280", .maxLength = 4, .width = 56, .height = 26,
                .bg = color(background), .textColor = color(foreground), .fontSize = 11, .radius = 3});
  width.init(previewBar).padding(6, 3).ref(width_).on(EventCode::Ready, [this] { changeResolution(); });
  appearance_.input(width);
  caption(previewBar, "×", muted, 11);
  Input height({.text = "800", .maxLength = 4, .width = 56, .height = 26,
                 .bg = color(background), .textColor = color(foreground), .fontSize = 11, .radius = 3});
  height.init(previewBar).padding(6, 3).ref(height_).on(EventCode::Ready, [this] { changeResolution(); });
  appearance_.input(height);
  actionButton(previewBar, "应用", 0, [this] { changeResolution(); });
  actionButton(previewBar, "−", 0, [this] { surface_->zoom(surface_->zoom() - 0.1f); refresh(false); });
  caption(previewBar, "100%", muted, 11).ref(zoomLabel_).width(42);
  actionButton(previewBar, "+", 0, [this] { surface_->zoom(surface_->zoom() + 0.1f); refresh(false); });
  actionButton(previewBar, "适合", 0, [this] { surface_->fit(); refresh(false); });
  Box work({.bg = color(background)});
  work.init(center).fillWidth().height(0).flex(1);
  appearance_.background(work, background);
  Box canvas({.bg = color(background)});
  canvas.init(work).fill().ref(canvasHost_);
  appearance_.background(canvas, background);
  surface_ = std::make_unique<DesignSurface>(document_, catalog_, appearance_);
  surface_->init(canvas);
  surface_->onSelection = [this] { selectionChanged(); };
  surface_->onChange = [this] { refresh(); };
  surface_->onToolChanged = [this] {
    for (const auto &[type, button] : paletteButtons_)
      if (button) button->bg(color(surface_->tool() == type ? EditorColor::ToolSelected : panel));
  };
  surface_->onStatus = [this](const std::string &text) { status(text); };
  surface_->onDelete = [this] { erase(); };
  surface_->onUndo = [this] { undo(); };
  surface_->onRedo = [this] { undo(true); };
  surface_->beforeEdit = [this] { return !inspector_ || commitEdits(false); };
  Input xml({.multiline = true, .horizontalScroll = true, .bg = color(background),
              .textColor = 0x9cdcfe, .fontSize = 12, .radius = 0, .readOnly = true});
  xml.init(work).fill().hidden().ref(xml_);
  appearance_.input(xml, EditorColor::Code);
  VBox bottom({.bg = color(panel), .radius = 0});
  bottom.init(center).fillWidth().height(150).ref(bottom_);
  appearance_.background(bottom, panel);
  auto outputBar = strip(bottom, 30);
  caption(outputBar, "输出", foreground, 11).flex(1);
  actionButton(outputBar, "清空", 0, [this] { outputText_.clear(); output_->text(""); });
  Input output({.multiline = true, .bg = color(background), .textColor = 0x9fb3c8,
                 .fontSize = 11, .radius = 0, .readOnly = true});
  output.init(bottom).fillWidth().height(0).flex(1).ref(output_);
  appearance_.input(output, EditorColor::Output);
}

void DesignerApp::buildInspector(Widget &parent) {
  VBox right({.bg = color(panel), .radius = 0});
  right.init(parent).width(286).fillHeight().ref(right_);
  appearance_.background(right, panel);
  auto tabs = strip(right, 34);
  actionButton(tabs, "属性", 0, [this] {
    propertyHost_->hidden(false); treeHost_->hidden(); refreshColors();
  }).ref(propsTab_).bg(0x04395e);
  actionButton(tabs, "组件树", 0, [this] {
    propertyHost_->hidden(); treeHost_->hidden(false); refreshColors();
  }).ref(treeTab_);
  actionButton(tabs, "×", 0, [this] { togglePanel(true); }).width(24);
  Input title({.text = document_.state.title.c_str(), .placeholder = "界面标题", .height = 28,
                .bg = color(background), .textColor = color(foreground), .fontSize = 12, .radius = 3});
  title.init(right).fillWidth().padding(6, 3).ref(projectTitle_);
  appearance_.input(title);
  title.on(EventCode::Ready, [this] { commitEdits(false); });
  title.on(EventCode::Defocused, [this] { commitEdits(false); });
  caption(right, "", muted, 11).ref(selectionLabel_).fillWidth().height(30).textOverflow(TextOverflow::Ellipsis);
  auto nodeActions = strip(right, 32);
  actionButton(nodeActions, "添加组件", 0, [this] { insertComponent(paletteType_); });
  actionButton(nodeActions, "新增子项", 0, [this] { insertItem(); });
  actionButton(nodeActions, "预览对象", 0, [this] { previewObject(); }).ref(objectPreview_);
  Box host({.bg = color(panel)});
  host.init(right).fillWidth().height(0).flex(1);
  appearance_.background(host, panel);
  Box property({.bg = color(panel)});
  property.init(host).fill().ref(propertyHost_);
  appearance_.background(property, panel);
  inspector_ = std::make_unique<PropertyInspector>(document_, catalog_, appearance_);
  inspector_->init(property);
  inspector_->refreshRequested = [this] { queueInspector(); };
  inspector_->apply = [this](DocumentState state, std::string selected, std::string &error) {
    if (fileDialog_ || confirmShade_) {
      error = "请先完成当前对话框操作";
      return false;
    }
    const bool result = apply(std::move(state), std::move(selected), error, false);
    if (result) queueInspector();
    return result;
  };
  Box treeHost({.bg = color(panel)});
  treeHost.init(host).fill().hidden().ref(treeHost_);
  appearance_.background(treeHost, panel);
  Tree tree({.rowHeight = 28, .indent = 14, .showConnectLine = true, .draggable = true,
             .bg = color(panel), .textColor = color(foreground), .selectedBg = 0x04395e,
             .hoverBg = color(raised), .connectLineColor = color(line), .fontSize = 12});
  tree.init(treeHost).fill().ref(tree_);
  appearance_.bind(tree, [tree = tree_](Widget &, const EditorPalette &p) {
    tree->palette({.bg = p[EditorColor::Panel], .textColor = p[EditorColor::Text],
        .selectedBg = p[EditorColor::Selected], .hoverBg = p[EditorColor::Hover],
        .connectLineColor = p[EditorColor::Border], .selectedTextColor = p[EditorColor::Text]});
  });
  tree.onChange([this](const std::vector<TreeNodeId> &ids) {
    if (treeDropQueued_ || ids.empty() || !treeIds_.contains(ids.front())) return;
    if (!surface_->select(treeIds_.at(ids.front()))) syncTreeSelection();
  });
  tree.nodeContextMenu([this](TreeNodeId id) { return treeMenu(id); });
  tree.onDrop([this](TreeNodeId source, TreeNodeId target, TreeDropPosition position) {
    queueTreeDrop(source, target, position);
  });
  auto actions = strip(right, 32);
  actionButton(actions, "重置属性", 0, [this] { inspector_->resetSelected(); });
  actionButton(actions, "↑", 0, [this] { order(-1); });
  actionButton(actions, "↓", 0, [this] { order(1); });
}

void DesignerApp::toggleTheme() {
  if (fileDialog_ || confirmShade_) return;
  appearance_.change(!appearance_.dark(), themeAnimation_);
  refreshThemeButton();
}

void DesignerApp::refreshThemeButton() {
  if (!themeButton_) return;
  const bool dark = appearance_.dark();
  const char *label = dark ? "切换浅色主题" : "切换深色主题";
  themeButton_->icon(dark ? HIYUI_ICON_LIGHT_MODE : HIYUI_ICON_DARK_MODE)
      .tooltip(label).accessibilityName(label);
}

void DesignerApp::refreshColors() {
  if (!root_) return;
  using C = EditorColor;
  const auto &p = appearance_.palette();
  root_.with([&](Box &root) {
    ContextMenu::palette(root, {.bg = p[C::Panel], .textColor = p[C::Text],
        .secondaryTextColor = p[C::Muted], .disabledTextColor = p[C::Muted],
        .hoverBg = p[C::Hover], .pressedBg = p[C::Selected],
        .borderColor = p[C::Border], .dividerColor = p[C::Border]});
  });
  if (window_) window_->titleBarPalette({.bg = p[C::Raised], .textColor = p[C::Text],
      .iconColor = p[C::Text], .iconHoverColor = p[C::Text], .closeHoverColor = 0xe81123});
  if (designTab_) designTab_->bg(p[showXml_ ? C::Raised : C::Background]);
  if (xmlTab_) xmlTab_->bg(p[showXml_ ? C::Background : C::Raised]);
  if (propertyHost_ && propsTab_ && treeTab_) {
    const bool properties = !propertyHost_->isHidden();
    propsTab_->bg(p[properties ? C::Selected : C::Raised]);
    treeTab_->bg(p[properties ? C::Raised : C::Selected]);
  }
  for (int i = 0; i < 5; ++i) if (deviceButtons_[i]) {
    const bool selected = i == document_.state.device;
    deviceButtons_[i]->style(StyleSet{
        .normal = {.color = p[selected ? C::White : C::Text], .bgFill = {.color = p[selected ? C::Accent : C::Inset]}},
        .hovered = {.color = p[selected ? C::White : C::Text], .bgFill = {.color = p[selected ? C::Accent : C::Hover]}},
        .pressed = {.color = p[selected ? C::White : C::Text], .bgFill = {.color = p[selected ? C::Accent : C::Selected]}},
        .focused = {.color = p[selected ? C::White : C::Text], .bgFill = {.color = p[selected ? C::Accent : C::Inset]}}});
  }
  if (surface_) {
    for (const auto &[type, button] : paletteButtons_)
      if (button) button->bg(p[surface_->tool() == type ? C::ToolSelected : C::Panel]);
    surface_->refresh();
  }
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
  refreshColors();
  if (value) xml_->text(windowXml(document_.state));
}

void DesignerApp::refresh(bool properties) {
  if (!root_) return;
  if (window_) window_->title((document_.state.title + (document_.dirty() ? " *" : "") + " — HIYDesignUI").c_str());
  documentTab_->text(document_.state.title + (document_.dirty() ? "  ●" : ""));
  projectTitle_->text(document_.state.title);
  width_->text(std::to_string(document_.state.width));
  height_->text(std::to_string(document_.state.height));
  zoomLabel_->text(std::to_string(int(surface_->zoom() * 100)) + "%");
  refreshColors();
  frameButton_->text(document_.state.borderless ? "自绘标题栏" : "系统边框")
      .enabled(document_.state.device >= 2);
  undoButton_->enabled(document_.canUndo());
  redoButton_->enabled(document_.canRedo());
  if (showXml_) xml_->text(windowXml(document_.state));
  refreshTree();
  if (properties) selectionChanged();
  else {
    const auto *node = document_.find(document_.selected);
    selectionLabel_->text(node ? node->id + " : " + nodeName(*node) : "未选择组件");
  }
}

void DesignerApp::selectionChanged(bool preserveEditor) {
  if (!inspector_) return;
  inspector_->refresh(preserveEditor);
  const auto *node = document_.find(document_.selected);
  selectionLabel_->text(node ? node->id + " : " + nodeName(*node) : "未选择组件");
  const auto *entry = node && node->kind == NodeKind::Component ? catalog_.find(node->type) : nullptr;
  if (objectPreview_) objectPreview_->enabled(entry && entry->metadata.activatable);
  const auto rect = surface_->geometry(document_.selected);
  position_->text(node && catalog_.visual(document_, *node) ? "X " + std::to_string(rect.x) + "   Y " + std::to_string(rect.y) +
      "   W " + std::to_string(rect.w) + "   H " + std::to_string(rect.h) : "");
  syncTreeSelection();
}

void DesignerApp::refreshTree() {
  if (!tree_) return;
  std::map<std::string, bool> expanded;
  for (const auto &[id, name] : treeIds_) expanded[name] = tree_->isExpanded(id);
  treeIds_.clear();
  treeLabels_.clear();
  TreeNodeId next = 1;
  const auto make = [&](auto &&self, const DesignNode &node) -> TreeNodeConfig {
    TreeNodeConfig value;
    value.id = next++;
    treeIds_[value.id] = node.id;
    treeLabels_.push_back(node.id + "  : " + nodeName(node) + (node.locked ? "  [锁定]" : ""));
    value.text = treeLabels_.back().c_str();
    if (const auto *entry = catalog_.owner(document_, node)) value.iconCodepoint = entry->icon();
    value.expanded = !expanded.contains(node.id) || expanded.at(node.id);
    for (const auto &child : node.children) value.children.push_back(self(self, child));
    return value;
  };
  tree_->nodes({make(make, document_.state.root)});
  syncTreeSelection();
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
    surface_->refreshTitle();
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

void DesignerApp::changeFrame() {
  if (!commitEdits() || document_.state.device < 2) return;
  auto state = document_.state;
  state.borderless = !state.borderless;
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
  if (!candidate.clipboard()) return;
  auto copied = *candidate.clipboard();
  copied.id.clear();
  while (owner && !catalog_.accepts(candidate, *owner, copied)) owner = candidate.parent(owner->id);
  if (!owner || !candidate.paste(owner->id)) { status("目标节点不接受剪贴板内容"); return; }
  auto *node = candidate.find(candidate.selected);
  std::string error;
  if (!catalog_.assignItemIds(candidate, *owner, *node, error)) { log(error); return; }
  if (catalog_.automaticLayout(*owner)) { node->attributes.erase("x"); node->attributes.erase("y"); node->attributes.erase("center"); }
  ensurePreviewCollections(candidate.state, catalog_);
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

void DesignerApp::newProject(DocumentState value) {
  if (fileDialog_ || confirmShade_) return;
  confirmDiscard([this, state = std::move(value)]() mutable {
    std::string error;
    if (!apply(state, state.root.id, error)) { log(error); return; }
    document_.reset(std::move(state), false);
    projectPath_.clear();
    surface_->fit();
    refresh();
    log("已创建项目：" + document_.state.title);
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

void DesignerApp::exportLayout(bool withWindow) {
  if (!commitEdits()) return;
  fileDialog_ = true;
  const FileFilter filter{withWindow ? "HIY-UI 窗口 XML" : "HIY-UI 布局 XML", "*.xml"};
  FileDialog::open(root_, {.mode = FileDialogMode::SaveFile, .title = withWindow ? "导出窗口 XML" : "导出布局 XML",
      .defaultFilename = withWindow ? "window.xml" : "layout.xml", .filters = &filter, .filterCount = 1},
      [this, withWindow](const FileDialogResult &result) {
    fileDialog_ = false;
    if (result.status == FileDialogStatus::Cancelled) return;
    if (result.status != FileDialogStatus::Ok) { log("文件选择器不可用"); return; }
    if (!commitEdits()) return;
    auto path = std::filesystem::u8path(result.path());
    if (path.extension().empty()) path += ".xml";
    auto state = document_.state;
    relocateResources(state, resourceBase(), path.parent_path());
    const auto xml = withWindow ? windowXml(state) : layoutXml(state);
    auto error = catalog_.validate(xml);
    if (!error.empty() || !writeText(path, xml, error)) { log(error); return; }
    log("已导出 " + pathText(path));
  });
}

void DesignerApp::preview() {
  if (!commitEdits()) return;
  if (previewWindow_) { previewWindow_->close(); previewWindow_ = nullptr; }
  auto layout = XmlWindowLayout::fromString(windowXml(document_.state), previewContext(document_.state));
  if (!layout) {
    for (const auto &diagnostic : layout.diagnostics()) log(diagnostic.message);
    return;
  }
  previewWindow_ = layout.window();
  previewWindow_->onCloseRequest([this, preview = previewWindow_](WindowCloseEvent &) {
    if (previewWindow_ == preview) previewWindow_ = nullptr;
  });
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
    VBox dialog({.padding = 20, .gap = 16, .bg = color(panel), .radius = 8});
    dialog.init(shade).fillWidth().maxWidth(420).center();
    appearance_.background(dialog, panel);
    caption(dialog, "保存当前项目？", foreground, 18).fillWidth();
    caption(dialog, "当前修改尚未保存。", muted, 13).fillWidth();
    FlowBox buttons({.gap = 8, .bg = color(panel), .radius = 0});
    buttons.init(dialog).fillWidth();
    appearance_.background(buttons, panel);
    std::array<Ref<Button>, 3> choices;
    actionButton(buttons, "保存并继续", 0, [this] { completeConfirmation(true); }).bg(color(accent)).ref(choices[0]);
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
