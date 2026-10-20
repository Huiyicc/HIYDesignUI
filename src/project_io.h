#pragma once

#include "design_document.h"
#include <filesystem>

namespace hiydesign {

std::string layoutXml(const DocumentState &document);
std::string projectXml(const DocumentState &document);
bool parseProject(const std::string &xml, DocumentState &document, std::string &error);
bool readProject(const std::filesystem::path &path, DocumentState &document, std::string &error);
bool writeText(const std::filesystem::path &path, const std::string &text, std::string &error);
std::string pathText(const std::filesystem::path &path);
void relocateResources(DocumentState &document, const std::filesystem::path &sourceBase,
                       const std::filesystem::path &destinationBase = {});
DocumentState loginDocument();
DocumentState emptyDocument();

}
