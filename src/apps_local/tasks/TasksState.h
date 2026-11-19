#pragma once

// What the Tasks app keeps on the card between visits, as pure logic:
//   - the last task list read from Notion, so the app opens instantly and
//     works offline;
//   - check-offs and restores made on the device that Notion has not been
//     told about yet (the queue);
//   - each task's status from before it was checked off here, so a restore
//     from the Completed bin puts back "In progress" rather than guessing.
//
// TasksStore (device side) reads and writes the files; this file only turns
// them into structures and back, so host-tests/tasks covers it.

#include <cstdint>
#include <string>
#include <vector>

#include "TasksCore.h"

namespace tasks {

struct PendingOp {
  std::string id;
  bool done = false;   // true: check off; false: restore
  std::string status;  // status name to write (empty for checkbox databases)
};

struct Remembered {
  std::string id;
  std::string previous;  // status before the check-off
  std::string date;      // YYYY-MM-DD it was checked off, for pruning
};

struct State {
  std::string dataSourceId;  // resolved from the database link, cached
  std::string databaseRef;   // the link it was resolved from
  int64_t syncedAt = 0;      // epoch seconds of the last good sync, 0 = never
  std::vector<Task> tasks;   // as of the last sync, before queued ops
  std::vector<PendingOp> queue;
  std::vector<Remembered> remembered;
};

std::string encodeState(const State& s);
// False on unreadable JSON; out is then left default (an empty, never-synced
// state), which the app treats as a first run.
bool decodeState(const std::string& json, State& out);

// Checks a task off (done) or restores it, locally: records the op in the
// queue (replacing an earlier op for the same task), remembers the earlier
// status on check-off, and updates the cached task so the screens move it
// between the lists at once. today is YYYY-MM-DD.
void markDone(State& s, const Config& cfg, const std::string& id, bool done, const std::string& today);

// The cached tasks with queued ops applied: what the screens should show.
std::vector<Task> effectiveTasks(const State& s, const Config& cfg, const std::string& today);

// After a successful sync: drop the ops Notion accepted and memories older
// than the Completed bin can show.
void ackOps(State& s, const std::vector<std::string>& sentIds);
void pruneRemembered(State& s, const Config& cfg, const std::string& today);

}  // namespace tasks
