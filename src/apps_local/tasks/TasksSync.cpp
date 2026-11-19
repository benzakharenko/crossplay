#include "TasksSync.h"

#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#if defined(FREEINK_NET_WOLFSSL)
#include <Arduino.h>
#include <HalStorage.h>
#include <SecureHttpClient.h>

#include "TasksRoots.h"
#include "TasksStore.h"
#include "network/WifiPowerSaveGuard.h"
#else
#include <unistd.h>

#include <fstream>
#include <sstream>
#endif

namespace tasks {

namespace {

constexpr const char* kTag = "TASKS";
// A page of 100 tasks with six properties each is ~150 KB; the cap only stops
// a runaway reply. Large buffers come from PSRAM on the S3.
constexpr size_t kMaxReplyBytes = 1024 * 1024;
// Bounds on paging, so a huge database cannot hold the loop for minutes.
constexpr int kMaxMainPages = 5;
constexpr int kMaxChildPages = 3;
constexpr int kChildRounds = 2;  // sub-items, then their sub-items

#if defined(FREEINK_NET_WOLFSSL)

const char* caRoots() {
  static std::string sdRoots;
  static bool probed = false;
  if (!probed) {
    probed = true;
    std::string pem;
    if (Storage.exists(store::kRootsPath) && Storage.readFileToString(kTag, store::kRootsPath, 64 * 1024, pem) &&
        pem.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
      sdRoots = std::move(pem);
      LOG_INF(kTag, "using CA bundle from the card");
    }
  }
  return sdRoots.empty() ? kNotionCaRoots : sdRoots.c_str();
}

#endif

}  // namespace

const char* syncResultName(SyncResult r) {
  switch (r) {
    case SyncResult::Ok:
      return "ok";
    case SyncResult::Aborted:
      return "aborted";
    case SyncResult::NoClock:
      return "no-clock";
    case SyncResult::Unreachable:
      return "unreachable";
    case SyncResult::BadToken:
      return "bad-token";
    case SyncResult::NotShared:
      return "not-shared";
    case SyncResult::RateLimited:
      return "rate-limited";
    case SyncResult::Failed:
    default:
      return "failed";
  }
}

#if defined(FREEINK_NET_WOLFSSL)

int TasksSync::call(const char* method, const std::string& path, const std::string& body, std::string& out,
                    const Abort& shouldAbort) {
  out.clear();
  if (ESP.getFreeHeap() < 35000 || ESP.getMaxAllocHeap() < 20000) {
    LOG_ERR(kTag, "heap too low for TLS: free=%u block=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return 0;
  }
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient http;
  http.setCACert(caRoots());
  http.setTimeout(30000);
  http.setUserAgent("CrossPlay-Tasks");
  if (!http.begin(std::string("https://") + kNotionHost + path)) return 0;
  if (!http.connect(shouldAbort)) {
    LOG_ERR(kTag, "%s %s: connect/TLS failed", method, path.substr(0, path.find('?')).c_str());
    return shouldAbort && shouldAbort() ? -1 : 0;
  }
  http.addHeader("Authorization", "Bearer " + cfg_.token);
  http.addHeader("Notion-Version", kNotionVersion);
  if (!body.empty()) http.addHeader("Content-Type", "application/json");
  bool overflow = false;
  const int status = http.sendRequest(
      method, reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        if (out.size() + len > kMaxReplyBytes) {
          overflow = true;
          return false;
        }
        out.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      shouldAbort);
  const bool complete = http.responseComplete();
  http.end();
  // The path, never the headers: the token must not reach a log.
  LOG_INF(kTag, "%s %s -> %d, %u bytes", method, path.substr(0, path.find('?')).c_str(), status,
          static_cast<unsigned>(out.size()));
  if (shouldAbort && shouldAbort()) return -1;
  if (overflow || status <= 0 || !complete) return 0;
  return status;
}

#else  // simulator

int TasksSync::call(const char* method, const std::string& path, const std::string& body, std::string& out,
                    const Abort& shouldAbort) {
  out.clear();
  if (shouldAbort && shouldAbort()) return -1;
  if (const char* fake = std::getenv("TASKS_FAKE_DIR")) {
    // Canned replies: databases.json, query.json, children.json; PATCHes succeed.
    std::string name = "patch.json";
    if (path.rfind("/v1/databases/", 0) == 0) name = "database.json";
    if (path.rfind("/v1/data_sources/", 0) == 0) {
      // datasource-<id>.json when the fixture has one for that table.
      const std::string id = path.substr(std::string("/v1/data_sources/").size());
      std::ifstream specific(std::string(fake) + "/datasource-" + id + ".json");
      name = specific ? "datasource-" + id + ".json" : "datasource.json";
    }
    if (path.find("/query") != std::string::npos)
      name = body.find("\"relation\"") != std::string::npos ? "children.json" : "query.json";
    if (std::string(method) == "PATCH") return 200;
    std::ifstream f(std::string(fake) + "/" + name);
    if (!f) return 404;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return 200;
  }
  char bodyPath[] = "/tmp/tasks-body-XXXXXX";
  char hdrPath[] = "/tmp/tasks-hdr-XXXXXX";
  char outPath[] = "/tmp/tasks-out-XXXXXX";
  const int fb = mkstemp(bodyPath), fh = mkstemp(hdrPath), fo = mkstemp(outPath);
  if (fb < 0 || fh < 0 || fo < 0) return 0;
  // Headers go through a file so the token is not on a command line.
  const std::string hdrs = "Authorization: Bearer " + cfg_.token + "\nNotion-Version: " + kNotionVersion +
                           "\nContent-Type: application/json\n";
  if (write(fh, hdrs.data(), hdrs.size()) < 0 || write(fb, body.data(), body.size()) < 0) return 0;
  close(fb);
  close(fh);
  close(fo);
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "curl -s -o '%s' -w '%%{http_code}' -X %s -H @'%s' %s 'https://%s%s'", outPath, method,
           hdrPath, body.empty() ? "" : (std::string("--data-binary @") + bodyPath).c_str(), kNotionHost,
           path.c_str());
  int status = 0;
  if (FILE* p = popen(cmd, "r")) {
    if (fscanf(p, "%d", &status) != 1) status = 0;
    pclose(p);
  }
  std::ifstream f(outPath);
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  unlink(bodyPath);
  unlink(hdrPath);
  unlink(outPath);
  LOG_INF(kTag, "%s %s -> %d (sim)", method, path.substr(0, path.find('?')).c_str(), status);
  return status;
}

#endif

SyncOutcome TasksSync::fail(int status, const std::string& body) {
  SyncOutcome o;
  const std::string notion = errorMessage(body);
  if (status == -1) {
    o.result = SyncResult::Aborted;
  } else if (status == 0) {
    o.result = SyncResult::Unreachable;
    o.message = "Could not reach Notion. Check Wi-Fi.";
  } else if (status == 401) {
    o.result = SyncResult::BadToken;
    o.message = "Notion refused the token in tasks.json.";
  } else if (status == 404) {
    o.result = SyncResult::NotShared;
    o.message = "Notion can't see the database. Share it with the connection.";
  } else if (status == 429) {
    o.result = SyncResult::RateLimited;
    o.message = "Notion asked to slow down. Try again in a minute.";
  } else {
    o.result = SyncResult::Failed;
    o.message = notion.empty() ? "Notion returned an error (" + std::to_string(status) + ")." : notion;
  }
  return o;
}

int TasksSync::fitColumns(const std::string& dataSourceId, Config& fitted, std::string& err,
                          const Abort& shouldAbort) {
  std::string reply;
  const int status = call("GET", dataSourcePath(dataSourceId), "", reply, shouldAbort);
  if (status != 200) {
    err = errorMessage(reply);
    return status;
  }
  fitted = cfg_;
  return resolveProperties(reply, fitted, err) ? 200 : 422;
}

bool TasksSync::resolveSource(State& state, SyncOutcome& o, const Abort& shouldAbort) {
  std::vector<std::string> candidates;
  bool fresh = false;
  if (!cfg_.dataSourceId.empty()) {
    candidates = {cfg_.dataSourceId};
  } else if (!state.dataSourceId.empty() && state.databaseRef == cfg_.databaseRef) {
    candidates = {state.dataSourceId};
  } else {
    std::string reply;
    const int status = call("GET", databasePath(extractNotionId(cfg_.databaseRef)), "", reply, shouldAbort);
    if (status != 200) {
      o = fail(status, reply);
      return false;
    }
    std::string err;
    if (!parseDatabaseReply(reply, candidates, err)) {
      o.result = SyncResult::Failed;
      o.message = err;
      return false;
    }
    fresh = true;
  }

  std::string firstErr;
  for (const std::string& id : candidates) {
    Config fitted;
    std::string err;
    const int status = fitColumns(id, fitted, err, shouldAbort);
    if (status == 200) {
      cfg_ = std::move(fitted);
      dataSourceId_ = id;
      state.dataSourceId = id;
      state.databaseRef = cfg_.databaseRef;
      LOG_INF(kTag, "table %s: title='%s' due='%s' status='%s' parent='%s'", id.c_str(), cfg_.props.title.c_str(),
              cfg_.props.due.c_str(), cfg_.props.status.c_str(), cfg_.props.parent.c_str());
      return true;
    }
    if (status != 422) {  // Notion or the network failed, not the columns
      o = fail(status, "");
      if (!err.empty() && status > 0) o.message = err;
      return false;
    }
    LOG_INF(kTag, "table %s does not fit: %s", id.c_str(), err.c_str());
    if (firstErr.empty()) firstErr = err;
  }
  // A cached table that stopped fitting (columns renamed) is forgotten, so the
  // next sync looks through the database's tables again.
  if (!fresh) state.dataSourceId.clear();
  o.result = SyncResult::Failed;
  o.message = candidates.size() > 1 ? "None of the database's " + std::to_string(candidates.size()) +
                                          " tables fit. First: " + firstErr
                                    : firstErr;
  return false;
}

bool TasksSync::sendQueue(State& state, SyncOutcome& o, const Abort& shouldAbort) {
  std::vector<std::string> sent;
  bool ok = true;
  for (const PendingOp& op : state.queue) {
    std::string reply;
    const int status = call("PATCH", updatePath(op.id), updateBody(cfg_, op.done, op.status), reply, shouldAbort);
    if (status == 200 || status == 404) {
      // 404: the page is gone; nothing left to tell Notion.
      sent.push_back(op.id);
      continue;
    }
    if (status == 400) {
      const std::string why = errorMessage(reply);
      LOG_ERR(kTag, "change rejected: %s", why.c_str());
      o.warning = why.empty() ? "Notion rejected a change." : "Notion rejected a change: " + why;
      sent.push_back(op.id);
      continue;
    }
    o = fail(status, reply);
    ok = false;
    break;
  }
  ackOps(state, sent);
  o.sentOps = static_cast<int>(sent.size());
  return ok;
}

bool TasksSync::query(const std::function<std::string(const std::string&)>& build, int maxPages,
                      std::vector<Task>& into, SyncOutcome& o, const Abort& shouldAbort) {
  Config withSource = cfg_;
  withSource.dataSourceId = dataSourceId_;
  const std::string path = queryPath(withSource);
  std::string cursor;
  for (int page = 0; page < maxPages; ++page) {
    std::string reply;
    const int status = call("POST", path, build(cursor), reply, shouldAbort);
    if (status != 200) {
      o = fail(status, reply);
      return false;
    }
    QueryPage qp;
    std::string err;
    if (!parseQueryResponse(reply, cfg_, qp, err)) {
      o.result = SyncResult::Failed;
      o.message = err;
      return false;
    }
    for (Task& t : qp.tasks) into.push_back(std::move(t));
    if (!qp.hasMore || qp.nextCursor.empty()) return true;
    cursor = qp.nextCursor;
  }
  LOG_INF(kTag, "stopped paging after %d pages", maxPages);
  return true;
}

SyncOutcome TasksSync::sync(State& state, const std::string& today, const Abort& shouldAbort) {
  SyncOutcome o;
  if (today.empty()) {
    o.result = SyncResult::NoClock;
    o.message = "The clock isn't set yet. Connect to Wi-Fi once.";
    return o;
  }
  if (!resolveSource(state, o, shouldAbort)) return o;
  if (!sendQueue(state, o, shouldAbort)) return o;
  const int sentOps = o.sentOps;
  const std::string warning = o.warning;

  const std::string tomorrow = addDays(today, 1);
  std::vector<Task> all;
  all.reserve(64);
  const auto mainBody = [&](const std::string& cursor) { return queryBody(cfg_, today, tomorrow, cursor); };
  if (!query(mainBody, kMaxMainPages, all, o, shouldAbort)) return o;

  // Sub-items of every open task shown, then theirs.
  std::vector<std::string> asked;
  size_t from = 0;
  for (int round = 0; round < kChildRounds && !cfg_.props.parent.empty(); ++round) {
    std::vector<std::string> parents;
    for (size_t i = from; i < all.size(); ++i) {
      const Task& t = all[i];
      if (t.done || std::find(asked.begin(), asked.end(), t.id) != asked.end()) continue;
      if (round == 0 && bucketFor(t, cfg_, today, tomorrow) == Bucket::Other) continue;
      parents.push_back(t.id);
    }
    if (parents.empty()) break;
    from = all.size();
    for (size_t at = 0; at < parents.size(); at += kMaxParentsPerQuery) {
      const std::vector<std::string> chunk(
          parents.begin() + static_cast<long>(at),
          parents.begin() + static_cast<long>(std::min(parents.size(), at + kMaxParentsPerQuery)));
      const auto childBody = [&](const std::string& cursor) { return childrenQueryBody(cfg_, chunk, cursor); };
      if (!query(childBody, kMaxChildPages, all, o, shouldAbort)) return o;
      asked.insert(asked.end(), chunk.begin(), chunk.end());
    }
  }

  // A sub-item can come back in both rounds (it is asked for as a child, then
  // its own children are asked for); keep each task once.
  std::vector<Task> unique;
  unique.reserve(all.size());
  for (Task& t : all) {
    if (std::none_of(unique.begin(), unique.end(), [&](const Task& u) { return u.id == t.id; }))
      unique.push_back(std::move(t));
  }
  state.tasks = std::move(unique);
  state.syncedAt = static_cast<int64_t>(time(nullptr));
  pruneRemembered(state, cfg_, today);
  o = SyncOutcome();
  o.result = SyncResult::Ok;
  o.sentOps = sentOps;
  o.warning = warning;
  return o;
}

}  // namespace tasks
