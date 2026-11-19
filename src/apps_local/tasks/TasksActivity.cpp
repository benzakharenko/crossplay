#include "TasksActivity.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/icons/listIcons.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "TasksStore.h"
#include "TasksText.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kTag = "TASKS";
// After Wi-Fi comes up the clock is set by SNTP a moment later; wait this long
// for it before giving up on knowing what "today" is.
constexpr unsigned long kClockWaitMs = 8000;

std::string localDate(const time_t t, struct tm& parts) {
  localtime_r(&t, &parts);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
  return buf;
}

}  // namespace

std::unique_ptr<Activity> TasksActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TasksActivity>(renderer, mappedInput);
}

// --- Lifecycle ---------------------------------------------------------------

void TasksActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  loadAll();
  requestUpdate();
}

void TasksActivity::onExit() {
  // A check-off made in the last few seconds stays queued on the card and goes
  // to Notion on the next visit; leaving never waits on the radio.
  if (!state_.queue.empty()) tasks::store::saveState(state_);
  Activity::onExit();
}

void TasksActivity::loadAll() {
  std::string err;
  switch (tasks::store::loadConfig(cfg_, err)) {
    case tasks::store::ConfigStatus::Missing:
      haveConfig_ = false;
      syncOnOpen_ = false;
      showNotice(
          "Put tasks.json on the card to connect Notion. Upload it with File Transfer; the app tucks it away when it "
          "opens.",
          false);
      return;
    case tasks::store::ConfigStatus::Invalid:
      haveConfig_ = false;
      syncOnOpen_ = false;
      showNotice(err, false);
      return;
    case tasks::store::ConfigStatus::Ok:
      break;
  }
  haveConfig_ = true;
  cfg_.priorityOrder = cfg_.priorityOrder.empty() ? std::vector<std::string>{"High", "Medium", "Low"}
                                                  : cfg_.priorityOrder;
  state_ = tasks::store::loadState();
  rebuild();
}

// --- Data ----------------------------------------------------------------------

const tasks::Task* TasksActivity::findTask(const std::string& id) const {
  for (const auto* list : {&view_.missed, &view_.today, &view_.tomorrow, &view_.completed})
    for (const tasks::Row& r : *list)
      if (r.task.id == id) return &r.task;
  return nullptr;
}

void TasksActivity::rebuild() {
  today_ = tasks::store::today();
  // Without a clock there is no "today"; the cached list is still shown,
  // sorted as of the day it was read.
  struct tm parts;
  const std::string day =
      today_.empty() && state_.syncedAt > 0 ? localDate(static_cast<time_t>(state_.syncedAt), parts) : today_;
  view_ = tasks::buildView(tasks::effectiveTasks(state_, cfg_, day), cfg_, day);
  counts_[tasksui::Missed] = static_cast<int>(view_.missed.size());
  counts_[tasksui::Today] = static_cast<int>(view_.today.size());
  counts_[tasksui::Tomorrow] = static_cast<int>(view_.tomorrow.size());
  counts_[tasksui::Done] = static_cast<int>(view_.completed.size());

  rows_.clear();
  rowIds_.clear();
  titles_.clear();
  metas_.clear();
  const std::vector<tasks::Row>* list = &view_.today;
  switch (section_) {
    case tasksui::Missed:
      list = &view_.missed;
      break;
    case tasksui::Tomorrow:
      list = &view_.tomorrow;
      break;
    case tasksui::Done:
      list = &view_.completed;
      break;
    case tasksui::Today:
      break;
  }
  const bool showDate = list == &view_.missed || list == &view_.completed;
  titles_.reserve(list->size());
  metas_.reserve(list->size());
  rowIds_.reserve(list->size());
  rows_.reserve(list->size());
  for (const tasks::Row& r : *list) {
    titles_.push_back(tasks::displayTitle(r.task.title));
    std::string meta = tasks::displayText(r.task.category);
    const std::string status = tasks::displayText(r.task.status);
    if (!status.empty() && cfg_.statusKind != tasks::StatusKind::Checkbox) meta += (meta.empty() ? "" : " - ") + status;
    if (showDate && r.depth == 0 && !r.task.due.empty()) meta += (meta.empty() ? "" : " - ") + tasks::shortDate(r.task.due);
    metas_.push_back(std::move(meta));
    rowIds_.push_back(r.task.id);
  }
  // Rows point into titles_/metas_, which are not resized past this point.
  for (size_t k = 0; k < list->size(); ++k) {
    tasksui::Row row;
    row.title = titles_[k].c_str();
    row.meta = metas_[k].empty() ? nullptr : metas_[k].c_str();
    row.done = (*list)[k].task.done;
    row.depth = (*list)[k].depth;
    rows_.push_back(row);
  }

  // Keep the page in range after rows come and go.
  if (top_ >= static_cast<int>(rows_.size())) top_ = 0;
  pageLabel_.clear();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  tasksui::ListModel probe;
  probe.section = section_;
  for (int i = 0; i < 4; ++i) probe.counts[i] = counts_[i];
  probe.rows = rows_.data();
  probe.count = static_cast<int>(rows_.size());
  const std::vector<int> starts = tasksui::listPageStarts(target, target.deviceContext(), probe);
  int pageIndex = 0;
  for (size_t i = 0; i < starts.size(); ++i)
    if (starts[i] <= top_) pageIndex = static_cast<int>(i);
  top_ = starts[static_cast<size_t>(pageIndex)];
  if (starts.size() > 1) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", pageIndex + 1, static_cast<int>(starts.size()));
    pageLabel_ = buf;
  }
}

