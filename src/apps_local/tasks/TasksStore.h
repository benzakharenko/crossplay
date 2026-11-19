#pragma once

// The Tasks app's files on the SD card.
//
//   /.crosspoint/tasks.json        settings: Notion token, database link, ...
//   /.crosspoint/tasks/state.json  last task list, queued check-offs, memories
//   /.crosspoint/tasks/roots.pem   optional CA bundle replacing the baked one
//
// tasks.json may also be uploaded to the top of the card (/tasks.json) through
// File Transfer, which does not show hidden folders. The app moves it into
// /.crosspoint/ the first time it opens, so the token is not left lying in the
// card's top-level listing.

#include <string>

#include "TasksCore.h"
#include "TasksState.h"

namespace tasks::store {

constexpr const char* kConfigPath = "/.crosspoint/tasks.json";
constexpr const char* kUploadedConfigPath = "/tasks.json";
constexpr const char* kDir = "/.crosspoint/tasks";
constexpr const char* kStatePath = "/.crosspoint/tasks/state.json";
constexpr const char* kRootsPath = "/.crosspoint/tasks/roots.pem";

enum class ConfigStatus { Ok, Missing, Invalid };

// Loads tasks.json (adopting an uploaded /tasks.json first). On Invalid, err
// says what to fix.
ConfigStatus loadConfig(Config& out, std::string& err);

// A missing or unreadable state file gives an empty state (first run).
State loadState();
// Writes through a temp file, so a power cut never leaves half a file.
bool saveState(const State& s);

// Today in local time as YYYY-MM-DD, or empty while the clock is not yet set
// (a fresh boot before Wi-Fi has synced it). Lists need a real date.
std::string today();

}  // namespace tasks::store
