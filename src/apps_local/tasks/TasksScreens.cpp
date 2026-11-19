#include "TasksScreens.h"

#include <cstdio>
#include <string>
#include <vector>

#include "../ui/ToyboxText.h"

namespace tasksui {
namespace {

constexpr int16_t kBoxSide = 40;
constexpr int16_t kBoxGap = 16;
constexpr int16_t kIndent = 36;        // per level of sub-item
constexpr int16_t kRowPad = 12;        // above and below a row's text
constexpr int16_t kMinRow = 72;        // a finger, with room to miss
constexpr int16_t kHeaderRow = 44;     // variant 2's section label
constexpr int16_t kTabHeight = 56;     // variant 1's tabs, variant 3's strips
constexpr int16_t kStrike = 2;
constexpr int kTitleLines = 2;
constexpr int16_t kFooterHeight = toybox::kPillHeight;

fui::TextStyle plain(const fui::FontId font, const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = fui::Color::Black;
  style.maxLines = 1;
  return style;
}

int16_t pageWidth(const fui::DeviceContext& device) { return static_cast<int16_t>(device.width - 2 * toybox::kMargin); }

// Header band with the app's title and, on the list, the sync control.
void chrome(toybox::Screen& screen, const char* title, const freeink::Icon* trailing, fui::ActionId trailingAction) {
  fui::TextStyle titleStyle = screen.theme().titleText;
  titleStyle.font = toybox::kDisplayFont;
  fui::HeaderProps header;
  header.title = title;
  header.titleText = titleStyle;
  header.borderEdges = fui::EdgesNone;
  if (trailing != nullptr) {
    header.trailingIcon = fui::bitmapFromIcon(*trailing);
    header.trailingAction = trailingAction;
    header.trailingStyles = toybox::rowStyles();
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// THE line-break rule of this file: whole words, at most maxLines, "..." on
// the last line when anything was left over, and a word wider than the line
// cut at a character boundary. Every row is sized and drawn from this.
std::vector<std::string> breakLines(const fui::DrawTarget& target, const std::string& text, const int16_t width,
                                    const int maxLines, const fui::TextStyle& style) {
  std::vector<std::string> out;
  const auto fits = [&](const std::string& run) {
    if (run.size() > 160) return false;
    const int16_t w = target.measureText(style.font, run.c_str(), style).width;
    return w >= 0 && w <= width;
  };
  std::vector<std::string> words;
  for (size_t i = 0; i <= text.size();) {
    size_t end = text.find(' ', i);
    if (end == std::string::npos) end = text.size();
    if (end > i) words.push_back(text.substr(i, end - i));
    i = end + 1;
  }
  std::string line;
  bool overflow = false;
  for (size_t w = 0; w < words.size(); ++w) {
    std::string word = words[w];
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (fits(candidate)) {
      line = candidate;
      continue;
    }
    if (!line.empty()) {
      out.push_back(line);
      line.clear();
      if (static_cast<int>(out.size()) == maxLines) {
        overflow = true;
        break;
      }
    }
    while (word.size() > 1 && !fits(word)) {  // one word wider than the line: cut it
      size_t n = word.size() - 1;
      while (n > 1 && (!fits(word.substr(0, n)) || (static_cast<unsigned char>(word[n]) & 0xC0) == 0x80)) --n;
      out.push_back(word.substr(0, n));
      word = word.substr(n);
      if (static_cast<int>(out.size()) == maxLines) {
        overflow = true;
        break;
      }
    }
    if (overflow) break;
    line = word;
  }
  if (!overflow && !line.empty()) out.push_back(line);
  if (out.empty()) out.push_back(std::string());
  if (overflow) {
    std::string& last = out.back();
    while (!last.empty() && !fits(last + "...")) {
      const size_t sp = last.find_last_of(' ');
      last.erase(sp != std::string::npos && sp > 0 ? sp : last.size() - 1);
    }
    last += "...";
  }
  return out;
}

// --- Rows -----------------------------------------------------------------

struct RowPlan {
  std::vector<std::string> lines;  // title lines
  std::string meta;
  int16_t height = 0;
};

int16_t textWidth(const fui::DeviceContext& device, const Row& row) {
  return static_cast<int16_t>(pageWidth(device) - kBoxSide - kBoxGap - kIndent * (row.depth > 2 ? 2 : row.depth));
}

RowPlan planRow(const fui::DrawTarget& target, const fui::DeviceContext& device, const Row& row) {
  RowPlan plan;
  if (row.header) {
    plan.lines.push_back(row.title);
    plan.height = kHeaderRow;
    return plan;
  }
  const fui::TextStyle title = plain(toybox::kBodyFont);
  const fui::TextStyle small = plain(toybox::kTileFont);
  const int16_t width = textWidth(device, row);
  plan.lines = breakLines(target, row.title, width, kTitleLines, title);
  int16_t h = static_cast<int16_t>(kRowPad * 2 + target.lineHeight(title.font) * static_cast<int>(plan.lines.size()));
  if (row.meta != nullptr && *row.meta) {
    plan.meta = breakLines(target, row.meta, width, 1, small).front();
    h = static_cast<int16_t>(h + target.lineHeight(small.font));
  }
  plan.height = h < kMinRow ? kMinRow : h;
  return plan;
}

void tickBox(toybox::Screen& screen, const fui::Rect& box, const bool checked) {
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kRule, 4);
  if (!checked) return;
  const int16_t inset = 9;
  screen.target().fill(
      fui::makeRect(static_cast<int16_t>(box.x + inset), static_cast<int16_t>(box.y + inset),
                    static_cast<int16_t>(box.width - 2 * inset), static_cast<int16_t>(box.height - 2 * inset)),
      fui::Paint::solid(fui::Color::Black), 2);
}

void hit(toybox::Screen& screen, const fui::Rect& rect, const fui::ActionId action, const int value) {
  fui::ButtonProps b;
  b.label = "";
  b.action = action;
  b.value = static_cast<int16_t>(value);
  b.styles = toybox::rowStyles();
  b.styles.normal.background = fui::Paint::none();
  b.styles.normal.border = fui::Paint::none();
  screen.button(b, rect);
}

void drawRow(toybox::Screen& screen, const fui::Rect& box, const Row& row, const RowPlan& plan, const int index,
             const bool rule) {
  fui::DrawTarget& t = screen.target();
  if (row.header) {
    const fui::TextStyle small = plain(toybox::kTileFont);
    const int16_t lh = t.lineHeight(small.font);
    t.text(fui::makeRect(box.x, static_cast<int16_t>(box.y + box.height - lh - 6), box.width, lh), row.title, small);
    t.fill(fui::makeRect(box.x, static_cast<int16_t>(box.y + box.height - 2), box.width, 2),
           fui::Paint::solid(fui::Color::Black));
    return;
  }
  const int16_t indent = static_cast<int16_t>(kIndent * (row.depth > 2 ? 2 : row.depth));
  const fui::TextStyle title = plain(toybox::kBodyFont);
  const fui::TextStyle small = plain(toybox::kTileFont);
  const int16_t lh = t.lineHeight(title.font);
  const int16_t boxX = static_cast<int16_t>(box.x + indent);
  const fui::Rect tick = fui::makeRect(boxX, static_cast<int16_t>(box.y + kRowPad + (lh - kBoxSide) / 2), kBoxSide,
                                       kBoxSide);
  tickBox(screen, tick, row.done);
  const int16_t textX = static_cast<int16_t>(boxX + kBoxSide + kBoxGap);
  const int16_t width = static_cast<int16_t>(box.x + box.width - textX);
  int16_t y = static_cast<int16_t>(box.y + kRowPad);
  for (const std::string& line : plan.lines) {
    t.text(fui::makeRect(textX, y, width, lh), line.c_str(), title);
    if (row.done && !line.empty()) {
      const int16_t w = t.measureText(title.font, line.c_str(), title).width;
      t.fill(fui::makeRect(textX, static_cast<int16_t>(y + lh / 2), w, kStrike), fui::Paint::solid(fui::Color::Black));
    }
    y = static_cast<int16_t>(y + lh);
  }
  if (!plan.meta.empty()) {
    t.text(fui::makeRect(textX, y, width, t.lineHeight(small.font)), plan.meta.c_str(), small);
  }
  if (rule) {
    t.fill(fui::makeRect(box.x, static_cast<int16_t>(box.y + box.height - toybox::kHairline), box.width,
                         toybox::kHairline),
           fui::Paint::solid(fui::Color::Black));
  }
  // Box and text are separate targets: the box checks off, the text opens.
  const fui::Rect boxHit = fui::makeRect(box.x, box.y, static_cast<int16_t>(textX - box.x - kBoxGap / 2), box.height);
  hit(screen, boxHit, ActionToggle, index);
  hit(screen, fui::makeRect(static_cast<int16_t>(boxHit.x + boxHit.width), box.y,
                            static_cast<int16_t>(box.width - boxHit.width), box.height),
      ActionOpen, index);
}

// --- Bands ----------------------------------------------------------------

fui::Rect footerBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       pageWidth(device), kFooterHeight);
}

bool hasTopStrip(const ListModel& m) {
#if TASKS_VARIANT == 1
  (void)m;
  return true;  // the tabs
#elif TASKS_VARIANT == 3
  return m.section == Today && m.counts[Missed] > 0;
#else
  (void)m;
  return false;
#endif
}

bool hasBottomStrip(const ListModel& m) {
#if TASKS_VARIANT == 3
  return m.section == Today && m.counts[Tomorrow] > 0;
#else
  (void)m;
  return false;
#endif
}

// The rows' band: under the chrome (and the top strip), above the bottom
// strip, the page label's line and the footer.
fui::Rect rowBand(const fui::DrawTarget& target, const fui::DeviceContext& device, const ListModel& m) {
  int16_t top = toybox::kBodyTop;
  if (hasTopStrip(m)) top = static_cast<int16_t>(top + kTabHeight + toybox::kGutter);
  int16_t bottom = static_cast<int16_t>(footerBand(device).y - toybox::kGutter);
  bottom = static_cast<int16_t>(bottom - target.lineHeight(toybox::kTileFont));  // page label
  if (hasBottomStrip(m)) bottom = static_cast<int16_t>(bottom - kTabHeight - toybox::kGutter);
  return fui::makeRect(toybox::kMargin, top, pageWidth(device), static_cast<int16_t>(bottom - top));
}

// Labels on strips, tabs and the footer's DONE are set in the small cut: four
// tabs share 448px, and "TOMORROW 3" at the body cut needs 170 of them.
void stripButton(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                 const int value, const bool selected) {
  fui::ButtonProps b;
  b.label = label;
  b.text = toybox::buttonText(screen.theme());
  b.action = action;
  b.value = static_cast<int16_t>(value);
  b.styles = selected ? toybox::invertedStyles() : toybox::rowStyles();
  screen.button(b, box);
}

#if TASKS_VARIANT == 1
void tabs(toybox::Screen& screen, const fui::DeviceContext& device, const ListModel& m) {
  static constexpr const char* kNames[] = {"MISSED", "TODAY", "TMRW", "DONE"};
  const int16_t w = static_cast<int16_t>((pageWidth(device) - 3 * 6) / 4);
  for (int i = 0; i < 4; ++i) {
    char label[24];
    snprintf(label, sizeof(label), "%s %d", kNames[i], m.counts[i]);
    stripButton(screen,
                fui::makeRect(static_cast<int16_t>(toybox::kMargin + i * (w + 6)), toybox::kBodyTop, w, kTabHeight),
                label, ActionSection, i, m.section == i);
  }
}
#endif

void footer(toybox::Screen& screen, const fui::DeviceContext& device, const ListModel& m, const bool doneButton) {
  const fui::Rect band = footerBand(device);
  fui::DrawTarget& t = screen.target();
  const fui::TextStyle small = plain(toybox::kTileFont);
  int16_t textWidth = band.width;
  if (doneButton) {
    const int16_t bw = 150;
    char label[24];
    snprintf(label, sizeof(label), "DONE %d", m.counts[Done]);
    stripButton(screen, fui::makeRect(static_cast<int16_t>(band.x + band.width - bw), band.y, bw, band.height), label,
                ActionSection, Done, false);
    textWidth = static_cast<int16_t>(band.width - bw - toybox::kGutter);
  }
  const int16_t lh = t.lineHeight(small.font);
  const std::string status = breakLines(t, m.status ? m.status : "", textWidth, 1, small).front();
  t.text(fui::makeRect(band.x, static_cast<int16_t>(band.y + (band.height - lh) / 2), textWidth, lh), status.c_str(),
         small);
}

const char* listTitle(const ListModel& m) {
#if TASKS_VARIANT == 1
  (void)m;
  return "TASKS";
#else
  switch (m.section) {
    case Missed:
      return "MISSED";
    case Tomorrow:
      return "TOMORROW";
    case Done:
      return "DONE";
    case Today:
    default:
      return "TASKS";
  }
#endif
}

}  // namespace

