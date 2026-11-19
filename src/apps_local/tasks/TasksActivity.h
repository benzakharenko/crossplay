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
//   - a few seconds after the last check-off, so a run of taps is one sync;
//   - when the sync icon is tapped.
// Nothing in the background and no timer wake.

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

 private:
  enum class View : uint8_t { List, Detail, Notice };

  // --- Data ---------------------------------------------------------------
  void loadAll();
  void rebuild();  // state -> view -> rows, labels, page
  void setChecked(const std::string& id, bool done);
  const tasks::Task* findTask(const std::string& id) const;

  // --- Sync ---------------------------------------------------------------
  void askSync();     // raises the Wi-Fi picker when needed
  void runSync();     // holds the loop until done or abandoned
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
  unsigned long lastChangeMs_ = 0;  // for the debounced sync after taps
  bool offline_ = false;
  std::string lastError_, lastWarning_;
  bool aborted_ = false;

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
