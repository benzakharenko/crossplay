#pragma once

// TASKS: today's Notion tasks, with missed and tomorrow's beside them, checked
// off on the device and written back to Notion.
//
// The split every app here uses: TasksCore/TasksState are the rules and the
// parsing (host-tests/tasks), TasksScreens lays out (host-tests/tasks too),
// TasksStore and TasksSync touch the card and the radio, and this file keeps
// the panel, the input and the order things happen in.
//
// When it talks to Notion:
//   - on opening, over Wi-Fi (the picker is raised if Wi-Fi is down, and
//     declining shows the list on the card, marked offline);
//   - when the sync icon is tapped;
//   - on the way out (Back or Home), to send the check-offs made this visit.
// Check-offs themselves never wait on the radio: each is saved to the card and
// shown at once. While the radio is busy the whole screen says so, since the
// list cannot take taps. Nothing in the background and no timer wake.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
// For the shelf's row, which names icon_check_32 next to TasksActivity::create.
#include "../../components/icons/listIcons.h"
#include "../ui/ToyboxScreen.h"
#include "TasksCore.h"
#include "TasksScreens.h"
#include "TasksState.h"
#include "TasksSync.h"

class TasksActivity final : public Activity {
 public:
  TasksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Tasks", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return syncing_ || syncPending_; }
  bool handleHomeGesture() override;

 private:
  enum class View : uint8_t { List, Detail, Notice };

  // --- Data ---------------------------------------------------------------
  void loadAll();
  void rebuild();  // state -> view -> rows, labels, page
  void setChecked(const std::string& id, bool done);
  const tasks::Task* findTask(const std::string& id) const;

  // --- Sync ---------------------------------------------------------------
  enum class Leave : uint8_t { None, Shelf, Home };
  void askSync();     // raises the Wi-Fi picker when needed
  void runSync();     // holds the loop until done or abandoned
  void runSendAndLeave();
  void leave(Leave where);  // sends queued changes first when Wi-Fi is up
  bool pollDuringSync();

  // --- Navigation ---------------------------------------------------------
  void showSection(tasksui::Section section);
  void openDetail(int row);
  void back();
  void page(bool down);
  void showNotice(const std::string& prose, bool retry);
  std::string statusLine() const;

  tasks::Config cfg_;
  bool haveConfig_ = false;
  tasks::State state_;
  tasks::View view_;
  std::string today_;

  View screen_ = View::List;
  tasksui::Section section_ = tasksui::Today;
  std::vector<tasksui::Row> rows_;
  std::vector<std::string> rowIds_;  // empty for section labels
  std::vector<std::string> titles_, metas_;
  int counts_[4] = {0, 0, 0, 0};
  int top_ = 0;
  std::string pageLabel_;

  std::string detailId_;
  std::string notice_;
  bool noticeRetry_ = false;

  bool syncing_ = false;
  bool syncPending_ = false;        // run on the next loop pass
  bool syncOnOpen_ = true;
  Leave leaving_ = Leave::None;  // set while the queue goes out on the way out
  bool offline_ = false;
  std::string lastError_, lastWarning_;
  bool aborted_ = false;
  bool homeDuringSync_ = false;  // Home pressed while the radio held the loop

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
