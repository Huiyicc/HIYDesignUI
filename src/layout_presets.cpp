#include "layout_presets.h"

namespace hiydesign {

std::span<const LayoutPreset> layoutPresets() {
  static constexpr LayoutPreset presets[] = {
      {"导航＋内容", R"xml(<WindowFrame xmlns="urn:hiyui:ui" id="root" fill="both"
    wideMinWidth="840" narrowNavigation="Drawer" wideNavigation="Docked"
    navigationWidth="248" drawerWidth="288" drawerMaxWidthPercent="86"
    dockedNavigationFullHeight="true">
  <FrameHeader id="header">
    <HBox id="headerBar" fill="width" padding="12" gap="12" bg="#ffffff">
      <FrameNavigationToggle id="navigationToggle" size="44"/>
      <Label id="pageTitle" text="工作台" fontSize="20" color="#16181d" width="0" flex="1"/>
    </HBox>
  </FrameHeader>
  <FrameNavigation id="navigation">
    <VBox id="sidebar" fill="both" padding="12" gap="12" bg="#ffffff">
      <Label id="navigationTitle" text="导航" fontSize="18" color="#16181d"/>
      <List id="navigationItems" fill="width" height="0" flex="1">
        <Item text="概览"/>
        <Item text="项目"/>
        <Item text="设置"/>
      </List>
    </VBox>
  </FrameNavigation>
  <FrameContent id="content">
    <VBox id="page" fill="both" padding="16" gap="16" scroll="Vertical" bg="#f3f4f6">
      <Label id="contentTitle" text="项目概览" fontSize="24" color="#16181d" fill="width"/>
      <Card id="projectCard" fill="width" padding="16" gap="12" bg="#ffffff" radius="12" shadow="false">
        <Label id="projectLabel" text="项目名称" fontSize="14" color="#3d434e"/>
        <Input id="projectName" placeholder="输入项目名称" fill="width" height="44"/>
        <Label id="descriptionLabel" text="项目说明" fontSize="14" color="#3d434e"/>
        <Input id="description" placeholder="输入项目说明" fill="width" height="44"/>
      </Card>
    </VBox>
  </FrameContent>
</WindowFrame>)xml"},
      {"列表＋详情", R"xml(<ListDetail xmlns="urn:hiyui:ui" id="root" fill="both" splitMinWidth="720" listWidth="240" gap="12">
  <ListDetailList id="listRegion">
    <VBox id="listPane" fill="both" padding="16" gap="12" bg="#ffffff">
      <Label id="listTitle" text="项目列表" fontSize="20" color="#16181d"/>
      <Input id="filter" placeholder="筛选项目" fill="width" height="44"/>
      <List id="projects" fill="width" height="0" flex="1">
        <Item text="项目一"/>
        <Item text="项目二"/>
        <Item text="项目三"/>
      </List>
    </VBox>
  </ListDetailList>
  <ListDetailContent id="detailRegion">
    <VBox id="detailPane" fill="both" padding="16" gap="16" scroll="Vertical" bg="#ffffff">
      <Label id="detailTitle" text="项目详情" fontSize="20" color="#16181d" fill="width"/>
      <Label id="nameLabel" text="名称" fontSize="14" color="#3d434e"/>
      <Input id="name" placeholder="项目名称" fill="width" height="44"/>
      <Label id="notesLabel" text="备注" fontSize="14" color="#3d434e"/>
      <Input id="notes" placeholder="项目备注" fill="width" height="44"/>
    </VBox>
  </ListDetailContent>
</ListDetail>)xml"},
      {"自动换行卡片", R"xml(<FlowBox xmlns="urn:hiyui:ui" id="root" fill="both" padding="16" gap="16" scroll="Vertical" bg="#f3f4f6">
  <Card id="cardOne" width="200" maxWidth="360" height="200" flex="1" padding="16" gap="12" bg="#ffffff" radius="12" shadow="false">
    <Label id="titleOne" text="项目一" fontSize="18" color="#16181d" fill="width"/>
    <Label id="labelOne" text="名称" fontSize="14" color="#3d434e"/>
    <Input id="nameOne" placeholder="项目名称" fill="width" height="44"/>
  </Card>
  <Card id="cardTwo" width="200" maxWidth="360" height="200" flex="1" padding="16" gap="12" bg="#ffffff" radius="12" shadow="false">
    <Label id="titleTwo" text="项目二" fontSize="18" color="#16181d" fill="width"/>
    <Label id="labelTwo" text="名称" fontSize="14" color="#3d434e"/>
    <Input id="nameTwo" placeholder="项目名称" fill="width" height="44"/>
  </Card>
  <Card id="cardThree" width="200" maxWidth="360" height="200" flex="1" padding="16" gap="12" bg="#ffffff" radius="12" shadow="false">
    <Label id="titleThree" text="项目三" fontSize="18" color="#16181d" fill="width"/>
    <Label id="labelThree" text="名称" fontSize="14" color="#3d434e"/>
    <Input id="nameThree" placeholder="项目名称" fill="width" height="44"/>
  </Card>
  <Card id="cardFour" width="200" maxWidth="360" height="200" flex="1" padding="16" gap="12" bg="#ffffff" radius="12" shadow="false">
    <Label id="titleFour" text="项目四" fontSize="18" color="#16181d" fill="width"/>
    <Label id="labelFour" text="名称" fontSize="14" color="#3d434e"/>
    <Input id="nameFour" placeholder="项目名称" fill="width" height="44"/>
  </Card>
</FlowBox>)xml"},
  };
  return presets;
}

}
