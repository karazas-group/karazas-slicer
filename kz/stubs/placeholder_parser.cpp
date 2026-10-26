// The template language for custom G-code and output file names. The pricing
// build writes neither, so these stand in for PlaceholderParser.cpp (a large
// Boost.Spirit grammar): templates come back unexpanded.
#include "libslic3r/PlaceholderParser.hpp"

namespace Slic3r {
PlaceholderParser::PlaceholderParser(const DynamicConfig *external_config) : m_external_config(external_config) {}
bool PlaceholderParser::apply_config(const DynamicPrintConfig &) { return false; }
std::string PlaceholderParser::process(const std::string &templ, unsigned int, const DynamicConfig *, DynamicConfig *, ContextData *) const { return templ; }
void PlaceholderParser::update_timestamp(DynamicConfig &) {}
void PlaceholderParser::update_user_name(DynamicConfig &) {}
}
