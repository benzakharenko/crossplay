#pragma once

// The Tasks app's conversation with Notion, run on the device:
//   1. find the database's data source (once; cached in the state),
//   2. send queued check-offs and restores,
//   3. read the lists (missed, today, tomorrow, recently completed) and the
//      sub-items of every task shown.
// Each call holds the loop until it finishes or shouldAbort says stop; the
// activity pumps input from that callback (see WeatherActivity).
//
// Every request verifies Notion's certificate against TasksRoots.h (or the
// SD override): the token travels in every request, so an unverified
// connection would hand it to anyone on the network who asked.
//
// Simulator builds use curl instead of the device TLS stack. Setting
// TASKS_FAKE_DIR there replays canned replies from that folder (see
// host-tests/tasks/fake/) so the screens can be photographed offline.

#include <functional>
#include <string>
#include <vector>

#include "TasksCore.h"
#include "TasksState.h"

namespace tasks {

enum class SyncResult {
  Ok,
  Aborted,      // shouldAbort returned true
  NoClock,      // device clock not set yet: no "today" to ask for
  Unreachable,  // network/TLS failure; offline check-offs stay queued
  BadToken,     // 401: token wrong or revoked
  NotShared,    // 404: database not shared with the connection
  RateLimited,  // 429
  Failed,       // anything else; message has Notion's words when it gave any
};

struct SyncOutcome {
  SyncResult result = SyncResult::Failed;
  std::string message;  // one line for the screen
  // Set even on Ok: Notion rejected a queued change (e.g. a status name that
  // does not exist). The change is dropped so it cannot block later syncs.
  std::string warning;
  int sentOps = 0;
};

class TasksSync {
 public:
  using Abort = std::function<bool()>;

  explicit TasksSync(const Config& cfg) : cfg_(cfg) {}

  // Sends queued ops, then refreshes state.tasks. On anything but Ok the
  // state keeps its last good list; ops Notion accepted are still dropped
  // from the queue.
  SyncOutcome sync(State& state, const std::string& today, const Abort& shouldAbort);

 private:
  // One request. Returns the HTTP status, 0 on transport failure, -1 when
  // aborted. Notion's reply body lands in out.
  int call(const char* method, const std::string& path, const std::string& body, std::string& out,
           const Abort& shouldAbort);
  bool resolveDataSource(State& state, SyncOutcome& o, const Abort& shouldAbort);
  bool sendQueue(State& state, SyncOutcome& o, const Abort& shouldAbort);
  // Runs one query across its pages; build(cursor) makes each page's body.
  bool query(const std::function<std::string(const std::string&)>& build, int maxPages, std::vector<Task>& into,
             SyncOutcome& o, const Abort& shouldAbort);
  SyncOutcome fail(int status, const std::string& body);

  const Config& cfg_;
  std::string dataSourceId_;
};

const char* syncResultName(SyncResult r);

}  // namespace tasks
