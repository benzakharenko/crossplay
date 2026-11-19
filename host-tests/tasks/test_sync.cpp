// End-to-end test of TasksSync's orchestration in its simulator build, against
// the canned replies in fake/ (TASKS_FAKE_DIR). Covers: resolving the database
// link to a data source, flushing the queue, the main query, the sub-item
// query, and the resulting view.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../src/apps_local/tasks/TasksSync.h"

static int failures = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (!(c)) {                                                          \
      std::fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #c); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

using namespace tasks;

static std::string ids(const std::vector<Row>& rows) {
  std::string out;
  for (const Row& r : rows) {
    if (!out.empty()) out += " ";
    out += std::string(static_cast<size_t>(r.depth), '>') + r.task.id;
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  setenv("TASKS_FAKE_DIR", argv[1], 1);
  Config cfg;
  std::string err;
  CHECK(parseConfig(R"({"token":"ntn_FAKE","database":"https://www.notion.so/me/Task-List-1a2b3c4d5e6f40718293a4b5c6d7e8f9?v=1"})",
                    cfg, err));
  cfg.priorityOrder = {"High", "Medium", "Low"};
  const std::string today = "2026-11-19";

  State st;
  st.tasks = {};
  PendingOp op;
  op.id = "t-old";
  op.done = true;
  op.status = "Completed";
  st.queue.push_back(op);

  TasksSync sync(cfg);
  SyncOutcome o = sync.sync(st, today, nullptr);
  CHECK(o.result == SyncResult::Ok);
  CHECK(o.sentOps == 1);
  CHECK(st.queue.empty());
  // The database's first table (an archive) lacks the columns; the second fits.
  CHECK(st.dataSourceId == "ds-fake-1");
  CHECK(st.databaseRef == cfg.databaseRef);
  CHECK(st.syncedAt > 0);

  View v = buildView(effectiveTasks(st, cfg, today), cfg, today);
  const std::string t = ids(v.today), m = ids(v.missed), tm = ids(v.tomorrow), c = ids(v.completed);
  std::printf("today: %s\nmissed: %s\ntomorrow: %s\ncompleted: %s\n", t.c_str(), m.c_str(), tm.c_str(), c.c_str());
  CHECK(t == "t-plan >t-plan-goals >t-plan-cal t-gym");
  CHECK(m == "t-taxes");
  CHECK(tm == "t-groceries");
  CHECK(c == "t-email");
  // Sub-items asked for in both rounds are stored once.
  int plans = 0;
  for (const Task& task : st.tasks) plans += task.id == "t-plan-cal" ? 1 : 0;
  CHECK(plans == 1);

  // A table with nothing usable says what it saw.
  {
    Config only = cfg;
    only.dataSourceId = "ds-archive";
    TasksSync s2(only);
    State sa;
    const SyncOutcome oa = s2.sync(sa, today, nullptr);
    CHECK(oa.result == SyncResult::Failed);
    CHECK(oa.message.find("Columns seen: Name (title), Archived on (created_time)") != std::string::npos);
  }

  // No clock: refuses before any request.
  State st2;
  CHECK(sync.sync(st2, "", nullptr).result == SyncResult::NoClock);

  // Abort before anything goes out.
  State st3;
  CHECK(sync.sync(st3, today, [] { return true; }).result == SyncResult::Aborted);

  // A database Notion does not show the connection.
  setenv("TASKS_FAKE_DIR", "/nonexistent", 1);
  State st4;
  SyncOutcome o4 = sync.sync(st4, today, nullptr);
  CHECK(o4.result == SyncResult::NotShared);

  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::printf("tasks sync: all tests passed\n");
  return 0;
}
