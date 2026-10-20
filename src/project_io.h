#pragma once

#include "design_document.h"
#include <filesystem>
#include <hiyui/xml/xml_context.h>

namespace hiydesign {
class ComponentCatalog;
struct ComponentEntry;

std::string layoutXml(const DocumentState &document);
std::string windowXml(const DocumentState &document);
std::string projectXml(const DocumentState &document);
bool parseProject(const std::string &xml, DocumentState &document, std::string &error);
bool readProject(const std::filesystem::path &path, DocumentState &document, std::string &error);
bool writeText(const std::filesystem::path &path, const std::string &text, std::string &error);
std::string pathText(const std::filesystem::path &path);
void relocateResources(DocumentState &document, const std::filesystem::path &sourceBase,
                       const std::filesystem::path &destinationBase = {});
DocumentState loginDocument();
DocumentState emptyDocument();
bool parseNodeXml(const std::string &xml, const ComponentCatalog &catalog, DesignNode &node,
                  std::string &error, const ComponentEntry *owner = nullptr, NodeKind kind = NodeKind::Component);
std::string collectionsXml(const DocumentState &document);
bool parseCollections(const std::string &xml, DocumentState &document, std::string &error);
hiyui::XmlContext previewContext(const DocumentState &document);
void ensurePreviewCollections(DocumentState &document, const ComponentCatalog &catalog);

}
