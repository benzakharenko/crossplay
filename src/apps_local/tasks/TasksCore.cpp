#include "TasksCore.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace tasks {

namespace {

const char* statusKey(StatusKind k) {
  switch (k) {
    case StatusKind::Select:
      return "select";
    case StatusKind::Checkbox:
      return "checkbox";
    case StatusKind::Status:
    default:
      return "status";
  }
}

// Cuts s to at most max bytes without splitting a UTF-8 sequence.
bool clipUtf8(std::string& s, size_t max) {
  if (s.size() <= max) return false;
  size_t cut = max;
  while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
  s.resize(cut);
  return true;
}

std::string joinPlainText(JsonArrayConst parts) {
  std::string out;
  for (JsonObjectConst part : parts) {
    const char* t = part["plain_text"] | "";
    out += t;
  }
  return out;
}

// Text of any property value Notion returns, whatever its type. Lets the
// config name a property without saying what type it is.
std::string propText(JsonObjectConst prop) {
  const char* type = prop["type"] | "";
  if (!strcmp(type, "title") || !strcmp(type, "rich_text")) return joinPlainText(prop[type]);
  if (!strcmp(type, "select") || !strcmp(type, "status")) return prop[type]["name"] | "";
  if (!strcmp(type, "multi_select")) {
    std::string out;
    for (JsonObjectConst o : prop["multi_select"].as<JsonArrayConst>()) {
      if (!out.empty()) out += ", ";
      out += o["name"] | "";
    }
    return out;
  }
  if (!strcmp(type, "date")) {
    std::string start = prop["date"]["start"] | "";
    return start.substr(0, 10);
  }
  if (!strcmp(type, "checkbox")) return prop["checkbox"].as<bool>() ? "true" : "false";
  if (!strcmp(type, "number")) {
    if (prop["number"].isNull()) return "";
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", prop["number"].as<double>());
    return buf;
  }
  if (!strcmp(type, "formula")) {
    JsonObjectConst f = prop["formula"];
    const char* ft = f["type"] | "";
    if (!strcmp(ft, "string")) return f["string"] | "";
    if (!strcmp(ft, "boolean")) return f["boolean"].as<bool>() ? "true" : "false";
    if (!strcmp(ft, "date")) return std::string(f["date"]["start"] | "").substr(0, 10);
    if (!strcmp(ft, "number") && !f["number"].isNull()) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%g", f["number"].as<double>());
      return buf;
    }
  }
  return "";
}

bool readStringArray(JsonVariantConst v, std::vector<std::string>& out) {
  if (v.is<const char*>()) {
    out = {v.as<const char*>()};
    return true;
  }
  if (!v.is<JsonArrayConst>()) return false;
  std::vector<std::string> tmp;
  for (JsonVariantConst e : v.as<JsonArrayConst>()) {
    if (!e.is<const char*>()) return false;
    tmp.emplace_back(e.as<const char*>());
  }
  out = std::move(tmp);
  return true;
}

void propFilter(std::string& out, const std::string& prop, const char* type, const char* op,
                const std::string& value, bool quoted = true) {
  out += "{\"property\":\"" + jsonEscape(prop) + "\",\"" + type + "\":{\"" + op + "\":";
  out += quoted ? "\"" + jsonEscape(value) + "\"" : value;
  out += "}}";
}

std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// Digits only; callers check the format first.
int num(const std::string& s, size_t pos, size_t len) {
  int v = 0;
  for (size_t i = pos; i < pos + len; ++i) v = v * 10 + (s[i] - '0');
  return v;
}

int daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

void civilFromDays(int z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

}  // namespace

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