std::vector<int> listPageStarts(const fui::DrawTarget& target, const fui::DeviceContext& device,
                                const ListModel& model) {
  std::vector<int> starts{0};
  const fui::Rect band = rowBand(target, device, model);
  int16_t used = 0;
  for (int i = 0; i < model.count; ++i) {
    const int16_t h = planRow(target, device, model.rows[i]).height;
    // A section label never ends a page: it moves to the next with its rows.
    const bool orphanHeader = model.rows[i].header && i + 1 < model.count &&
                              used + h + planRow(target, device, model.rows[i + 1]).height > band.height;
    if (used > 0 && (used + h > band.height || orphanHeader)) {
      starts.push_back(i);
      used = 0;
    }
    used = static_cast<int16_t>(used + h);
  }
  return starts;
}

void buildList(toybox::Screen& screen, const ListModel& m) {
  const fui::DeviceContext& device = screen.frame().device();
  chrome(screen, listTitle(m), m.syncIcon, ActionSync);
  fui::DrawTarget& t = screen.target();

#if TASKS_VARIANT == 1
  tabs(screen, device, m);
  const bool doneButton = false;
#else
  const bool doneButton = m.section != Done;
#endif
#if TASKS_VARIANT == 3
  if (hasTopStrip(m)) {
    char label[32];
    snprintf(label, sizeof(label), "%d MISSED  >", m.counts[Missed]);
    stripButton(screen, fui::makeRect(toybox::kMargin, toybox::kBodyTop, pageWidth(device), kTabHeight), label,
                ActionSection, Missed, true);
  }
#endif

  const fui::Rect band = rowBand(t, device, m);
  if (m.count == 0) {
    const fui::TextStyle body = plain(toybox::kBodyFont, fui::TextAlign::Center);
    const int16_t lh = t.lineHeight(body.font);
    t.text(fui::makeRect(band.x, static_cast<int16_t>(band.y + band.height / 3), band.width, lh), m.empty, body);
  }
  int16_t y = band.y;
  for (int i = m.firstVisible; i < m.count; ++i) {
    const RowPlan plan = planRow(t, device, m.rows[i]);
    if (y + plan.height > band.y + band.height && i > m.firstVisible) break;
    const bool last = i + 1 >= m.count || y + plan.height + planRow(t, device, m.rows[i + 1]).height >
                                              band.y + band.height;
    const bool nextIsHeader = i + 1 < m.count && m.rows[i + 1].header;
    drawRow(screen, fui::makeRect(band.x, y, band.width, plan.height), m.rows[i], plan, i, !last && !nextIsHeader);
    y = static_cast<int16_t>(y + plan.height);
  }

  if (m.pageLabel != nullptr) {
    const fui::TextStyle small = plain(toybox::kTileFont, fui::TextAlign::Center);
    const int16_t lh = t.lineHeight(small.font);
    t.text(fui::makeRect(band.x, static_cast<int16_t>(band.y + band.height), band.width, lh), m.pageLabel, small);
  }

#if TASKS_VARIANT == 3
  if (hasBottomStrip(m)) {
    char label[32];
    snprintf(label, sizeof(label), "TOMORROW %d  >", m.counts[Tomorrow]);
    const int16_t lh = t.lineHeight(toybox::kTileFont);
    stripButton(screen,
                fui::makeRect(toybox::kMargin,
                              static_cast<int16_t>(band.y + band.height + lh + toybox::kGutter / 2), pageWidth(device),
                              kTabHeight),
                label, ActionSection, Tomorrow, false);
  }
#endif
  footer(screen, device, m, doneButton);
}

