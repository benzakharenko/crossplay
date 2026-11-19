// Host tests for the Tasks app core (src/apps_local/tasks/TasksCore).
// Fixtures follow Notion API 2025-09-03 reply shapes, trimmed to what matters.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../src/apps_local/tasks/TasksCore.h"

static int failures = 0;

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                  \
    }                                                              \
  } while (0)

#define CHECK_EQ(a, b)                                                                        \
  do {                                                                                        \
    const std::string _a = (a), _b = (b);                                                     \
    if (_a != _b) {                                                                           \
      std::fprintf(stderr, "%s:%d: FAIL %s\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, #a, \
                   _a.c_str(), _b.c_str());                                                   \
      ++failures;                                                                             \
    }                                                                                         \
  } while (0)

using namespace tasks;

static const char* kConfig = R"({
  "token": "ntn_TEST",
  "data_source_id": "1f2e3d4c-0000-0000-0000-000000000001",
  "done": ["Completed", "Archived"],
  "open": "Not started",
  "priority_order": ["High", "Medium", "Low"]
})";

static Config config() {
  Config c;
  std::string err;
  if (!parseConfig(kConfig, c, err)) {
    std::fprintf(stderr, "config: %s\n", err.c_str());
    std::exit(1);
  }
  return c;
}

static std::string& err0() {
  static std::string e;
  return e;
}

static void testConfig() {
  Config c = config();
  CHECK_EQ(c.token, "ntn_TEST");
  CHECK_EQ(c.props.title, "Task");
  CHECK_EQ(c.props.due, "Due Date");
  CHECK(c.statusKind == StatusKind::Status);
  CHECK(c.doneValues.size() == 2);
  CHECK_EQ(c.props.parent, "Parent Item");
  CHECK(c.completedDays == 2);
  Config d;
  CHECK(parseConfig(R"({"token":"t","data_source_id":"x"})", d, err0()));
  CHECK_EQ(d.doneValues.front(), "Completed");
  CHECK_EQ(d.openValue, "Not started");
  CHECK(c.priorityOrder.size() == 3);

  std::string err;
  Config bad;
  CHECK(!parseConfig("{", bad, err));
  CHECK(!parseConfig(R"({"data_source_id":"x"})", bad, err));
  CHECK_EQ(err, "tasks.json: token is missing");
  CHECK(!parseConfig(R"({"token":"t","data_source_id":"x","status_type":"bogus"})", bad, err));
  CHECK(parseConfig(R"({"token":"t","data_source_id":"x","done":"Complete",
                        "properties":{"due":"When"}})", bad, err));
  CHECK(bad.doneValues.size() == 1 && bad.doneValues[0] == "Complete");
  CHECK(!parseConfig(R"({"token":"t","data_source_id":"x","completed_days":99})", bad, err));
  CHECK_EQ(bad.props.due, "When");
  CHECK_EQ(bad.props.title, "Task");
}

static void testDates() {
  CHECK_EQ(addDays("2026-11-19", 1), "2026-11-20");
  CHECK_EQ(addDays("2026-11-30", 1), "2026-12-01");
  CHECK_EQ(addDays("2026-12-31", 1), "2027-01-01");
  CHECK_EQ(addDays("2026-03-01", -1), "2026-02-28");
  CHECK_EQ(addDays("2028-03-01", -1), "2028-02-29");
  CHECK_EQ(addDays("2026-02-30", 1), "");
  CHECK_EQ(addDays("nonsense!!", 1), "");
  CHECK(isValidDate("2024-02-29"));
  CHECK(!isValidDate("2026-02-29"));
  CHECK(!isValidDate("2026-13-01"));
}

static void testRequests() {
  Config c = config();
  CHECK_EQ(queryPath(c),
           "/v1/data_sources/1f2e3d4c-0000-0000-0000-000000000001/query"
           "?filter_properties%5B%5D=Task&filter_properties%5B%5D=Category"
           "&filter_properties%5B%5D=Due%20Date&filter_properties%5B%5D=Status"
           "&filter_properties%5B%5D=Priority&filter_properties%5B%5D=Notes"
           "&filter_properties%5B%5D=Parent%20Item");

  CHECK_EQ(queryBody(c, "2026-11-19", "2026-11-20"),
           R"({"filter":{"or":[)"
           R"({"property":"Due Date","date":{"equals":"2026-11-19"}},)"
           R"({"property":"Due Date","date":{"equals":"2026-11-20"}},)"
           R"({"and":[{"property":"Due Date","date":{"before":"2026-11-19"}},)"
           R"({"property":"Status","status":{"does_not_equal":"Completed"}},)"
           R"({"property":"Status","status":{"does_not_equal":"Archived"}}]},)"
           R"({"and":[{"property":"Status","status":{"equals":"Completed"}},)"
           R"({"timestamp":"last_edited_time","last_edited_time":{"on_or_after":"2026-11-17"}}]}]})"
           R"(,"sorts":[{"property":"Due Date","direction":"ascending"}],"page_size":100})");

  CHECK_EQ(childrenQueryBody(c, {"p1", "p2"}),
           R"({"filter":{"or":[{"property":"Parent Item","relation":{"contains":"p1"}},)"
           R"({"property":"Parent Item","relation":{"contains":"p2"}}]})"
           R"(,"sorts":[{"property":"Due Date","direction":"ascending"}],"page_size":100})");
  std::vector<std::string> many(80, "x");
  const std::string capped = childrenQueryBody(c, many);
  size_t count = 0;
  for (size_t at = capped.find("contains"); at != std::string::npos; at = capped.find("contains", at + 1)) ++count;
  CHECK(count == kMaxParentsPerQuery);

  Config noBin = c;
  noBin.completedDays = 0;
  CHECK(queryBody(noBin, "2026-11-19", "2026-11-20").find("last_edited_time") == std::string::npos);

  CHECK(queryBody(c, "2026-11-19", "2026-11-20", "abc").find(R"("start_cursor":"abc")") != std::string::npos);

  CHECK_EQ(updatePath("abc-123"), "/v1/pages/abc-123");
  CHECK_EQ(updateBody(c, true), R"({"properties":{"Status":{"status":{"name":"Completed"}}}})");
  CHECK_EQ(updateBody(c, false, restoreStatus(c, "In progress")),
           R"({"properties":{"Status":{"status":{"name":"In progress"}}}})");
  CHECK_EQ(restoreStatus(c, ""), "Not started");
  CHECK_EQ(restoreStatus(c, "Completed"), "Not started");
  CHECK_EQ(updateBody(c, false), R"({"properties":{"Status":{"status":{"name":"Not started"}}}})");

  Config cb = c;
  cb.statusKind = StatusKind::Checkbox;
  CHECK_EQ(updateBody(cb, true), R"({"properties":{"Status":{"checkbox":true}}})");
  CHECK(queryBody(cb, "2026-11-19", "2026-11-20").find(R"("checkbox":{"equals":false})") != std::string::npos);

  Config sel = c;
  sel.statusKind = StatusKind::Select;
  sel.doneValues = {"Done"};
  const std::string b = queryBody(sel, "2026-11-19", "2026-11-20");
  CHECK(b.find(R"({"property":"Status","select":{"does_not_equal":"Done"}})") != std::string::npos);
  CHECK(b.find(R"({"property":"Status","select":{"equals":"Done"}})") != std::string::npos);

  CHECK_EQ(jsonEscape("a\"b\\c\nd\x01"), "a\\\"b\\\\c\\nd\\u0001");
}

static const char* kReply = R"({
  "object": "list",
  "results": [
    {
      "object": "page", "id": "p-missed", "in_trash": false, "archived": false,
      "last_edited_time": "2026-11-18T22:10:00.000Z",
      "icon": null, "cover": null, "url": "https://www.notion.so/x",
      "properties": {
        "Task": {"id": "title", "type": "title", "title": [
          {"type": "text", "plain_text": "Renew ", "annotations": {"bold": false}},
          {"type": "text", "plain_text": "passport", "annotations": {"bold": true}}]},
        "Category": {"id": "a", "type": "select", "select": {"id": "1", "name": "Admin", "color": "blue"}},
        "Due Date": {"id": "b", "type": "date", "date": {"start": "2026-11-17", "end": null, "time_zone": null}},
        "Status": {"id": "c", "type": "status", "status": {"id": "2", "name": "In progress", "color": "blue"}},
        "Priority": {"id": "d", "type": "select", "select": {"name": "High"}},
        "Notes": {"id": "e", "type": "rich_text", "rich_text": [{"type": "text", "plain_text": "Photos first"}]}
      }
    },
    {
      "object": "page", "id": "p-today-done", "in_trash": false,
      "properties": {
        "Task": {"type": "title", "title": [{"plain_text": "Call dentist"}]},
        "Category": {"type": "multi_select", "multi_select": [{"name": "Health"}, {"name": "Calls"}]},
        "Due Date": {"type": "date", "date": {"start": "2026-11-19T09:30:00.000-08:00"}},
        "Status": {"type": "status", "status": {"name": "Completed"}},
        "Priority": {"type": "select", "select": null},
        "Notes": {"type": "rich_text", "rich_text": []}
      }
    },
    {
      "object": "page", "id": "p-today-low", "in_trash": false,
      "properties": {
        "Task": {"type": "title", "title": [{"plain_text": "Water plants"}]},
        "Category": {"type": "select", "select": {"name": "Home"}},
        "Due Date": {"type": "date", "date": {"start": "2026-11-19"}},
        "Status": {"type": "status", "status": {"name": "Not started"}},
        "Priority": {"type": "select", "select": {"name": "Low"}},
        "Notes": {"type": "rich_text", "rich_text": []}
      }
    },
    {
      "object": "page", "id": "p-today-high", "in_trash": false,
      "properties": {
        "Task": {"type": "title", "title": [{"plain_text": "Ship report"}]},
        "Category": {"type": "select", "select": {"name": "Work"}},
        "Due Date": {"type": "date", "date": {"start": "2026-11-19"}},
        "Status": {"type": "status", "status": {"name": "Not started"}},
        "Priority": {"type": "select", "select": {"name": "High"}},
        "Notes": {"type": "rich_text", "rich_text": [{"plain_text": "Line one\nLine two"}]},
        "Parent Item": {"type": "relation", "relation": [], "has_more": false}
      }
    },
    {
      "object": "page", "id": "p-trashed", "in_trash": true,
      "properties": {"Task": {"type": "title", "title": [{"plain_text": "Gone"}]}}
    },
    {
      "object": "page", "id": "p-tomorrow", "in_trash": false,
      "properties": {
        "Task": {"type": "title", "title": [{"plain_text": "Groceries"}]},
        "Parent Item": {"type": "relation", "relation": [{"id": "p-today-high"}], "has_more": false},
        "Due Date": {"type": "date", "date": {"start": "2026-11-20"}},
        "Status": {"type": "status", "status": {"name": "Not started"}}
      }
    }
  ],
  "next_cursor": "cur-2",
  "has_more": true,
  "type": "page_or_data_source",
  "request_id": "r"
})";