bool parseConfig(const std::string& json, Config& out, std::string& err) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    err = "tasks.json is not valid JSON";
    return false;
  }
  Config cfg;
  cfg.token = doc["token"] | "";
  cfg.dataSourceId = doc["data_source_id"] | "";
  cfg.databaseRef = doc["database"] | "";
  if (cfg.token.empty()) {
    err = "tasks.json: token is missing";
    return false;
  }
  if (cfg.dataSourceId.empty() && cfg.databaseRef.empty()) {
    err = "tasks.json: add your database link as \"database\"";
    return false;
  }
  if (cfg.dataSourceId.empty() && extractNotionId(cfg.databaseRef).empty()) {
    err = "tasks.json: \"database\" is not a Notion database link";
    return false;
  }
  JsonObjectConst p = doc["properties"];
  auto prop = [&](const char* key, std::string& field) {
    if (p[key].is<const char*>() && *p[key].as<const char*>()) field = p[key].as<const char*>();
  };
  prop("title", cfg.props.title);
  prop("category", cfg.props.category);
  prop("due", cfg.props.due);
  prop("status", cfg.props.status);
  prop("priority", cfg.props.priority);
  prop("notes", cfg.props.notes);
  prop("parent", cfg.props.parent);

  std::string kind = doc["status_type"] | "status";
  if (kind == "status") {
    cfg.statusKind = StatusKind::Status;
  } else if (kind == "select") {
    cfg.statusKind = StatusKind::Select;
  } else if (kind == "checkbox") {
    cfg.statusKind = StatusKind::Checkbox;
  } else {
    err = "tasks.json: status_type must be status, select or checkbox";
    return false;
  }
  if (!doc["done"].isNull() && (!readStringArray(doc["done"], cfg.doneValues) || cfg.doneValues.empty())) {
    err = "tasks.json: done must be a name or a list of names";
    return false;
  }
  if (doc["open"].is<const char*>()) cfg.openValue = doc["open"].as<const char*>();
  if (!doc["completed_days"].isNull()) {
    if (!doc["completed_days"].is<int>() || doc["completed_days"].as<int>() < 0 ||
        doc["completed_days"].as<int>() > 30) {
      err = "tasks.json: completed_days must be a number from 0 to 30";
      return false;
    }
    cfg.completedDays = doc["completed_days"].as<int>();
  }
  if (!doc["priority_order"].isNull() && !readStringArray(doc["priority_order"], cfg.priorityOrder)) {
    err = "tasks.json: priority_order must be a list of names";
    return false;
  }
  out = std::move(cfg);
  return true;
}

std::string queryPath(const Config& cfg) {
  std::string path = "/v1/data_sources/" + urlEncode(cfg.dataSourceId) + "/query";
  const std::string* names[] = {&cfg.props.title,    &cfg.props.category, &cfg.props.due,   &cfg.props.status,
                                &cfg.props.priority, &cfg.props.notes,    &cfg.props.parent};
  char sep = '?';
  for (const std::string* n : names) {
    if (n->empty()) continue;  // an optional column this database lacks
    path += sep;
    path += "filter_properties%5B%5D=" + urlEncode(*n);
    sep = '&';
  }
  return path;
}

namespace {

// Conditions (without brackets) saying the task is not done. Notion allows
// filters nested two levels deep, so these are spliced into an existing "and".
std::string notDoneConditions(const Config& cfg) {
  std::string out;
  if (cfg.statusKind == StatusKind::Checkbox) {
    propFilter(out, cfg.props.status, "checkbox", "equals", "false", false);
    return out;
  }
  for (size_t i = 0; i < cfg.doneValues.size(); ++i) {
    if (i) out += ",";
    propFilter(out, cfg.props.status, statusKey(cfg.statusKind), "does_not_equal", cfg.doneValues[i]);
  }
  return out;
}

std::string isDoneCondition(const Config& cfg) {
  std::string out;
  if (cfg.statusKind == StatusKind::Checkbox) {
    propFilter(out, cfg.props.status, "checkbox", "equals", "true", false);
  } else {
    propFilter(out, cfg.props.status, statusKey(cfg.statusKind), "equals", cfg.doneValues.front());
  }
  return out;
}

std::string tail(const Config& cfg, const std::string& cursor, int pageSize) {
  std::string b = ",\"sorts\":[{\"property\":\"" + jsonEscape(cfg.props.due) + "\",\"direction\":\"ascending\"}]";
  b += ",\"page_size\":" + std::to_string(pageSize);
  if (!cursor.empty()) b += ",\"start_cursor\":\"" + jsonEscape(cursor) + "\"";
  b += "}";
  return b;
}

}  // namespace

