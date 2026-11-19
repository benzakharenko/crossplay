// Structural tests for TasksScreens: what each screen draws and what it makes
// tappable, at the real cuts' line heights (toybox_10/20/30 = 21/42/63).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/tasks/TasksScreens.h"

namespace fui = freeink::ui;
using namespace tasksui;

static int failures = 0;
#define CHECK(c)                                                                         \
  do {                                                                                   \
    if (!(c)) {                                                                          \
      std::fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #c); \
      ++failures;                                                                        \
    }                                                                                    \
  } while (0)

class Target final : public fui::DrawTarget {
 public:
  struct Run {
    fui::Rect rect;
    std::string text;
    fui::FontId font;
  };
  std::vector<Run> runs;
  std::vector<fui::Rect> strokes;
  // Real Toybox cuts average about 0.4 of their line height per character
  // (tools_local/forehead/measure.py: 'In progress' is 183px at toybox_20).
  int16_t cell(fui::FontId f) const { return static_cast<int16_t>(lineHeight(f) * 2 / 5); }
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(std::strlen(text ? text : "") * cell(font)), lineHeight(font)};
  }
  int16_t lineHeight(fui::FontId font) const override {
    if (font == fui::FONT_SLOT_SMALL) return 21;
    if (font == fui::FONT_SLOT_TITLE) return 63;
    return 42;
  }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xFF) override {}
  void stroke(fui::Rect r, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xFF) override { strokes.push_back(r); }
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect r, const char* t, fui::TextStyle s) override { runs.push_back({r, t ? t : "", s.font}); }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}

  bool drew(const std::string& s) const {
    for (const Run& r : runs)
      if (r.text == s) return true;
    return false;
  }
  bool drewContaining(const std::string& s) const {
    for (const Run& r : runs)
      if (r.text.find(s) != std::string::npos) return true;
    return false;
  }
};

static fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

struct Rendered {
  Target target;
  toybox::Interactions interactions;
  bool has(fui::ActionId a, int value = -1) const {
    for (size_t i = 0; i < interactions.count(); ++i) {
      const auto& it = interactions.data()[i];
      if (it.action == a && (value < 0 || it.value == value)) return true;
    }
    return false;
  }
  fui::ActionEvent tap(int x, int y) {
    fui::InputSnapshot in;
    in.touchReleased = true;
    in.touchX = static_cast<int16_t>(x);
    in.touchY = static_cast<int16_t>(y);
    return interactions.route(in);
  }
};

template <typename Build>
static void render(Rendered& out, Build build) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot none{};
  toybox::Frame frame(out.target, ctx, none, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  build(screen);
}

static const Row kRows[] = {
    {"Plan the week", "Personal - In progress", false, 0},
    {"Pick three goals", nullptr, false, 1},
    {"Review calendar", nullptr, false, 1},
    {"Gym", "Health - Not started", false, 0},
    {"A title that is long enough that it really must wrap onto a second line and then some more words", "Work",
     false, 0},
};

static ListModel listModel(const Row* rows, int count) {
  ListModel m;
  m.section = Today;
  m.counts[Missed] = 2;
  m.counts[Today] = count;
  m.counts[Tomorrow] = 3;
  m.counts[Done] = 1;
  m.rows = rows;
  m.count = count;
  m.status = "Synced 8:02 AM";
  return m;
}

static void testList() {
  Rendered r;
  const ListModel m = listModel(kRows, 5);
  render(r, [&](toybox::Screen& s) { buildList(s, m); });
  CHECK(r.target.drew("Plan the week"));
  CHECK(r.target.drew("Personal - In progress"));
  CHECK(r.target.drew("Synced 8:02 AM"));
  CHECK(r.has(ActionToggle, 0));
  CHECK(r.has(ActionOpen, 0));
  CHECK(r.has(ActionToggle, 2));  // sub-items are tappable too
  // The long title wrapped to two lines and ended in a drawn ellipsis.
  {
    Rendered one;
    const ListModel lm = listModel(&kRows[4], 1);
    render(one, [&](toybox::Screen& s) { buildList(s, lm); });
    CHECK(one.target.drewContaining("..."));
  }
  for (const auto& run : r.target.runs) {
    const bool fits = static_cast<int>(run.text.size()) * r.target.cell(run.font) <= run.rect.width + 1;
    if (!fits) std::fprintf(stderr, "  overflow: '%s' in %d px\n", run.text.c_str(), run.rect.width);
    CHECK(fits);  // nothing overflows
  }
  CHECK(r.interactions.count() <= toybox::kMaxInteractions);
  // Sub-item boxes sit further right than their parent's.
  int16_t parentX = -1, childX = -1;
  for (const auto& run : r.target.runs) {
    if (run.text == "Plan the week") parentX = run.rect.x;
    if (run.text == "Pick three goals") childX = run.rect.x;
  }
  CHECK(childX > parentX);
  CHECK(r.has(ActionSection, Missed));
  CHECK(r.has(ActionSection, Tomorrow));
  CHECK(r.has(ActionSection, Done));
}

