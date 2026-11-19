#include "TasksState.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace tasks {

namespace {

constexpr int kStateVersion = 1;

const Remembered* findRemembered(const State& s, const std::string& id) {
  for (const Remembered& r : s.remembered)
    if (r.id == id) return &r;
  return nullptr;
}

void applyOp(Task& t, const PendingOp& op, const Config& cfg, const std::string& today) {
  t.done = op.done;
  if (cfg.statusKind == StatusKind::Checkbox) {
    t.status = op.done ? "true" : "false";
  } else {
    t.status = op.status;
  }
  // Keeps a fresh check-off in the Completed bin until Notion's own edit time
  // arrives with the next sync.
  if (op.done) t.lastEdited = today;
}

}  // namespace

std::string encodeState(const State& s) {
  JsonDocument doc;
  doc["v"] = kStateVersion;
  doc["data_source_id"] = s.dataSourceId;
  doc["database"] = s.databaseRef;
  doc["synced_at"] = s.syncedAt;
  JsonArray tasks = doc["tasks"].to<JsonArray>();
  for (const Task& t : s.tasks) {
    JsonObject o = tasks.add<JsonObject>();
    o["id"] = t.id;
    o["title"] = t.title;
    if (!t.category.empty()) o["category"] = t.category;
    if (!t.due.empty()) o["due"] = t.due;
    o["status"] = t.status;
    if (!t.priority.empty()) o["priority"] = t.priority;
    if (!t.notes.empty()) o["notes"] = t.notes;
    if (t.notesTruncated) o["notes_cut"] = true;
    if (!t.parentId.empty()) o["parent"] = t.parentId;
    if (!t.lastEdited.empty()) o["edited"] = t.lastEdited;
    if (t.done) o["done"] = true;
  }
  JsonArray queue = doc["queue"].to<JsonArray>();
  for (const PendingOp& op : s.queue) {
    JsonObject o = queue.add<JsonObject>();
    o["id"] = op.id;
    o["done"] = op.done;
    o["status"] = op.status;
  }
  JsonArray mem = doc["remembered"].to<JsonArray>();
  for (const Remembered& r : s.remembered) {
    JsonObject o = mem.add<JsonObject>();
    o["id"] = r.id;
    o["previous"] = r.previous;
    o["date"] = r.date;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool decodeState(const std::string& json, State& out) {
  out = State();
  JsonDocument doc;
  if (deserializeJson(doc, json) || doc["v"] != kStateVersion) return false;
  State s;
  s.dataSourceId = doc["data_source_id"] | "";
  s.databaseRef = doc["database"] | "";
  s.syncedAt = doc["synced_at"] | static_cast<int64_t>(0);
  JsonArrayConst tasks = doc["tasks"];
  s.tasks.reserve(tasks.size());
  for (JsonObjectConst o : tasks) {
    Task t;
    t.id = o["id"] | "";
    if (t.id.empty()) continue;
    t.title = o["title"] | "";
    t.category = o["category"] | "";
    t.due = o["due"] | "";
    t.status = o["status"] | "";
    t.priority = o["priority"] | "";
    t.notes = o["notes"] | "";
    t.notesTruncated = o["notes_cut"] | false;
    t.parentId = o["parent"] | "";
    t.lastEdited = o["edited"] | "";
    t.done = o["done"] | false;
    s.tasks.push_back(std::move(t));
  }
  for (JsonObjectConst o : doc["queue"].as<JsonArrayConst>()) {
    PendingOp op;
    op.id = o["id"] | "";
    op.done = o["done"] | false;
    op.status = o["status"] | "";
    if (!op.id.empty()) s.queue.push_back(std::move(op));
  }
  for (JsonObjectConst o : doc["remembered"].as<JsonArrayConst>()) {
    Remembered r;
    r.id = o["id"] | "";
    r.previous = o["previous"] | "";
    r.date = o["date"] | "";
    if (!r.id.empty()) s.remembered.push_back(std::move(r));
  }
  out = std::move(s);
  return true;
}

void markDone(State& s, const Config& cfg, const std::string& id, bool done, const std::string& today) {
  auto task = std::find_if(s.tasks.begin(), s.tasks.end(), [&](const Task& t) { return t.id == id; });
  // The status shown right now, queued ops included.
  std::string shown = task != s.tasks.end() ? task->status : "";
  auto queued = std::find_if(s.queue.begin(), s.queue.end(), [&](const PendingOp& o) { return o.id == id; });
  if (queued != s.queue.end()) shown = queued->status;

  PendingOp op;
  op.id = id;
  op.done = done;
  if (cfg.statusKind != StatusKind::Checkbox) {
    if (done) {
      op.status = doneStatus(cfg);
    } else {
      const Remembered* r = findRemembered(s, id);
      op.status = restoreStatus(cfg, r ? r->previous : "");
    }
  }

  if (done && !isDoneValue(cfg, shown) && !shown.empty()) {
    s.remembered.erase(std::remove_if(s.remembered.begin(), s.remembered.end(),
                                      [&](const Remembered& r) { return r.id == id; }),
                       s.remembered.end());
    s.remembered.push_back({id, shown, today});
  }

  // Undoing an op Notion never saw cancels it rather than queueing a second.
  const std::string synced = task != s.tasks.end() ? task->status : "";
  const bool backToSynced = task != s.tasks.end() && task->done == done &&
                            (cfg.statusKind == StatusKind::Checkbox || synced == op.status);
  if (queued != s.queue.end()) s.queue.erase(queued);
  if (!backToSynced) s.queue.push_back(op);
}

std::vector<Task> effectiveTasks(const State& s, const Config& cfg, const std::string& today) {
  std::vector<Task> out = s.tasks;
  for (const PendingOp& op : s.queue) {
    for (Task& t : out)
      if (t.id == op.id) applyOp(t, op, cfg, today);
  }
  return out;
}

void ackOps(State& s, const std::vector<std::string>& sentIds) {
  s.queue.erase(std::remove_if(s.queue.begin(), s.queue.end(),
                               [&](const PendingOp& op) {
                                 return std::find(sentIds.begin(), sentIds.end(), op.id) != sentIds.end();
                               }),
                s.queue.end());
}

void pruneRemembered(State& s, const Config& cfg, const std::string& today) {
  // Keep a day past the bin's window, so a task still showing there always
  // has its memory.
  const std::string oldest = addDays(today, -(cfg.completedDays + 1));
  s.remembered.erase(std::remove_if(s.remembered.begin(), s.remembered.end(),
                                    [&](const Remembered& r) { return r.date < oldest; }),
                     s.remembered.end());
}

}  // namespace tasks