std::string queryBody(const Config& cfg, const std::string& today, const std::string& tomorrow,
                      const std::string& cursor, int pageSize) {
  const std::string& due = cfg.props.due;
  std::string b = "{\"filter\":{\"or\":[";
  propFilter(b, due, "date", "equals", today);
  b += ",";
  propFilter(b, due, "date", "equals", tomorrow);
  b += ",{\"and\":[";
  propFilter(b, due, "date", "before", today);
  b += "," + notDoneConditions(cfg) + "]}";
  if (cfg.completedDays > 0) {
    // The Completed bin. Only the first done value is matched here: a second
    // "or" would nest a third level, which Notion refuses.
    b += ",{\"and\":[" + isDoneCondition(cfg) +
         ",{\"timestamp\":\"last_edited_time\",\"last_edited_time\":{\"on_or_after\":\"" +
         addDays(today, -cfg.completedDays) + "\"}}]}";
  }
  b += "]}";
  return b + tail(cfg, cursor, pageSize);
}

std::string childrenQueryBody(const Config& cfg, const std::vector<std::string>& parentIds,
                              const std::string& cursor, int pageSize) {
  std::string b = "{\"filter\":{\"or\":[";
  const size_t n = std::min(parentIds.size(), kMaxParentsPerQuery);
  for (size_t i = 0; i < n; ++i) {
    if (i) b += ",";
    propFilter(b, cfg.props.parent, "relation", "contains", parentIds[i]);
  }
  b += "]}";
  return b + tail(cfg, cursor, pageSize);
}

bool parseQueryResponse(const std::string& body, const Config& cfg, QueryPage& out, std::string& err) {
  // Keep only what the app reads, so a reply full of page metadata (icons,
  // covers, users, URLs) never lands in memory.
  JsonDocument filter;
  filter["object"] = true;
  filter["message"] = true;
  filter["has_more"] = true;
  filter["next_cursor"] = true;
  JsonObject item = filter["results"].add<JsonObject>();
  item["id"] = true;
  item["in_trash"] = true;
  item["archived"] = true;
  item["last_edited_time"] = true;
  for (const std::string* n : {&cfg.props.title, &cfg.props.category, &cfg.props.due, &cfg.props.status,
                               &cfg.props.priority, &cfg.props.notes, &cfg.props.parent}) {
    if (n->empty()) continue;
    item["properties"][*n] = true;
  }

  JsonDocument doc;
  if (DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = std::string("Bad reply from Notion (") + e.c_str() + ")";
    return false;
  }
  if (doc["object"] == "error") {
    err = doc["message"] | "Notion returned an error";
    return false;
  }
  if (!doc["results"].is<JsonArrayConst>()) {
    err = "Bad reply from Notion (no results)";
    return false;
  }

  QueryPage page;
  JsonArrayConst results = doc["results"];
  page.tasks.reserve(results.size());
  for (JsonObjectConst r : results) {
    if (r["in_trash"].as<bool>() || r["archived"].as<bool>()) continue;
    JsonObjectConst props = r["properties"];
    Task t;
    t.id = r["id"] | "";
    if (t.id.empty()) continue;
    auto prop = [&](const std::string& name) -> JsonObjectConst {
      return name.empty() ? JsonObjectConst() : props[name].as<JsonObjectConst>();
    };
    t.title = propText(prop(cfg.props.title));
    clipUtf8(t.title, kMaxTitleBytes);
    t.category = propText(prop(cfg.props.category));
    t.due = propText(prop(cfg.props.due));
    t.status = propText(prop(cfg.props.status));
    t.priority = propText(prop(cfg.props.priority));
    t.notes = propText(prop(cfg.props.notes));
    t.notesTruncated = clipUtf8(t.notes, kMaxNotesBytes);
    t.parentId = prop(cfg.props.parent)["relation"][0]["id"] | "";
    t.lastEdited = std::string(r["last_edited_time"] | "").substr(0, 10);
    t.done = isDoneValue(cfg, t.status);
    page.tasks.push_back(std::move(t));
  }
  page.hasMore = doc["has_more"].as<bool>();
  page.nextCursor = doc["next_cursor"] | "";
  out = std::move(page);
  return true;
}

