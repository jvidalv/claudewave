// Minimal line protocol over USB-CDC. Each snapshot is a frame:
//
//     BEGIN
//     S<TAB>name<TAB>status<TAB>ago_ms
//     S<TAB>name<TAB>status<TAB>ago_ms
//     ...
//     END
//
// Status is one of: working | waiting | idle | done | error.
// `ago_ms` is host-computed "ms since this session's last activity".
// Lines between BEGIN and END accumulate into a staging list; END
// commits the list via sessions::set().

#include "transport.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr size_t kLineMax = 256;
char     s_line[kLineMax];
size_t   s_line_len = 0;

constexpr uint16_t kMaxRows = 16;
constexpr size_t   kNameMax = 32;
char     s_name_pool[kMaxRows][kNameMax];
sessions::Session s_staging[kMaxRows];
uint16_t s_staging_count = 0;
bool     s_in_frame      = false;

sessions::Status parse_status(const char *s) {
  if (!s) return sessions::Status::Idle;
  if (strcmp(s, "working") == 0) return sessions::Status::Working;
  if (strcmp(s, "waiting") == 0) return sessions::Status::Waiting;
  if (strcmp(s, "done")    == 0) return sessions::Status::Done;
  if (strcmp(s, "error")   == 0) return sessions::Status::Error;
  return sessions::Status::Idle;
}

void handle_session_line(char *line) {
  // Expect: S<TAB>name<TAB>status<TAB>ago_ms (we already stripped the
  // leading "S\t" before getting here).
  char *tab1 = strchr(line, '\t');
  if (!tab1) {
    Serial.println("transport: bad row (no name tab)");
    return;
  }
  *tab1 = '\0';
  const char *name = line;

  char *tab2 = strchr(tab1 + 1, '\t');
  if (!tab2) {
    Serial.println("transport: bad row (no status tab)");
    return;
  }
  *tab2 = '\0';
  const char *status_s = tab1 + 1;

  const char *ago_s = tab2 + 1;
  const uint32_t ago = (uint32_t)atol(ago_s);

  if (s_staging_count >= kMaxRows) return;
  strncpy(s_name_pool[s_staging_count], name, kNameMax - 1);
  s_name_pool[s_staging_count][kNameMax - 1] = '\0';

  const uint32_t now = millis();
  s_staging[s_staging_count].name             = s_name_pool[s_staging_count];
  s_staging[s_staging_count].status           = parse_status(status_s);
  s_staging[s_staging_count].last_activity_ms = (ago > now) ? 0 : now - ago;
  ++s_staging_count;
}

void handle_line(char *line) {
  if (strcmp(line, "BEGIN") == 0) {
    s_in_frame = true;
    s_staging_count = 0;
    return;
  }
  if (strcmp(line, "END") == 0) {
    if (!s_in_frame) return;
    s_in_frame = false;
    sessions::set(s_staging, s_staging_count);
    Serial.printf("transport: applied %u sessions\n", (unsigned)s_staging_count);
    return;
  }
  if (s_in_frame && line[0] == 'S' && line[1] == '\t') {
    handle_session_line(line + 2);
  }
}

}  // namespace

namespace transport {

void tick() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (s_line_len > 0) {
        s_line[s_line_len] = '\0';
        handle_line(s_line);
        s_line_len = 0;
      }
      continue;
    }
    if (s_line_len + 1 < kLineMax) {
      s_line[s_line_len++] = (char)c;
    } else {
      s_line_len = 0;  // overflow → resync at next newline
    }
  }
}

}  // namespace transport
