#include "transport.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

namespace {

// Buffer one JSON line at a time. 6 KB fits ~24 max-sized rows comfortably;
// the actual payload from the host caps at 6 sessions × ~80 bytes ≈ 500 B.
constexpr size_t kLineMax = 6 * 1024;
char     s_line[kLineMax];
size_t   s_line_len = 0;

// Storage for the parsed snapshot — sessions::set() copies into its own
// table, but we still need the const char* fields it stores by reference
// to remain valid. Keep our own string pool here.
constexpr uint16_t kMaxRows = 16;
constexpr size_t   kNameMax = 32;
char     s_name_pool[kMaxRows][kNameMax];
sessions::Session s_snapshot[kMaxRows];

sessions::Status parse_status(const char *s) {
  if (!s) return sessions::Status::Idle;
  if (strcmp(s, "working") == 0) return sessions::Status::Working;
  if (strcmp(s, "waiting") == 0) return sessions::Status::Waiting;
  if (strcmp(s, "done")    == 0) return sessions::Status::Done;
  if (strcmp(s, "error")   == 0) return sessions::Status::Error;
  return sessions::Status::Idle;
}

void process_line(const char *line, size_t len) {
  (void)len;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("transport: JSON error: %s on input: %s\n", err.c_str(), line);
    return;
  }

  JsonArray arr = doc["s"].as<JsonArray>();
  if (arr.isNull()) {
    Serial.println("transport: missing 's' array");
    return;
  }

  const uint32_t now = millis();
  uint16_t count = 0;
  for (JsonObject item : arr) {
    if (count >= kMaxRows) break;
    const char *n  = item["n"]  | "?";
    const char *st = item["st"] | "idle";
    const uint32_t ago = item["ago"] | 0;

    strncpy(s_name_pool[count], n, kNameMax - 1);
    s_name_pool[count][kNameMax - 1] = '\0';
    s_snapshot[count].name             = s_name_pool[count];
    s_snapshot[count].status           = parse_status(st);
    s_snapshot[count].last_activity_ms = (ago > now) ? 0 : now - ago;
    ++count;
  }

  sessions::set(s_snapshot, count);
  Serial.printf("transport: applied %u sessions\n", (unsigned)count);
}

}  // namespace

namespace transport {

void tick() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (s_line_len > 0) {
        s_line[s_line_len] = '\0';
        process_line(s_line, s_line_len);
        s_line_len = 0;
      }
      continue;
    }
    if (s_line_len + 1 < kLineMax) {
      s_line[s_line_len++] = (char)c;
    } else {
      // Overflow — discard and resync at the next newline.
      s_line_len = 0;
    }
  }
}

}  // namespace transport