std::string updatePath(const std::string& pageId) { return "/v1/pages/" + urlEncode(pageId); }

std::string updateBody(const Config& cfg, bool done, const std::string& statusName) {
  std::string b = "{\"properties\":{\"" + jsonEscape(cfg.props.status) + "\":";
  if (cfg.statusKind == StatusKind::Checkbox) {
    b += done ? "{\"checkbox\":true}" : "{\"checkbox\":false}";
  } else {
    const std::string& name = !statusName.empty() ? statusName : (done ? doneStatus(cfg) : cfg.openValue);
    b += std::string("{\"") + statusKey(cfg.statusKind) + "\":{\"name\":\"" + jsonEscape(name) + "\"}}";
  }
  b += "}}";
  return b;
}

const std::string& doneStatus(const Config& cfg) { return cfg.doneValues.front(); }

std::string restoreStatus(const Config& cfg, const std::string& previous) {
  if (previous.empty() || isDoneValue(cfg, previous)) return cfg.openValue;
  return previous;
}

std::string extractNotionId(const std::string& linkOrId) {
  // Drop the query (?v=<view id>) and fragment, then take the last run of 32
  // hex digits, dashed or not.
  std::string s = linkOrId.substr(0, linkOrId.find_first_of("?#"));
  std::string best;
  std::string run;
  auto flush = [&] {
    if (run.size() == 32) best = run;
    run.clear();
  };
  for (char c : s) {
    if (std::isxdigit(static_cast<unsigned char>(c))) {
      run += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (run.size() > 32) run.erase(0, 1);  // keep the trailing 32 of a longer run
    } else if (c == '-' && !run.empty() && run.size() < 32) {
      continue;  // dashes inside a UUID
    } else {
      flush();
    }
  }
  flush();
  if (best.empty()) return "";
  return best.substr(0, 8) + "-" + best.substr(8, 4) + "-" + best.substr(12, 4) + "-" + best.substr(16, 4) + "-" +
         best.substr(20);
}

std::string databasePath(const std::string& databaseId) { return "/v1/databases/" + urlEncode(databaseId); }

bool parseDatabaseReply(const std::string& body, std::string& dataSourceId, std::string& err) {
  JsonDocument filter;
  filter["object"] = true;
  filter["message"] = true;
  filter["data_sources"][0]["id"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "Bad reply from Notion";
    return false;
  }
  if (doc["object"] == "error") {
    err = doc["message"] | "Notion returned an error";
    return false;
  }
  const char* id = doc["data_sources"][0]["id"] | "";
  if (!*id) {
    err = "That database has no data source the integration can see";
    return false;
  }
  dataSourceId = id;
  return true;
}

std::string dataSourcePath(const std::string& dataSourceId) { return "/v1/data_sources/" + urlEncode(dataSourceId); }

namespace {

std::string squash(const std::string& s) {
  std::string out;
  for (unsigned char c : s)
    if (!std::isspace(c)) out += static_cast<char>(std::tolower(c));
  return out;
}

struct Column {
  std::string name;
  std::string type;
};

}  // namespace