static const Task* find(const QueryPage& p, const char* id) {
  for (const Task& t : p.tasks)
    if (t.id == id) return &t;
  return nullptr;
}

static void testParse() {
  Config c = config();
  QueryPage page;
  std::string err;
  CHECK(parseQueryResponse(kReply, c, page, err));
  CHECK(page.tasks.size() == 5);  // trashed one dropped
  CHECK(page.hasMore);
  CHECK_EQ(page.nextCursor, "cur-2");

  const Task* m = find(page, "p-missed");
  CHECK(m != nullptr);
  if (m) {
    CHECK_EQ(m->title, "Renew passport");
    CHECK_EQ(m->category, "Admin");
    CHECK_EQ(m->due, "2026-11-17");
    CHECK_EQ(m->status, "In progress");
    CHECK_EQ(m->priority, "High");
    CHECK_EQ(m->notes, "Photos first");
    CHECK(!m->done);
  }
  const Task* d = find(page, "p-today-done");
  CHECK(d != nullptr);
  if (d) {
    CHECK_EQ(d->category, "Health, Calls");
    CHECK_EQ(d->due, "2026-11-19");  // date part of a datetime
    CHECK_EQ(d->priority, "");
    CHECK(d->done);
  }
  const Task* t = find(page, "p-tomorrow");
  CHECK(t != nullptr);
  if (t) {
    CHECK_EQ(t->category, "");  // property absent from reply
    CHECK_EQ(t->parentId, "p-today-high");
  }
  if (m) CHECK_EQ(m->lastEdited, "2026-11-18");

  const std::string today = "2026-11-19", tomorrow = addDays(today, 1);
  CHECK(bucketFor(*m, c, today, tomorrow) == Bucket::Missed);
  CHECK(bucketFor(*d, c, today, tomorrow) == Bucket::Completed);  // done, due today
  CHECK(bucketFor(*t, c, today, tomorrow) == Bucket::Tomorrow);
  Task doneLate = *m;
  doneLate.done = true;
  CHECK(bucketFor(doneLate, c, today, tomorrow) == Bucket::Completed);  // edited yesterday
  doneLate.lastEdited = "2026-11-01";
  CHECK(bucketFor(doneLate, c, today, tomorrow) == Bucket::Other);

  std::vector<Task> mixed = page.tasks;
  sortTasks(mixed, c);
  CHECK_EQ(mixed.back().id, "p-today-done");  // done sorts last
}

