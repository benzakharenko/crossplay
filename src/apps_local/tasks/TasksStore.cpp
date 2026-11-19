#include "TasksStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <ctime>

#include "../ClockFloor.h"

namespace tasks::store {

namespace {

constexpr const char* kTag = "TASKS";
constexpr size_t kMaxConfigBytes = 8 * 1024;
constexpr size_t kMaxStateBytes = 1024 * 1024;
constexpr const char* kStateTmpPath = "/.crosspoint/tasks/state.json.tmp";

}  // namespace

ConfigStatus loadConfig(Config& out, std::string& err) {
  if (Storage.exists(kUploadedConfigPath)) {
    Storage.ensureDirectoryExists("/.crosspoint");
    // A newer upload replaces the settings already in place.
    if (Storage.exists(kConfigPath)) Storage.remove(kConfigPath);
    if (Storage.rename(kUploadedConfigPath, kConfigPath)) {
      LOG_INF(kTag, "adopted uploaded tasks.json");
    } else {
      LOG_ERR(kTag, "could not move /tasks.json into /.crosspoint");
    }
  }
  std::string json;
  const char* path = Storage.exists(kConfigPath) ? kConfigPath : kUploadedConfigPath;
  if (!Storage.exists(path)) return ConfigStatus::Missing;
  if (!Storage.readFileToString(kTag, path, kMaxConfigBytes, json)) {
    err = "tasks.json could not be read";
    return ConfigStatus::Invalid;
  }
  if (!parseConfig(json, out, err)) return ConfigStatus::Invalid;
  return ConfigStatus::Ok;
}

State loadState() {
  State s;
  std::string json;
  if (!Storage.exists(kStatePath)) return s;
  if (!Storage.readFileToString(kTag, kStatePath, kMaxStateBytes, json) || !decodeState(json, s)) {
    LOG_ERR(kTag, "state.json unreadable; starting fresh");
  }
  return s;
}

bool saveState(const State& s) {
  Storage.ensureDirectoryExists(kDir);
  const std::string json = encodeState(s);
  if (!Storage.writeFile(kStateTmpPath, String(json.c_str()))) {
    LOG_ERR(kTag, "could not write state");
    return false;
  }
  if (!Storage.replaceFile(kStateTmpPath, kStatePath)) {
    LOG_ERR(kTag, "could not replace state");
    return false;
  }
  return true;
}

std::string today() {
  const time_t now = time(nullptr);
  if (!clockfloor::trusted(static_cast<int64_t>(now))) return "";
  struct tm parts;
  localtime_r(&now, &parts);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
  return buf;
}

}  // namespace tasks::store