bool resolveProperties(const std::string& schemaBody, Config& cfg, std::string& err) {
  JsonDocument filter;
  filter["object"] = true;
  filter["message"] = true;
  filter["properties"]["*"]["name"] = true;
  filter["properties"]["*"]["type"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, schemaBody, DeserializationOption::Filter(filter))) {
    err = "Bad reply from Notion";
    return false;
  }
  if (doc["object"] == "error") {
    err = doc["message"] | "Notion returned an error";
    return false;
  }
  std::vector<Column> cols;
  for (JsonPairConst kv : doc["properties"].as<JsonObjectConst>()) {
    // The key is the property's name; "name" repeats it on current API versions.
    cols.push_back({kv.value()["name"] | kv.key().c_str(), kv.value()["type"] | ""});
  }
  if (cols.empty()) {
    err = "Notion returned no columns for the database";
    return false;
  }

  // types: the column types this role accepts; hint: a word the name should
  // contain when picking by type among several.
  // byTypeAlone: a required role may take the only column of its type whatever
  // it is called; an optional one may not (the only select left could be any).
  auto find = [&](const std::string& wanted, std::initializer_list<const char*> types, const char* hint,
                  const bool byTypeAlone) -> const Column* {
    auto typed = [&](const Column& c) {
      for (const char* t : types)
        if (c.type == t) return true;
      return false;
    };
    for (const Column& c : cols)
      if (c.name == wanted && typed(c)) return &c;
    for (const Column& c : cols)
      if (squash(c.name) == squash(wanted) && typed(c)) return &c;
    const Column* only = nullptr;
    int count = 0;
    for (const Column& c : cols) {
      if (!typed(c)) continue;
      ++count;
      only = &c;
    }
    if (byTypeAlone && count == 1) return only;
    if (hint != nullptr) {
      const Column* hinted = nullptr;
      int hits = 0;
      for (const Column& c : cols) {
        if (!typed(c) || squash(c.name).find(hint) == std::string::npos) continue;
        // "Due Date Needed?" is a question, not the date: prefer names without one.
        if (hinted == nullptr || (hinted->name.find('?') != std::string::npos && c.name.find('?') == std::string::npos))
          hinted = &c;
        ++hits;
      }
      if (hits > 0) return hinted;
    }
    return nullptr;
  };

  Config out = cfg;
  const Column* title = find(cfg.props.title, {"title"}, nullptr, true);
  const Column* due = find(cfg.props.due, {"date"}, "due", true);
  const Column* status = find(cfg.props.status, {"status", "select", "checkbox"}, "status", true);
  if (title == nullptr) {
    err = "The database has no title column";
    return false;
  }
  if (due == nullptr) {
    err = "No date column named \"" + cfg.props.due + "\". Set properties.due in tasks.json.";
    return false;
  }
  if (status == nullptr) {
    err = "No status column named \"" + cfg.props.status + "\". Set properties.status in tasks.json.";
    return false;
  }
  out.props.title = title->name;
  out.props.due = due->name;
  out.props.status = status->name;
  out.statusKind = status->type == "checkbox" ? StatusKind::Checkbox
                   : status->type == "select" ? StatusKind::Select
                                              : StatusKind::Status;
  auto optional = [&](std::string& field, std::initializer_list<const char*> types, const char* hint) {
    const Column* c = find(field, types, hint, false);
    field = c != nullptr ? c->name : "";
  };
  optional(out.props.category, {"select", "multi_select", "status", "rich_text", "formula"}, "category");
  optional(out.props.priority, {"select", "status", "multi_select", "number", "formula"}, "priority");
  optional(out.props.notes, {"rich_text"}, "note");
  optional(out.props.parent, {"relation"}, "parent");
  cfg = std::move(out);
  return true;
}