void TasksActivity::setChecked(const std::string& id, const bool done) {
  if (id.empty()) return;
  const std::string day = today_.empty() ? tasks::store::today() : today_;
  tasks::markDone(state_, cfg_, id, done, day.empty() ? "1970-01-01" : day);
  tasks::store::saveState(state_);
  rebuild();
  interactionsReady_ = false;
  requestUpdate();
}

// --- Sync ------------------------------------------------------------------------

void TasksActivity::askSync() {
  if (!haveConfig_ || syncing_) return;
  bool connected = WiFi.status() == WL_CONNECTED;
#if defined(SIMULATOR)
  // Canned replies need no network, so previews skip the Wi-Fi picker.
  if (std::getenv("TASKS_FAKE_DIR") != nullptr) connected = true;
#endif
  if (connected) {
    syncPending_ = true;
    requestUpdate();
    return;
  }
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             offline_ = true;
                             interactionsReady_ = false;
                             requestUpdate();
                             return;
                           }
                           offline_ = false;
                           syncPending_ = true;
                           requestUpdate();
                         });
}

bool TasksActivity::pollDuringSync() {
  mappedInput.update();
  if (mappedInput.wasHomeGesture() || (gpio.hasHomeKey() && gpio.wasHomeKeyPressed())) {
    aborted_ = true;
    homeDuringSync_ = true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) aborted_ = true;
  return aborted_;
}

void TasksActivity::runSync() {
  syncing_ = true;
  aborted_ = false;
  requestUpdateAndWait();  // "Syncing..." is on the glass before the radio is busy

  // SNTP sets the clock a moment after Wi-Fi joins; give it that moment.
  std::string day = tasks::store::today();
  const unsigned long started = millis();
  while (day.empty() && millis() - started < kClockWaitMs && !pollDuringSync()) {
    delay(200);
    day = tasks::store::today();
  }

  tasks::TasksSync sync(cfg_);
  const tasks::SyncOutcome o = sync.sync(state_, day, [this] { return pollDuringSync(); });
  LOG_INF(kTag, "sync: %s, %d change(s) sent, %u tasks", tasks::syncResultName(o.result), o.sentOps,
          static_cast<unsigned>(state_.tasks.size()));
  tasks::store::saveState(state_);
  syncing_ = false;
  lastWarning_ = o.warning;
  switch (o.result) {
    case tasks::SyncResult::Ok:
      lastError_.clear();
      offline_ = false;
      break;
    case tasks::SyncResult::Aborted:
      break;
    case tasks::SyncResult::Unreachable:
      offline_ = true;
      lastError_ = o.message;
      break;
    case tasks::SyncResult::BadToken:
    case tasks::SyncResult::NotShared:
      // Nothing works until the settings change: say so full-screen.
      lastError_ = o.message;
      if (state_.tasks.empty()) {
        showNotice(o.message, true);
        return;
      }
      break;
    default:
      lastError_ = o.message;
      // With nothing on the card to show, the error is the screen: a two-line
      // footer would cut a column list in half.
      if (state_.tasks.empty() && o.result == tasks::SyncResult::Failed) {
        showNotice(o.message, true);
        return;
      }
      break;
  }
  rebuild();
  interactionsReady_ = false;
  requestUpdate();
}

