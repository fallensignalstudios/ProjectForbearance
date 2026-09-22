// Display strings and formatting, stored separately from logic from the first
// build (TDD 15.5). Reformatting or localisation must not change the historical
// facts it renders (TDD 13.3).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "expansion/state.hpp"
#include "expansion/units.hpp"

namespace expansion::text {

// Looks up a template key. An unknown key returns the key itself so a missing
// string is visible rather than silently blank.
const std::string& lookup(const std::string& key);

// Substitutes {name} placeholders from numeric and text arguments. Numeric
// arguments render as milli-unit decimals unless the placeholder ends in _bp,
// _day, _days or _count.
std::string format(const std::string& key, const std::vector<NamedValue>& args,
                   const std::vector<std::string>& text_args);

std::string news_line(const NewsRecord& n);
std::string fact_line(const FactRecord& f);
// Plain-language rendering of a typed reason id.
const std::string& reason_text(const std::string& reason_id);

}  // namespace expansion::text