static void testErrors() {
  Config c = config();
  QueryPage page;
  std::string err;
  const char* unauthorized =
      R"({"object":"error","status":401,"code":"unauthorized","message":"API token is invalid.","request_id":"r"})";
  CHECK(!parseQueryResponse(unauthorized, c, page, err));
  CHECK_EQ(err, "API token is invalid.");
  CHECK_EQ(errorMessage(unauthorized), "API token is invalid.");
  CHECK_EQ(errorMessage(kReply), "");
  CHECK(!parseQueryResponse("<html>502</html>", c, page, err));
  CHECK(!parseQueryResponse(R"({"object":"list"})", c, page, err));
}

static void testTruncation() {
  Config c = config();
  std::string notes;
  for (int i = 0; i < 1000; ++i) notes += "\xC3\xA9";  // é, 2000 bytes
  std::string reply = R"({"object":"list","has_more":false,"next_cursor":null,"results":[{"id":"x","properties":{)"
                      R"("Task":{"type":"title","title":[{"plain_text":"t"}]},)"
                      R"("Notes":{"type":"rich_text","rich_text":[{"plain_text":")" +
                      notes + R"("}]}}}]})";
  QueryPage page;
  std::string err;
  CHECK(parseQueryResponse(reply, c, page, err));
  CHECK(page.tasks.size() == 1);
  if (page.tasks.size() == 1) {
    const Task& t = page.tasks[0];
    CHECK(t.notesTruncated);
    CHECK(t.notes.size() <= kMaxNotesBytes);
    CHECK(t.notes.size() % 2 == 0);  // never split a 2-byte character
  }
  CHECK(!page.hasMore);
  CHECK_EQ(page.nextCursor, "");
}