std::string TasksActivity::statusLine() const {
  if (syncing_) return "Syncing...";
  if (!lastWarning_.empty()) return lastWarning_;
  const size_t waiting = state_.queue.size();
  char buf[64];
  if (!lastError_.empty()) {
    if (waiting > 0) {
      snprintf(buf, sizeof(buf), "%u change%s waiting. ", static_cast<unsigned>(waiting), waiting == 1 ? "" : "s");
      return buf + lastError_;
    }
    return lastError_;
  }
  if (waiting > 0) {
    snprintf(buf, sizeof(buf), "%u change%s to send", static_cast<unsigned>(waiting), waiting == 1 ? "" : "s");
    return buf;
  }
  if (state_.syncedAt <= 0) return offline_ ? "Offline. Tap sync to connect." : "Not synced yet";
  struct tm parts;
  const std::string syncedDay = localDate(static_cast<time_t>(state_.syncedAt), parts);
  int hour = parts.tm_hour % 12;
  if (hour == 0) hour = 12;
  std::string out = offline_ ? "Offline. Synced " : "Synced ";
  if (syncedDay != today_) out += tasks::shortDate(syncedDay) + " ";
  snprintf(buf, sizeof(buf), "%d:%02d %s", hour, parts.tm_min, parts.tm_hour < 12 ? "AM" : "PM");
  return out + buf;
}

// --- Navigation ------------------------------------------------------------------

void TasksActivity::showNotice(const std::string& prose, const bool retry) {
  notice_ = prose;
  noticeRetry_ = retry;
  screen_ = View::Notice;
  interactionsReady_ = false;
  requestUpdate();
}

void TasksActivity::showSection(const tasksui::Section section) {
  section_ = section;
  top_ = 0;
  screen_ = View::List;
  rebuild();
  interactionsReady_ = false;
  requestUpdate();
}

void TasksActivity::openDetail(const int row) {
  if (row < 0 || row >= static_cast<int>(rowIds_.size()) || rowIds_[static_cast<size_t>(row)].empty()) return;
  detailId_ = rowIds_[static_cast<size_t>(row)];
  screen_ = View::Detail;
  interactionsReady_ = false;
  requestUpdate();
}

void TasksActivity::back() {
  if (screen_ == View::Detail || (screen_ == View::Notice && haveConfig_)) {
    screen_ = View::List;
    interactionsReady_ = false;
    requestUpdate();
    return;
  }
  const bool atHome = section_ == tasksui::Today;
  if (screen_ == View::List && !atHome) {
    showSection(tasksui::Today);
    return;
  }
  leave(Leave::Shelf);
}

bool TasksActivity::handleHomeGesture() {
  if (leaving_ != Leave::None || syncing_) return true;  // already on the way out
  leave(Leave::Home);
  return true;
}

void TasksActivity::leave(const Leave where) {
  const bool canSend = haveConfig_ && !state_.queue.empty() && WiFi.status() == WL_CONNECTED;
  if (canSend) {
    leaving_ = where;
    interactionsReady_ = false;
    requestUpdate();
    return;  // the loop sends, then leaves
  }
  // Offline: the changes stay queued on the card for the next visit.
  if (where == Leave::Home) {
    onGoHome();
  } else {
    shelf::leave(renderer, mappedInput);
  }
}

