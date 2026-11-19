#include "TasksText.h"

#include <Utf8.h>

namespace tasks {

std::string displayText(const std::string& in) {
  const std::string folded = utf8FoldTypography(in);
  std::string out;
  out.reserve(folded.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(folded.c_str());
  bool space = true;  // drops leading spaces
  auto put = [&](char c) {
    if (c == ' ') {
      if (space) return;
      space = true;
    } else {
      space = false;
    }
    out += c;
  };
  while (*p) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xA0) cp = ' ';
    if (cp >= 0x80) {
      const uint32_t base = utf8DecomposedBase(cp);
      cp = (base >= 0x20 && base < 0x7F) ? base : 0;
    }
    if (cp >= 0x20 && cp < 0x7F) put(static_cast<char>(cp));
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::string displayTitle(const std::string& in) {
  std::string t = displayText(in);
  return t.empty() ? "(untitled)" : t;
}

std::string shortDate(const std::string& ymd) {
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (ymd.size() < 10 || ymd[4] != '-' || ymd[7] != '-') return "";
  const int m = (ymd[5] - '0') * 10 + (ymd[6] - '0');
  const int d = (ymd[8] - '0') * 10 + (ymd[9] - '0');
  if (m < 1 || m > 12 || d < 1 || d > 31) return "";
  return std::string(kMonths[m - 1]) + " " + std::to_string(d);
}

}  // namespace tasks
