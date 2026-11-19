#pragma once

// The Tasks screens. Freestanding builders in the NotesScreens mould: a model
// in, a drawn frame out, no renderer and no Activity, so host-tests/tasks can
// assert what they drew and what they made tappable.
//
// Rules carried over from Notes, because they were paid for there:
//   - nothing is elided by the renderer: every line drawn is a line chosen
//     here, and a title too long for its two lines ends in "..." we drew;
//   - one row height rule, shared by drawing and paging, so the page label,
//     the side keys and the drawn rows cannot disagree;
//   - the whole row is a tap target (a 40px box alone is a miss waiting to
//     happen), split in two: the box checks off, the rest opens the task.
//
// The list leads with TODAY. MISSED and TOMORROW are strips above and below
// it, each opening its own list, and DONE (the bin a check-off lands in) sits
// in the footer. Chosen over tabs and over one stacked agenda, rendered side by
// side: today keeps the most room, and a missed task is the first thing on
// the screen.

#include <cstdint>
#include <vector>

#include "../ui/ToyboxScreen.h"

namespace tasksui {

namespace fui = freeink::ui;

// Notes has the 340s and 350s; Tasks takes the 370s.
enum : fui::ActionId {
  ActionToggle = 370,  // value: row index
  ActionOpen = 371,    // value: row index
  ActionSection = 372, // value: Section
  ActionSync = 373,
  ActionComplete = 374,
  ActionRestore = 375,
  ActionBack = 376,
  ActionRetry = 377,
};

enum Section : int16_t { Missed = 0, Today = 1, Tomorrow = 2, Done = 3 };

struct Row {
  const char* title = "";
  const char* meta = nullptr;  // "Work - In progress - Nov 15", small, under the title
  bool done = false;
  int depth = 0;               // sub-item nesting, indents the row
};

struct ListModel {
  Section section = Today;  // the list shown
  int counts[4] = {0, 0, 0, 0};
  const Row* rows = nullptr;
  int count = 0;
  int firstVisible = 0;
  const char* pageLabel = nullptr;  // "1 / 2" when paged
  const char* status = "";          // "Synced 8:02 AM", "Syncing...", ...
  const char* empty = "Nothing due.";
  const freeink::Icon* syncIcon = nullptr;
};

void buildList(toybox::Screen& screen, const ListModel& model);
// Index of the first row on each page, from the same layout pass that draws.
std::vector<int> listPageStarts(const fui::DrawTarget& target, const fui::DeviceContext& device,
                                const ListModel& model);

struct Field {
  const char* label = "";
  const char* value = "";
};

struct DetailModel {
  const char* title = "";
  const Field* fields = nullptr;  // CATEGORY, DUE, STATUS, PRIORITY
  int fieldCount = 0;
  const char* notes = nullptr;     // already display-safe; null or empty = none
  const char* subItems = nullptr;  // "2 sub-items, 1 done", or null
  bool done = false;               // footer offers RESTORE instead of COMPLETE
};

void buildDetail(toybox::Screen& screen, const DetailModel& model);

struct NoticeModel {
  const char* title = "TASKS";
  const char* prose = "";
  const char* action = nullptr;  // "TRY AGAIN"; null = Back only
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace tasksui