void TasksActivity::runSendAndLeave() {
  const Leave where = leaving_;
  syncing_ = true;
  aborted_ = false;
  requestUpdateAndWait();
  tasks::TasksSync sync(cfg_);
  const tasks::SyncOutcome o = sync.sendOnly(state_, [this] { return pollDuringSync(); });
  LOG_INF(kTag, "on leaving: %s, %d change(s) sent, %u still queued", tasks::syncResultName(o.result), o.sentOps,
          static_cast<unsigned>(state_.queue.size()));
  tasks::store::saveState(state_);
  syncing_ = false;
  leaving_ = Leave::None;
  // Left either way: whatever did not go out is on the card for next time.
  if (where == Leave::Home) {
    onGoHome();
  } else {
    shelf::leave(renderer, mappedInput);
  }
}

void TasksActivity::page(const bool down) {
  if (screen_ != View::List) return;
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  tasksui::ListModel probe;
  probe.section = section_;
  for (int i = 0; i < 4; ++i) probe.counts[i] = counts_[i];
  probe.rows = rows_.data();
  probe.count = static_cast<int>(rows_.size());
  const std::vector<int> starts = tasksui::listPageStarts(target, target.deviceContext(), probe);
  int current = 0;
  for (size_t i = 0; i < starts.size(); ++i)
    if (starts[i] <= top_) current = static_cast<int>(i);
  const int next = down ? current + 1 : current - 1;
  if (next < 0 || next >= static_cast<int>(starts.size())) return;
  top_ = starts[static_cast<size_t>(next)];
  rebuild();
  interactionsReady_ = false;
  requestUpdate();
}

// --- Loop -------------------------------------------------------------------------

void TasksActivity::loop() {
  if (syncOnOpen_) {
    syncOnOpen_ = false;
    askSync();
    return;
  }
  if (leaving_ != Leave::None) {
    runSendAndLeave();
    return;
  }
  if (syncPending_) {
    syncPending_ = false;
    homeDuringSync_ = false;
    runSync();
    // The gesture that cancelled the sync was spent inside it; act on it here.
    if (homeDuringSync_) leave(Leave::Home);
    return;
  }

  // Back on the per-frame path, above the tap guard: the back swipe arrives as
  // Button::Back and a swipe is not a tap (host-tests/backgesture).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    back();
    return;
  }
  const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (down || up) {
    page(down);
    return;
  }

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y) || !interactionsReady_) return;
  fui::InputSnapshot input{};
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(x);
  input.touchY = static_cast<int16_t>(y);
  const fui::ActionEvent action = interactions_.route(input);

  switch (action.action) {
    case tasksui::ActionToggle: {
      const int row = action.value;
      if (row < 0 || row >= static_cast<int>(rowIds_.size())) return;
      setChecked(rowIds_[static_cast<size_t>(row)], !rows_[static_cast<size_t>(row)].done);
      return;
    }
    case tasksui::ActionOpen:
      openDetail(action.value);
      return;
    case tasksui::ActionSection:
      showSection(static_cast<tasksui::Section>(action.value));
      return;
    case tasksui::ActionSync:
      askSync();
      return;
    case tasksui::ActionComplete:
    case tasksui::ActionRestore:
      screen_ = View::List;
      setChecked(detailId_, action.action == tasksui::ActionComplete);
      return;
    case tasksui::ActionBack:
      back();
      return;
    case tasksui::ActionRetry:
      loadAll();
      if (haveConfig_) {
        screen_ = View::List;
        askSync();
      }
      return;
    default:
      return;
  }
}

// --- Render -----------------------------------------------------------------------