std::string errorMessage(const std::string& body) {
  JsonDocument filter;
  filter["object"] = true;
  filter["message"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return "";
  if (doc["object"] != "error") return "";
  return doc["message"] | "";
}

bool isValidDate(const std::string& ymd) {
  if (ymd.size() != 10 || ymd[4] != '-' || ymd[7] != '-') return false;
  for (size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
    if (ymd[i] < '0' || ymd[i] > '9') return false;
  }
  const int y = num(ymd, 0, 4);
  const unsigned m = static_cast<unsigned>(num(ymd, 5, 2));
  const unsigned d = static_cast<unsigned>(num(ymd, 8, 2));
  if (m < 1 || m > 12 || d < 1) return false;
  static constexpr unsigned kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  const unsigned dim = (m == 2 && leap) ? 29 : kDays[m - 1];
  return d <= dim;
}

std::string addDays(const std::string& ymd, int days) {
  if (!isValidDate(ymd)) return "";
  int y = num(ymd, 0, 4);
  unsigned m = static_cast<unsigned>(num(ymd, 5, 2));
  unsigned d = static_cast<unsigned>(num(ymd, 8, 2));
  civilFromDays(daysFromCivil(y, m, d) + days, y, m, d);
  if (y < 0 || y > 9999) return "";
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d);
  return buf;
}

Bucket bucketFor(const Task& t, const Config& cfg, const std::string& today, const std::string& tomorrow) {
  if (t.done) {
    const std::string since = addDays(today, -cfg.completedDays);
    const bool recent = cfg.completedDays > 0 && !t.lastEdited.empty() && t.lastEdited >= since;
    return (recent || t.due == today || t.due == tomorrow) ? Bucket::Completed : Bucket::Other;
  }
  if (t.due.empty()) return Bucket::Other;
  if (t.due == today) return Bucket::Today;
  if (t.due == tomorrow) return Bucket::Tomorrow;
  // ISO dates compare correctly as strings.
  if (t.due < today) return Bucket::Missed;
  return Bucket::Other;
}

bool isDoneValue(const Config& cfg, const std::string& status) {
  if (cfg.statusKind == StatusKind::Checkbox) return status == "true";
  return std::find(cfg.doneValues.begin(), cfg.doneValues.end(), status) != cfg.doneValues.end();
}

void sortTasks(std::vector<Task>& v, const Config& cfg) {
  auto rank = [&](const std::string& p) {
    auto it = std::find(cfg.priorityOrder.begin(), cfg.priorityOrder.end(), p);
    return static_cast<size_t>(it - cfg.priorityOrder.begin());
  };
  std::stable_sort(v.begin(), v.end(), [&](const Task& a, const Task& b) {
    if (a.done != b.done) return !a.done;
    const size_t ra = rank(a.priority), rb = rank(b.priority);
    if (ra != rb) return ra < rb;
    if (a.due != b.due) return a.due < b.due;
    return a.title < b.title;
  });
}

}  // namespace tasks

