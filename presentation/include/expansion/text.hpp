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

// Turns a content id into the words a reader expects: "iron_ore" becomes
// "Iron Ore", "extraction_site" becomes "Extraction Site". Identity stays the id;
// this is display only (TDD 4.1).
std::string display_name(const std::string& content_id);

// The name to show for a definition: its authored display_key when the strings
// table has one, otherwise words derived from the id. A host calls this rather
// than choosing between the two itself.
std::string name_of(const std::string& display_key, const std::string& content_id);

// Substitutes {planet} in an already-formatted line. Kept separate from format()
// so a planet id is never rendered as a number.
std::string with_planet(std::string body, const std::string& planet_id);

std::string news_line(const NewsRecord& n);
std::string fact_line(const FactRecord& f);
// Plain-language rendering of a typed reason id.
const std::string& reason_text(const std::string& reason_id);

}  // namespace expansion::text