void TasksActivity::render(RenderLock&&) {
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  // A task can leave the lists under an open detail (a sync, a check-off from
  // elsewhere); the list is shown instead of a blank page.
  if (screen_ == View::Detail && findTask(detailId_) == nullptr) screen_ = View::List;
  const std::string status = statusLine();
  // The radio holds the loop while it works, so the list could not take a tap:
  // the whole screen says what is happening instead of a list that looks live.
  const bool busy = haveConfig_ && screen_ != View::Notice &&
                    (syncing_ || syncPending_ || syncOnOpen_ || leaving_ != Leave::None);
  if (busy) {
    char prose[160];
    const size_t waiting = state_.queue.size();
    if (leaving_ != Leave::None) {
      snprintf(prose, sizeof(prose), "Sending %u change%s to Notion before closing. Back skips this; they will go next time.",
               static_cast<unsigned>(waiting), waiting == 1 ? "" : "s");
    } else if (waiting > 0) {
      snprintf(prose, sizeof(prose), "Syncing with Notion: sending %u change%s, then fetching your tasks. Back cancels.",
               static_cast<unsigned>(waiting), waiting == 1 ? "" : "s");
    } else {
      snprintf(prose, sizeof(prose), "Syncing with Notion: fetching your tasks. One moment. Back cancels.");
    }
    tasksui::NoticeModel m;
    m.title = "SYNCING";
    m.prose = prose;
    tasksui::buildNotice(screen, m);
    interactionsReady_ = true;
    renderer.displayBuffer();
    return;
  }
  switch (screen_) {
    case View::List: {
      tasksui::ListModel m;
      m.section = section_;
      for (int i = 0; i < 4; ++i) m.counts[i] = counts_[i];
      m.rows = rows_.data();
      m.count = static_cast<int>(rows_.size());
      m.firstVisible = top_;
      m.pageLabel = pageLabel_.empty() ? nullptr : pageLabel_.c_str();
      m.status = status.c_str();
      m.syncIcon = &icon_refresh_cw_32;
      switch (section_) {
        case tasksui::Missed:
          m.empty = "Nothing missed.";
          break;
        case tasksui::Tomorrow:
          m.empty = "Nothing due tomorrow.";
          break;
        case tasksui::Done:
          m.empty = "Nothing completed lately.";
          break;
        default:
          m.empty = state_.syncedAt > 0 ? "Nothing due today." : "Waiting for Notion.";
          break;
      }
      tasksui::buildList(screen, m);
      break;
    }
    case View::Detail: {
      const tasks::Task* t = findTask(detailId_);
      const std::string title = tasks::displayTitle(t->title);
      const std::string category = tasks::displayText(t->category);
      const std::string due = tasks::shortDate(t->due);
      const std::string status2 = tasks::displayText(t->status);
      const std::string priority = tasks::displayText(t->priority);
      std::string notes = tasks::displayText(t->notes);
      if (t->notesTruncated) notes += "...";
      int kids = 0, kidsDone = 0;
      for (const tasks::Task& other : state_.tasks) {
        if (other.parentId != t->id) continue;
        ++kids;
        if (other.done) ++kidsDone;
      }
      char sub[48] = "";
      if (kids > 0) snprintf(sub, sizeof(sub), "%d SUB-ITEM%s, %d DONE", kids, kids == 1 ? "" : "S", kidsDone);
      const tasksui::Field fields[] = {{"CATEGORY", category.c_str()},
                                       {"DUE", due.c_str()},
                                       {"STATUS", status2.c_str()},
                                       {"PRIORITY", priority.c_str()}};
      tasksui::DetailModel m;
      m.title = title.c_str();
      m.fields = fields;
      m.fieldCount = 4;
      m.notes = notes.empty() ? nullptr : notes.c_str();
      m.subItems = kids > 0 ? sub : nullptr;
      m.done = t->done;
      tasksui::buildDetail(screen, m);
      break;
    }
    case View::Notice: {
      tasksui::NoticeModel m;
      m.prose = notice_.c_str();
      m.action = noticeRetry_ ? "TRY AGAIN" : nullptr;
      tasksui::buildNotice(screen, m);
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Tasks");
  renderer.displayBuffer();
}
