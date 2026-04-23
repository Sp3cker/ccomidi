#include "plugin/voicegroup_bridge.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ccomidi {

namespace {

std::string g_pluginDir;

std::string dirname_of(const char *path) {
  const char *end = path + std::strlen(path);
  while (end > path && *end != '/' && *end != '\\')
    --end;
  return std::string(path, static_cast<size_t>(end - path));
}

void strip_bundle_path(std::string &dir) {
#ifdef __APPLE__
  // plugin_path points at <bundle>.vst3/Contents/MacOS/<binary>; walk up two
  // levels to reach the dir that holds sibling .vst3 bundles.
  static const char kSuffix[] = "/Contents/MacOS";
  const size_t slen = sizeof(kSuffix) - 1;
  if (dir.size() > slen &&
      dir.compare(dir.size() - slen, slen, kSuffix) == 0) {
    dir.resize(dir.size() - slen);
    auto pos = dir.find_last_of('/');
    if (pos != std::string::npos)
      dir.resize(pos);
  }
#else
  (void)dir;
#endif
}

std::string state_path() {
  if (g_pluginDir.empty())
    return {};
  return g_pluginDir + "/poryaaaa_state.json";
}

long long mtime_ns(const std::string &path) {
  struct stat st;
  if (path.empty() || stat(path.c_str(), &st) != 0)
    return 0;
#if defined(__APPLE__)
  return static_cast<long long>(st.st_mtimespec.tv_sec) * 1000000000LL +
         st.st_mtimespec.tv_nsec;
#else
  return static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL +
         st.st_mtim.tv_nsec;
#endif
}

// Decode a JSON string body into dst (without surrounding quotes). Expects src
// to point at the opening '"'; advances src past the closing '"'. Returns true
// on success.
bool parse_json_string(const char *&src, std::string &dst) {
  if (*src != '"')
    return false;
  ++src;
  dst.clear();
  while (*src && *src != '"') {
    if (*src == '\\' && src[1]) {
      dst.push_back(src[1]);
      src += 2;
    } else {
      dst.push_back(*src++);
    }
  }
  if (*src != '"')
    return false;
  ++src;
  return true;
}

// Finds `"<key>"` in src (starting at or after src), advances src past the
// matching colon, and returns true. Skips over any nested strings so the
// search doesn't match keys buried inside string values.
bool find_key(const char *&src, const char *key) {
  const std::string needle = std::string("\"") + key + "\"";
  const char *p = std::strstr(src, needle.c_str());
  if (!p)
    return false;
  p += needle.size();
  while (*p && *p != ':')
    ++p;
  if (*p != ':')
    return false;
  ++p;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    ++p;
  src = p;
  return true;
}

bool read_file_to_string(const std::string &path, std::string &out) {
  std::FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size < 0 || size > 4 * 1024 * 1024) {  // cap at 4MB
    std::fclose(f);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  size_t got = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  return got == out.size();
}

}  // namespace

void voicegroup_bridge_set_plugin_path(const char *pluginPath) {
  if (!pluginPath || !*pluginPath)
    return;
  g_pluginDir = dirname_of(pluginPath);
  strip_bundle_path(g_pluginDir);
}

long long voicegroup_bridge_state_mtime() {
  return mtime_ns(state_path());
}

VoiceSlotLoad voicegroup_bridge_load_state() {
  VoiceSlotLoad result;

  if (g_pluginDir.empty()) {
    result.error = "Plugin directory unknown — CLAP host never called entry_init.";
    return result;
  }

  result.statePath = state_path();
  result.mtimeNs = mtime_ns(result.statePath);
  if (result.mtimeNs == 0) {
    result.error = "poryaaaa hasn't written its state yet — load poryaaaa in the DAW.";
    return result;
  }

  std::string body;
  if (!read_file_to_string(result.statePath, body)) {
    result.error = "Could not read poryaaaa_state.json.";
    return result;
  }

  const char *cursor = body.c_str();
  std::string projectRoot;
  std::string voicegroupName;
  if (find_key(cursor, "projectRoot"))
    parse_json_string(cursor, projectRoot);
  if (find_key(cursor, "voicegroup"))
    parse_json_string(cursor, voicegroupName);

  if (!find_key(cursor, "slots")) {
    result.error = "state.json missing 'slots' array.";
    return result;
  }
  while (*cursor && *cursor != '[')
    ++cursor;
  if (*cursor != '[') {
    result.error = "state.json 'slots' is not an array.";
    return result;
  }
  ++cursor;

  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
           *cursor == '\r' || *cursor == ',')
      ++cursor;
    if (*cursor == ']' || *cursor == '\0')
      break;
    if (*cursor != '{')
      break;

    const char *objStart = cursor;
    int depth = 0;
    const char *objEnd = cursor;
    while (*objEnd) {
      if (*objEnd == '{') ++depth;
      else if (*objEnd == '}') {
        --depth;
        if (depth == 0) { ++objEnd; break; }
      } else if (*objEnd == '"') {
        ++objEnd;
        while (*objEnd && *objEnd != '"') {
          if (*objEnd == '\\' && objEnd[1]) objEnd += 2;
          else ++objEnd;
        }
        if (*objEnd == '"') ++objEnd;
        continue;
      }
      ++objEnd;
    }

    std::string obj(objStart, static_cast<size_t>(objEnd - objStart));
    const char *oc = obj.c_str();
    int program = -1;
    std::string name;
    if (find_key(oc, "program"))
      program = std::atoi(oc);
    oc = obj.c_str();
    if (find_key(oc, "name"))
      parse_json_string(oc, name);

    if (program >= 0 && program < 128 && !name.empty())
      result.slots.push_back(VoiceSlot{program, name});

    cursor = objEnd;
  }

  if (result.slots.empty())
    result.error = voicegroupName.empty()
                       ? std::string("state.json has no slots.")
                       : "Voicegroup '" + voicegroupName +
                             "' has no sample-bearing slots.";

  return result;
}

}  // namespace ccomidi
