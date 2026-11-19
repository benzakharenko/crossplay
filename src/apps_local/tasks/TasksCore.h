#pragma once

// Tasks app core: everything about talking to Notion that does not need a
// device. Builds the query and update requests, parses Notion's replies,
// buckets tasks into Missed / Today / Tomorrow, and does the date arithmetic.
//
// Freestanding C++17 plus ArduinoJson, so host-tests/tasks/ exercises all of it
// against recorded Notion replies. No network, no SD card, no clock: the caller
// passes "today" in, and TasksSync (device side) does the I/O.
//
// Notion API version 2025-09-03: rows are queried through
// POST /v1/data_sources/{id}/query and updated through PATCH /v1/pages/{id}.

#include <cstddef>
#include <string>
#include <vector>

namespace tasks {

constexpr const char* kNotionHost = "api.notion.com";
constexpr const char* kNotionVersion = "2025-09-03";

// Notes longer than this are cut (on a UTF-8 boundary) when parsed, to bound
// memory on the device. The detail view shows a "…" when it happens.
constexpr size_t kMaxNotesBytes = 1500;
constexpr size_t kMaxTitleBytes = 300;

// Children are fetched for at most this many parents per request; the caller
// splits longer lists.
constexpr size_t kMaxParentsPerQuery = 50;

// How the done/not-done state is stored in Notion.
enum class StatusKind { Status, Select, Checkbox };

struct PropNames {
  std::string title = "Task";
  std::string category = "Category";
  std::string due = "Due Date";
  std::string status = "Status";
  std::string priority = "Priority";
  std::string notes = "Notes";
  std::string parent = "Parent Item";  // relation to the parent task
};

// Read from /.crosspoint/tasks.json on the SD card. Only token and
// data_source_id are required; everything else has defaults matching the
// owner's database.
struct Config {
  std::string token;
  // Either may be given. "database" takes the database's link (or its id) as
  // copied from Notion; the app looks up its data source once and caches it.
  std::string dataSourceId;
  std::string databaseRef;
  PropNames props;
  StatusKind statusKind = StatusKind::Status;
  // Status values that count as done. The first one is written when a task is
  // checked off. Ignored for StatusKind::Checkbox.
  std::vector<std::string> doneValues{"Completed"};
  // Written when a task is restored from the Completed bin and its earlier
  // status is unknown. Ignored for Checkbox.
  std::string openValue = "Not started";
  // The Completed bin holds tasks marked done within this many days, so a
  // mistaken check-off can be restored.
  int completedDays = 2;
  // Optional: priority values from most to least urgent, for sorting.
  std::vector<std::string> priorityOrder;
};

// Parses tasks.json. Needs token, and data_source_id or database. On failure returns false and sets err to one line a
// person can act on ("tasks.json: token is missing").
bool parseConfig(const std::string& json, Config& out, std::string& err);

enum class Bucket { Missed, Today, Tomorrow, Completed, Other };

struct Task {
  std::string id;        // Notion page id
  std::string title;
  std::string category;  // select / multi-select names, joined with ", "
  std::string due;       // YYYY-MM-DD (date part of the start), or empty
  std::string status;    // status/select name; "true"/"false" for a checkbox
  std::string priority;
  std::string notes;
  std::string parentId;    // first page in the parent relation, or empty
  std::string lastEdited;  // YYYY-MM-DD of Notion's last_edited_time (UTC)
  bool notesTruncated = false;
  bool done = false;
};

struct Row {
  Task task;
  int depth = 0;  // 0 = top level, 1 = sub-item, ...
};

// What the screens draw. Missed, Today and Tomorrow hold open tasks, each
// followed by its open sub-items. Completed is flat, most recent first.
struct View {
  std::vector<Row> missed, today, tomorrow, completed;
};

struct QueryPage {
  std::vector<Task> tasks;
  bool hasMore = false;
  std::string nextCursor;
};

// Request path for the query, with filter_properties so Notion returns only
// the six properties the app shows.
std::string queryPath(const Config& cfg);

// Request body for the main query: due today, due tomorrow, past due and not
// done, or done and edited in the last completedDays days. Sorted by due date.
// cursor is empty for the first page.
std::string queryBody(const Config& cfg, const std::string& today, const std::string& tomorrow,
                      const std::string& cursor = "", int pageSize = 100);

// Request body for the sub-items of up to kMaxParentsPerQuery parents.
std::string childrenQueryBody(const Config& cfg, const std::vector<std::string>& parentIds,
                              const std::string& cursor = "", int pageSize = 100);

// Parses one page of a query reply. Returns false on a Notion error object or
// malformed JSON, with err set to Notion's message when there is one.
bool parseQueryResponse(const std::string& body, const Config& cfg, QueryPage& out, std::string& err);

// PATCH request setting a task's status. statusName is ignored for Checkbox
// (done decides). Use doneStatus()/restoreStatus() for the name.
std::string updatePath(const std::string& pageId);
std::string updateBody(const Config& cfg, bool done, const std::string& statusName = "");
const std::string& doneStatus(const Config& cfg);
// Status to restore a completed task to: its status before it was checked off
// on this device when known (e.g. "In progress"), otherwise openValue.
std::string restoreStatus(const Config& cfg, const std::string& previous);

// The 32-hex page/database id in a Notion link or bare id, as a dashed UUID
// ("1a2b...-...."), or empty when there is none. For a database link the id is
// the one in the path, not the ?v= view id.
std::string extractNotionId(const std::string& linkOrId);

// GET /v1/databases/{id} and the first data source id from its reply.
std::string databasePath(const std::string& databaseId);
bool parseDatabaseReply(const std::string& body, std::string& dataSourceId, std::string& err);

// Error message from a Notion error reply, or empty.
std::string errorMessage(const std::string& body);

// Date arithmetic on YYYY-MM-DD strings. Returns empty on a malformed date.
std::string addDays(const std::string& ymd, int days);
bool isValidDate(const std::string& ymd);

Bucket bucketFor(const Task& t, const Config& cfg, const std::string& today, const std::string& tomorrow);

// Groups tasks (from the main and children queries; duplicates by id are
// dropped) into the four lists, nesting open sub-items under an open parent
// shown in a list. A sub-item whose parent is not shown stands on its own in
// its own bucket.
View buildView(const std::vector<Task>& all, const Config& cfg, const std::string& today);

// Order within a bucket: not-done first, then by priorityOrder (unknown
// priorities last), then by due date, then by title.
void sortTasks(std::vector<Task>& v, const Config& cfg);

bool isDoneValue(const Config& cfg, const std::string& status);

// JSON string escaping for the request bodies (exposed for tests).
std::string jsonEscape(const std::string& s);

}  // namespace tasks