static void testPaging() {
  std::vector<Row> many;
  char buf[40][32];
  for (int i = 0; i < 40; ++i) {
    snprintf(buf[i], sizeof(buf[i]), "Task number %d", i);
    many.push_back(Row{buf[i], "Home", false, 0});
  }
  const ListModel m = listModel(many.data(), 40);
  Target t;
  const std::vector<int> starts = listPageStarts(t, device(), m);
  CHECK(starts.size() > 2);
  CHECK(starts.front() == 0);
  // Each page fits within the interaction cap, two per row plus chrome.
  for (size_t p = 0; p + 1 < starts.size(); ++p) CHECK((starts[p + 1] - starts[p]) * 2 + 8 <= static_cast<int>(toybox::kMaxInteractions));
  // Rendering page 2 draws its first row and not page 1's.
  Rendered r;
  ListModel m2 = m;
  m2.firstVisible = starts[1];
  m2.pageLabel = "2 / 5";
  render(r, [&](toybox::Screen& s) { buildList(s, m2); });
  CHECK(r.target.drew(buf[starts[1]]));
  CHECK(!r.target.drew(buf[0]));
  CHECK(r.target.drew("2 / 5"));
  CHECK(r.has(ActionToggle, starts[1]));
}

// Page one draws nothing from page two: drawing and paging use one rule.
static void testPagesAgree() {
  std::vector<Row> rows;
  char buf[30][24];
  for (int i = 0; i < 30; ++i) {
    snprintf(buf[i], sizeof(buf[i]), "Item %d", i);
    rows.push_back(Row{buf[i], nullptr, false, 0});
  }
  const ListModel m = listModel(rows.data(), 30);
  Target t;
  const std::vector<int> starts = listPageStarts(t, device(), m);
  CHECK(starts.size() > 1);
  Rendered r;
  render(r, [&](toybox::Screen& s) { buildList(s, m); });
  if (starts.size() > 1) {
    CHECK(r.target.drew(buf[starts[1] - 1]));
    CHECK(!r.target.drew(buf[starts[1]]));
  }
}

static void testEmpty() {
  Rendered r;
  ListModel m = listModel(nullptr, 0);
  m.empty = "Nothing due today.";
  render(r, [&](toybox::Screen& s) { buildList(s, m); });
  CHECK(r.target.drew("Nothing due today."));
  CHECK(!r.has(ActionToggle));
}

static void testDetail() {
  const Field fields[] = {{"CATEGORY", "Personal"}, {"DUE", "Nov 19"}, {"STATUS", "In progress"}, {"PRIORITY", ""}};
  DetailModel m;
  m.title = "Plan the week";
  m.fields = fields;
  m.fieldCount = 4;
  m.notes = "Block out mornings for deep work.";
  m.subItems = "2 sub-items";
  Rendered r;
  render(r, [&](toybox::Screen& s) { buildDetail(s, m); });
  CHECK(r.target.drew("Plan the week"));
  CHECK(r.target.drew("CATEGORY"));
  CHECK(r.target.drew("In progress"));
  CHECK(r.target.drew("-"));  // empty priority shows a dash, not a hole
  CHECK(r.target.drewContaining("Block out mornings"));
  CHECK(r.has(ActionComplete));
  CHECK(!r.has(ActionRestore));
  CHECK(r.has(ActionBack));

  m.done = true;
  Rendered d;
  render(d, [&](toybox::Screen& s) { buildDetail(s, m); });
  CHECK(d.has(ActionRestore));
  CHECK(!d.has(ActionComplete));
}

static void testNotice() {
  NoticeModel m;
  m.prose = "Notion refused the token in tasks.json.";
  m.action = "TRY AGAIN";
  Rendered r;
  render(r, [&](toybox::Screen& s) { buildNotice(s, m); });
  CHECK(r.target.drewContaining("Notion refused"));
  CHECK(r.has(ActionRetry));
}

int main() {
  testList();
  testPaging();
  testPagesAgree();
  testEmpty();
  testDetail();
  testNotice();
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::printf("tasks screens: all tests passed\n");
  return 0;
}
