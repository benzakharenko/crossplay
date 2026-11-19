#pragma once

// Text from Notion, made drawable. The Toybox faces are ASCII only: a
// character outside it draws as NOTHING (no box, no fallback), so a title
// with an accent or an emoji would silently lose letters on the glass.
//
// Folding, in order: typography to ASCII ("smart" quotes, dashes, ellipsis),
// accented letters to their base ("é" -> "e"), line breaks and tabs to
// spaces; whatever is still outside printable ASCII (emoji, CJK) is dropped,
// and runs of spaces collapse. A title that folds to nothing at all becomes
// "(untitled)" so a row never draws blank.

#include <string>

namespace tasks {

std::string displayText(const std::string& in);
std::string displayTitle(const std::string& in);

// "2026-11-15" -> "Nov 15"; empty or malformed -> "".
std::string shortDate(const std::string& ymd);

}  // namespace tasks