namespace tasks {

View buildView(const std::vector<Task>& all, const Config& cfg, const std::string& today) {
  const std::string tomorrow = addDays(today, 1);
  std::vector<const Task*> uniq;
  uniq.reserve(all.size());
  for (const Task& t : all) {
    bool seen = false;
    for (const Task* u : uniq) seen = seen || u->id == t.id;
    if (!seen) uniq.push_back(&t);
  }
  auto indexOfId = [&](const std::string& id) -> int {
    for (size_t i = 0; i < uniq.size(); ++i)
      if (uniq[i]->id == id) return static_cast<int>(i);
    return -1;
  };
  std::vector<Bucket> own(uniq.size());
  for (size_t i = 0; i < uniq.size(); ++i) {
    own[i] = bucketFor(*uniq[i], cfg, today, tomorrow);
    // An open sub-item with no date of its own is due when its parent is,
    // even after the parent is checked off: it stays in that day's list
    // rather than vanishing with it.
    if (own[i] != Bucket::Other || uniq[i]->done || !uniq[i]->due.empty()) continue;
    int p = indexOfId(uniq[i]->parentId);
    for (int hops = 0; p >= 0 && hops < 8; ++hops) {
      if (!uniq[p]->due.empty()) {
        Task dated = *uniq[i];
        dated.due = uniq[p]->due;
        own[i] = bucketFor(dated, cfg, today, tomorrow);
        break;
      }
      p = indexOfId(uniq[p]->parentId);
    }
  }

  auto isList = [](Bucket b) { return b == Bucket::Missed || b == Bucket::Today || b == Bucket::Tomorrow; };
  auto indexOf = [&](const std::string& id) -> int {
    for (size_t i = 0; i < uniq.size(); ++i)
      if (uniq[i]->id == id) return static_cast<int>(i);
    return -1;
  };

  // A task nests under its parent when both are open and the parent is in a
  // list. Following parents up resolves which list a nested task lands in.
  std::vector<int> parentOf(uniq.size(), -1);
  for (size_t i = 0; i < uniq.size(); ++i) {
    if (uniq[i]->done || uniq[i]->parentId.empty()) continue;
    const int p = indexOf(uniq[i]->parentId);
    if (p >= 0 && p != static_cast<int>(i) && !uniq[p]->done) parentOf[i] = p;
  }
  // Bad data can make a task its own ancestor; break the loop where found so
  // nothing in it disappears.
  for (size_t i = 0; i < uniq.size(); ++i) {
    int cur = parentOf[i];
    for (int hops = 0; cur >= 0 && hops < 16; ++hops) {
      if (cur == static_cast<int>(i)) {
        parentOf[i] = -1;
        break;
      }
      cur = parentOf[cur];
    }
  }
  auto rootOf = [&](size_t i) -> int {
    int cur = static_cast<int>(i);
    for (int hops = 0; parentOf[cur] >= 0 && hops < 8; ++hops) cur = parentOf[cur];
    return cur;
  };
  for (size_t i = 0; i < uniq.size(); ++i) {
    if (parentOf[i] >= 0 && !isList(own[rootOf(i)])) parentOf[i] = -1;  // parent not shown
  }

  View v;
  auto emit = [&](std::vector<Row>& out, Bucket b) {
    std::vector<Task> tops;
    for (size_t i = 0; i < uniq.size(); ++i)
      if (parentOf[i] < 0 && own[i] == b) tops.push_back(*uniq[i]);
    sortTasks(tops, cfg);
    // Depth-first: each task, then its children sorted the same way.
    struct Frame {
      std::vector<Task> kids;
      size_t next;
      int depth;
    };
    std::vector<Frame> stack;
    stack.push_back({tops, 0, 0});
    while (!stack.empty()) {
      Frame& f = stack.back();
      if (f.next >= f.kids.size()) {
        stack.pop_back();
        continue;
      }
      const Task t = f.kids[f.next++];
      const int depth = f.depth;
      out.push_back({t, depth});
      std::vector<Task> kids;
      for (size_t i = 0; i < uniq.size(); ++i)
        if (parentOf[i] >= 0 && uniq[parentOf[i]]->id == t.id) kids.push_back(*uniq[i]);
      if (!kids.empty() && depth < 8) {
        sortTasks(kids, cfg);
        stack.push_back({std::move(kids), 0, depth + 1});
      }
    }
  };
  emit(v.missed, Bucket::Missed);
  emit(v.today, Bucket::Today);
  emit(v.tomorrow, Bucket::Tomorrow);

  for (size_t i = 0; i < uniq.size(); ++i)
    if (own[i] == Bucket::Completed) v.completed.push_back({*uniq[i], 0});
  std::stable_sort(v.completed.begin(), v.completed.end(), [](const Row& a, const Row& b) {
    if (a.task.lastEdited != b.task.lastEdited) return a.task.lastEdited > b.task.lastEdited;
    return a.task.title < b.task.title;
  });
  return v;
}

}  // namespace tasks