// --- Detail ----------------------------------------------------------------

void buildDetail(toybox::Screen& screen, const DetailModel& m) {
  const fui::DeviceContext& device = screen.frame().device();
  chrome(screen, "TASK", nullptr, fui::NO_ACTION);
  fui::DrawTarget& t = screen.target();
  const fui::Rect footer = footerBand(device);
  const int16_t width = pageWidth(device);
  const fui::TextStyle big = plain(toybox::kBodyFont);
  const fui::TextStyle small = plain(toybox::kTileFont);
  const int16_t bigH = t.lineHeight(big.font);
  const int16_t smallH = t.lineHeight(small.font);
  int16_t y = toybox::kBodyTop;
  const int16_t limit = static_cast<int16_t>(footer.y - toybox::kGutter);

  for (const std::string& line : breakLines(t, m.title, width, 3, big)) {
    t.text(fui::makeRect(toybox::kMargin, y, width, bigH), line.c_str(), big);
    if (m.done && !line.empty()) {
      t.fill(fui::makeRect(toybox::kMargin, static_cast<int16_t>(y + bigH / 2),
                           t.measureText(big.font, line.c_str(), big).width, kStrike),
             fui::Paint::solid(fui::Color::Black));
    }
    y = static_cast<int16_t>(y + bigH);
  }
  y = static_cast<int16_t>(y + toybox::kGutter);
  t.fill(fui::makeRect(toybox::kMargin, y, width, toybox::kHairline), fui::Paint::solid(fui::Color::Black));
  y = static_cast<int16_t>(y + toybox::kGutter);

  // Label over value, two columns, so four fields take two rows.
  const int16_t colW = static_cast<int16_t>((width - toybox::kGutter) / 2);
  for (int i = 0; i < m.fieldCount; ++i) {
    const int16_t x = static_cast<int16_t>(toybox::kMargin + (i % 2) * (colW + toybox::kGutter));
    const int16_t rowY = static_cast<int16_t>(y + (i / 2) * (smallH + bigH + toybox::kGutter));
    t.text(fui::makeRect(x, rowY, colW, smallH), m.fields[i].label, small);
    const std::string value = breakLines(t, m.fields[i].value[0] ? m.fields[i].value : "-", colW, 1, big).front();
    t.text(fui::makeRect(x, static_cast<int16_t>(rowY + smallH), colW, bigH), value.c_str(), big);
  }
  y = static_cast<int16_t>(y + ((m.fieldCount + 1) / 2) * (smallH + bigH + toybox::kGutter));

  if (m.subItems != nullptr && *m.subItems) {
    t.text(fui::makeRect(toybox::kMargin, y, width, smallH), m.subItems, small);
    y = static_cast<int16_t>(y + smallH + toybox::kGutter);
  }
  if (m.notes != nullptr && *m.notes) {
    t.text(fui::makeRect(toybox::kMargin, y, width, smallH), "NOTES", small);
    y = static_cast<int16_t>(y + smallH);
    const int lines = (limit - y) / bigH;
    if (lines > 0) {
      for (const std::string& line : breakLines(t, m.notes, width, lines, big)) {
        t.text(fui::makeRect(toybox::kMargin, y, width, bigH), line.c_str(), big);
        y = static_cast<int16_t>(y + bigH);
      }
    }
  }

  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  stripButton(screen, fui::makeRect(footer.x, footer.y, half, footer.height), "BACK", ActionBack, 0, false);
  fui::ButtonProps main;
  main.label = m.done ? "RESTORE" : "COMPLETE";
  main.action = m.done ? ActionRestore : ActionComplete;
  screen.button(main, fui::makeRect(static_cast<int16_t>(footer.x + half + toybox::kGutter), footer.y, half,
                                    footer.height));
}

// --- Notice ----------------------------------------------------------------

void buildNotice(toybox::Screen& screen, const NoticeModel& m) {
  const fui::DeviceContext& device = screen.frame().device();
  chrome(screen, m.title, nullptr, fui::NO_ACTION);
  fui::DrawTarget& t = screen.target();
  const fui::Rect footer = footerBand(device);
  const int16_t width = pageWidth(device);
  const fui::TextStyle body = plain(toybox::kBodyFont);
  const int16_t lh = t.lineHeight(body.font);
  const int lines = (footer.y - toybox::kGutter - toybox::kBodyTop) / lh;
  int16_t y = toybox::kBodyTop;
  for (const std::string& line : breakLines(t, m.prose, width, lines > 0 ? lines : 1, body)) {
    t.text(fui::makeRect(toybox::kMargin, y, width, lh), line.c_str(), body);
    y = static_cast<int16_t>(y + lh);
  }
  if (m.action != nullptr) {
    fui::ButtonProps b;
    b.label = m.action;
    b.action = ActionRetry;
    screen.button(b, footer);
  }
}

}  // namespace tasksui