static Task mk(const char* id, const char* due, const char* parent = "", bool done = false,
              const char* priority = "", const char* edited = "2026-11-19") {
  Task t;
  t.id = id;
  t.title = id;
  t.due = due;
  t.parentId = parent;
  t.done = done;
  t.status = done ? "Completed" : "Not started";
  t.priority = priority;
  t.lastEdited = edited;
  return t;
}

static std::string ids(const std::vector<Row>& rows) {
  std::string out;
  for (const Row& r : rows) {
    if (!out.empty()) out += " ";
    out += std::string(static_cast<size_t>(r.depth), '>') + r.task.id;
  }
  return out;
}

static void testView() {
  Config c = config();
  const std::string today = "2026-11-19";
  std::vector<Task> all = {
      mk("A", "2026-11-19", "", false, "Low"),
      mk("B", "2026-11-19", "", false, "High"),
      mk("B1", "", "B", false, "Low"),             // no due date: nests under B
      mk("B2", "2026-11-25", "B", false, "High"),  // due later: still under B
      mk("B2a", "", "B2"),                         // grandchild
      mk("B3", "2026-11-19", "B", true),           // done: bin, not under B
      mk("C1", "2026-11-20", "Z"),                 // parent not fetched: stands alone
      mk("M", "2026-11-15"),
      mk("M1", "2026-11-19", "M"),                 // due today, but its parent is missed
      mk("X", "2026-11-10", "", true, "", "2026-11-18"),
      mk("Old", "2026-11-10", "", true, "", "2026-10-01"),
      mk("A", "2026-11-19", "", false, "Low"),     // duplicate from children query
  };
  View v = buildView(all, c, today);
  CHECK_EQ(ids(v.today), "B >B2 >>B2a >B1 A");
  CHECK_EQ(ids(v.missed), "M >M1");
  CHECK_EQ(ids(v.tomorrow), "C1");
  CHECK_EQ(ids(v.completed), "B3 X");

  // A child whose parent is open but not in any list stands alone.
  std::vector<Task> orphan = {mk("P", "2026-12-01"), mk("K", "2026-11-19", "P")};
  View o = buildView(orphan, c, today);
  CHECK_EQ(ids(o.today), "K");

  // A parent-cycle in bad data must not hang.
  std::vector<Task> cyc = {mk("Q", "2026-11-19", "R"), mk("R", "2026-11-19", "Q")};
  View y = buildView(cyc, c, today);
  CHECK(y.today.size() == 2);
}

int main() {
  testConfig();
  testDates();
  testRequests();
  testParse();
  testErrors();
  testTruncation();
  testView();
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::printf("tasks: all tests passed\n");
  return 0;
}
