#include "GithubMode.h"
#include "Platform.h"
#include "Gfx.h"
#include "TileRenderer.h"
#include "DisplayLayout.h"
#include <Arduino_GFX_Library.h>
#include <time.h>
#include <ctype.h>

GithubMode g_githubMode;

namespace {
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t BG      = rgb565(13, 17, 23);
constexpr uint16_t PANEL   = rgb565(22, 27, 34);
constexpr uint16_t TEXT    = rgb565(230, 237, 243);
constexpr uint16_t MUTED   = rgb565(139, 148, 158);
constexpr uint16_t BLUE    = rgb565(88, 166, 255);
constexpr uint16_t PURPLE  = rgb565(174, 124, 255);
constexpr uint16_t GREEN_1 = rgb565(14, 68, 41);
constexpr uint16_t GREEN_2 = rgb565(0, 109, 50);
constexpr uint16_t GREEN_3 = rgb565(38, 166, 65);
constexpr uint16_t GREEN_4 = rgb565(57, 211, 83);
constexpr uint16_t ERROR_C = rgb565(248, 81, 73);

struct GithubData {
  bool valid = false;
  bool error = false;
  int httpCode = 0;
  char errorText[72] = "";
  char login[32] = "";
  uint32_t commits = 0;
  uint32_t openIssues = 0;
  uint32_t openPullRequests = 0;
  uint32_t totalContributions = 0;
  uint16_t streak = 0;
  uint16_t weekTotal = 0;
  uint8_t graph[GITHUB_GRAPH_WEEKS][7] = {{0}};
  uint8_t graphLevel[GITHUB_GRAPH_WEEKS][7] = {{0}};
  uint32_t updatedMs = 0;
};

GithubData G;

void isoUtc(time_t value, char* out, size_t outSize) {
  struct tm t;
  gmtime_r(&value, &t);
  strftime(out, outSize, "%Y-%m-%dT%H:%M:%SZ", &t);
}

void yearStartIso(time_t now, char* out, size_t outSize) {
  struct tm t;
  gmtime_r(&now, &t);
  snprintf(out, outSize, "%04d-01-01T00:00:00Z", t.tm_year + 1900);
}

void setError(const char* message, int httpCode = 0) {
  // Keep the last good dashboard visible. A transient API/TLS failure is shown
  // as a red status dot instead of discarding valid cached data.
  G.error = true;
  G.httpCode = httpCode;
  strlcpy(G.errorText, message ? message : "GITHUB API ERROR",
          sizeof(G.errorText));
}

class TimedStreamReader {
 public:
  TimedStreamReader(Stream& stream, NetClient& client, int remaining,
                    uint32_t timeoutMs)
      : stream_(stream), client_(client), remaining_(remaining),
        timeoutMs_(timeoutMs) {}

  int read() {
    if (pushed_ >= 0) {
      const int value = pushed_;
      pushed_ = -1;
      return value;
    }
    const uint32_t started = millis();
    for (;;) {
      if (stream_.available()) {
        const int value = stream_.read();
        if (value >= 0 && remaining_ > 0) --remaining_;
        return value;
      }
      if (remaining_ == 0) return -1;
      if (!client_.connected() && !stream_.available()) return -1;
      if (millis() - started >= timeoutMs_) {
        timedOut_ = true;
        return -1;
      }
      delay(1);
      yield();
    }
  }

  void unread(int value) { pushed_ = value; }
  bool timedOut() const { return timedOut_; }

  int nextNonSpace() {
    int value;
    do {
      value = read();
    } while (value >= 0 && isspace(value));
    return value;
  }

  bool readString(char* output, size_t outputSize) {
    size_t length = 0;
    for (;;) {
      int value = read();
      if (value < 0) return false;
      if (value == '"') {
        if (outputSize) {
          const size_t end = length < outputSize - 1 ? length : outputSize - 1;
          output[end] = 0;
        }
        return true;
      }
      if (value == '\\') {
        value = read();
        if (value < 0) return false;
        switch (value) {
          case 'n': value = '\n'; break;
          case 'r': value = '\r'; break;
          case 't': value = '\t'; break;
          case 'b': value = '\b'; break;
          case 'f': value = '\f'; break;
          case 'u':
            // Error/login strings in this response are ASCII. Consume a JSON
            // unicode escape without allocating a UTF-8 conversion buffer.
            for (uint8_t i = 0; i < 4; ++i) if (read() < 0) return false;
            value = '?';
            break;
          default: break;
        }
      }
      if (outputSize && length + 1 < outputSize) output[length] = value;
      ++length;
    }
  }

  bool readUnsigned(int first, uint32_t& output) {
    if (first < '0' || first > '9') return false;
    uint32_t value = static_cast<uint32_t>(first - '0');
    for (;;) {
      const int next = read();
      if (next < '0' || next > '9') {
        if (next >= 0) unread(next);
        output = value;
        return true;
      }
      if (value <= 429496729UL) value = value * 10UL + (next - '0');
    }
  }

  void skipPrimitive(int first) {
    int value = first;
    while (value >= 0 && value != ',' && value != '}' && value != ']') {
      value = read();
    }
    if (value >= 0) unread(value);
  }

 private:
  Stream& stream_;
  NetClient& client_;
  int remaining_;
  uint32_t timeoutMs_;
  int pushed_ = -1;
  bool timedOut_ = false;
};

struct CalendarBuilder {
  int lastWeekday = -1;
  int pendingWeekday = -1;
  int pendingCount = -1;
  uint8_t week = 0;
  bool sawDay = false;
};

void advanceCalendarWeek(GithubData& data, CalendarBuilder& builder) {
  if (builder.week + 1 < GITHUB_GRAPH_WEEKS) {
    ++builder.week;
    return;
  }
  memmove(data.graph[0], data.graph[1],
          (GITHUB_GRAPH_WEEKS - 1) * 7 * sizeof(uint8_t));
  memset(data.graph[GITHUB_GRAPH_WEEKS - 1], 0, 7 * sizeof(uint8_t));
  builder.week = GITHUB_GRAPH_WEEKS - 1;
}

void commitCalendarDay(GithubData& data, CalendarBuilder& builder) {
  if (builder.pendingWeekday < 0 || builder.pendingCount < 0) return;
  const int weekday = constrain(builder.pendingWeekday, 0, 6);
  if (builder.lastWeekday >= 0 && weekday <= builder.lastWeekday) {
    advanceCalendarWeek(data, builder);
  }
  data.graph[builder.week][weekday] = static_cast<uint8_t>(
      constrain(builder.pendingCount, 0, 255));
  builder.lastWeekday = weekday;
  builder.pendingWeekday = -1;
  builder.pendingCount = -1;
  builder.sawDay = true;
}

void calculateDerived(GithubData& data, const CalendarBuilder& builder) {
  uint8_t maximum = 0;
  for (uint8_t week = 0; week < GITHUB_GRAPH_WEEKS; ++week) {
    for (uint8_t day = 0; day < 7; ++day) {
      maximum = max(maximum, data.graph[week][day]);
    }
  }

  for (uint8_t week = 0; week < GITHUB_GRAPH_WEEKS; ++week) {
    for (uint8_t day = 0; day < 7; ++day) {
      const uint8_t value = data.graph[week][day];
      if (!value) data.graphLevel[week][day] = 0;
      else if (maximum <= 4) data.graphLevel[week][day] = value > 4 ? 4 : value;
      else {
        const uint8_t level = static_cast<uint8_t>(
            (value * 4UL + maximum - 1) / maximum);
        data.graphLevel[week][day] = constrain(level, 1, 4);
      }
    }
  }

  data.weekTotal = 0;
  if (builder.sawDay) {
    for (uint8_t day = 0; day < 7; ++day) {
      data.weekTotal += data.graph[builder.week][day];
    }
  }

  data.streak = 0;
  if (!builder.sawDay) return;
  int week = builder.week;
  int day = builder.lastWeekday;
  if (day >= 0 && data.graph[week][day] == 0) {
    if (--day < 0) {
      day = 6;
      --week;
    }
  }
  while (week >= 0 && day >= 0) {
    if (data.graph[week][day] == 0) break;
    ++data.streak;
    if (--day < 0) {
      day = 6;
      --week;
    }
  }
}

bool parseGithubResponse(Stream& stream, NetClient& client, int contentLength,
                         uint32_t timeoutMs, GithubData& output,
                         char* graphError, size_t graphErrorSize) {
  TimedStreamReader reader(stream, client, contentLength, timeoutMs);
  CalendarBuilder calendar;
  bool sawLogin = false;
  bool sawCommitCount = false;

  for (;;) {
    const int value = reader.read();
    if (value < 0) break;
    if (value != '"') continue;

    char key[40];
    if (!reader.readString(key, sizeof(key))) return false;
    const int separator = reader.nextNonSpace();
    if (separator != ':') {
      if (separator >= 0) reader.unread(separator);
      continue;  // this was a string value, not an object key
    }
    const int first = reader.nextNonSpace();
    if (first < 0) return false;

    if (!strcmp(key, "login") && first == '"') {
      if (!reader.readString(output.login, sizeof(output.login))) return false;
      sawLogin = output.login[0] != 0;
    } else if (!strcmp(key, "yearCommitCount")) {
      uint32_t number;
      if (!reader.readUnsigned(first, number)) return false;
      output.commits = number;
      sawCommitCount = true;
    } else if (!strcmp(key, "openIssueCount")) {
      uint32_t number;
      if (!reader.readUnsigned(first, number)) return false;
      output.openIssues = number;
    } else if (!strcmp(key, "openPullRequestCount")) {
      uint32_t number;
      if (!reader.readUnsigned(first, number)) return false;
      output.openPullRequests = number;
    } else if (!strcmp(key, "recentTotal")) {
      uint32_t number;
      if (!reader.readUnsigned(first, number)) return false;
      output.totalContributions = number;
    } else if (!strcmp(key, "weekday")) {
      uint32_t number;
      if (!reader.readUnsigned(first, number)) return false;
      calendar.pendingWeekday = static_cast<int>(number);
      commitCalendarDay(output, calendar);
    } else if (!strcmp(key, "contributionCount")) {
      uint32_t number;
      if (!reader.readUnsigned(first, number)) return false;
      calendar.pendingCount = static_cast<int>(number);
      commitCalendarDay(output, calendar);
    } else if (!strcmp(key, "message") && first == '"') {
      if (!reader.readString(graphError, graphErrorSize)) return false;
    } else if (first == '"') {
      char ignored[2];
      if (!reader.readString(ignored, sizeof(ignored))) return false;
    } else if (first == '{' || first == '[') {
      reader.unread(first);  // scan nested objects for the fields above
    } else {
      reader.skipPrimitive(first);
    }
  }

  if (reader.timedOut()) return false;
  calculateDerived(output, calendar);
  return sawLogin && sawCommitCount && calendar.sawDay;
}

void drawBranchIcon(TileCanvas& g, int x, int y) {
  g.drawCircle(x, y, 3, BLUE);
  g.drawCircle(x, y + 15, 3, BLUE);
  g.drawCircle(x + 14, y + 8, 3, BLUE);
  g.drawFastVLine(x, y + 3, 9, BLUE);
  g.drawLine(x + 3, y + 13, x + 10, y + 9, BLUE);
}

void statRow(TileCanvas& g, int y, const char* label, uint32_t value,
             uint16_t accent) {
  g.fillRoundRect(8, y, 224, 24, 7, PANEL);
  g.setTextSize(1);
  g.fillCircle(20, y + 12, 3, accent);
  g.setTextColor(MUTED);
  g.setCursor(31, y + 8);
  g.print(label);
  char number[18];
  snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(value));
  g.setTextColor(TEXT);
  g.setCursor(224 - gfxTextW(number, 1), y + 8);
  g.print(number);
}

void drawGithub(TileCanvas& g, void*) {
  g.fillScreen(BG);
  g.setTextWrap(false);

  drawBranchIcon(g, 14, 9);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(37, 9);
  g.print("GITHUB ACTIVITY");
  g.fillCircle(228, 13, G.error ? 4 : 3, G.error ? ERROR_C : GREEN_4);

  if (!G.valid) {
    g.fillRoundRect(10, 40, 220, 184, 14, PANEL);
    g.setTextSize(2);
    g.setTextColor(G.error ? ERROR_C : BLUE);
    g.setCursor(26, 64);
    g.print(G.error ? "API ERROR" : "LOADING");
    g.setTextSize(1);
    g.setTextColor(MUTED);
    g.setCursor(26, 99);
    g.print(G.errorText[0] ? G.errorText : "REQUESTING CONTRIBUTIONS");
    if (G.httpCode) {
      g.setCursor(26, 116);
      g.print("HTTP ");
      g.print(G.httpCode);
    }
    g.setCursor(26, 178);
    g.print("Use a user access token.");
    g.setCursor(26, 194);
    g.print("Set it in the DeskMate UI.");
    return;
  }

  g.setTextSize(2);
  g.setTextColor(TEXT);
  g.setCursor(10, 32);
  g.print('@');
  g.print(G.login);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  char pulse[28];
  snprintf(pulse, sizeof(pulse), "%u THIS WEEK", G.weekTotal);
  g.setCursor(DisplayLayout::Right - gfxTextW(pulse, 1), 40);
  g.print(pulse);

  statRow(g, 55, "COMMITS THIS YEAR", G.commits, GREEN_4);
  statRow(g, 83, "OPEN PULL REQUESTS", G.openPullRequests, BLUE);
  statRow(g, 111, "OPEN ISSUES", G.openIssues, PURPLE);

  char streak[28];
  snprintf(streak, sizeof(streak), "STREAK %u DAYS", G.streak);
  g.setTextColor(MUTED);
  g.setCursor(10, 143);
  g.print(streak);
  char total[24];
  snprintf(total, sizeof(total), "%lu TOTAL",
           static_cast<unsigned long>(G.totalContributions));
  g.setCursor(DisplayLayout::Right - gfxTextW(total, 1), 143);
  g.print(total);

  g.setCursor(10, 171);
  g.print("52 WEEK CONTRIBUTIONS");

  const uint16_t levelColors[5] = {PANEL, GREEN_1, GREEN_2, GREEN_3, GREEN_4};
  constexpr int startX = 12;
  constexpr int startY = 188;
  static_assert(DisplayLayout::fitsSafe(startX, startY, 208, 34),
                "GitHub contribution graph must fit the safe display area");
  for (uint8_t week = 0; week < GITHUB_GRAPH_WEEKS; ++week) {
    for (uint8_t day = 0; day < 7; ++day) {
      const int x = startX + week * 4;
      const int y = startY + day * 5;
      g.fillRect(x, y, 3, 4, levelColors[G.graphLevel[week][day]]);
    }
  }
}

bool fetchGraphql(const Settings& settings) {
  if (!settings.github.token.length()) {
    setError("TOKEN REQUIRED");
    return false;
  }
  const time_t now = time(nullptr);
  if (now < 1609459200) {
    setError("WAITING FOR CLOCK");
    return false;
  }

#if defined(DESKMATE_ESP8266)
  // A failed large allocation during BearSSL setup can reset the ESP8266. Skip
  // safely and retry later rather than entering a reboot loop on this screen.
  if (ESP.getFreeHeap() < 19000 || platformMaxFreeBlock() < 11000) {
    setError("LOW HEAP - RETRY LATER");
    return false;
  }
#endif

  char yearStart[24];
  char recentStart[24];
  char nowIso[24];
  yearStartIso(now, yearStart, sizeof(yearStart));
  isoUtc(now - static_cast<time_t>(GITHUB_GRAPH_WEEKS * 7 - 1) * 86400,
         recentStart, sizeof(recentStart));
  isoUtc(now, nowIso, sizeof(nowIso));

  // The former query omitted first/last on the user issue/PR connections,
  // which GitHub rejects as a pagination error. first:1 keeps the response
  // tiny while totalCount still represents the complete connection.
  static const char QUERY[] PROGMEM =
      "query($yearStart:DateTime!,$recentStart:DateTime!,$to:DateTime!){"
      "viewer{login "
      "openIssues:issues(first:1,states:[OPEN]){openIssueCount:totalCount} "
      "openPullRequests:pullRequests(first:1,states:[OPEN]){"
      "openPullRequestCount:totalCount} "
      "year:contributionsCollection(from:$yearStart,to:$to){"
      "yearCommitCount:totalCommitContributions} "
      "recent:contributionsCollection(from:$recentStart,to:$to){"
      "contributionCalendar{recentTotal:totalContributions "
      "weeks{contributionDays{weekday contributionCount}}}}}}";

  String body;
  body.reserve(900);
  body += F("{\"query\":\"");
  body += FPSTR(QUERY);
  body += F("\",\"variables\":{\"yearStart\":\"");
  body += yearStart;
  body += F("\",\"recentStart\":\"");
  body += recentStart;
  body += F("\",\"to\":\"");
  body += nowIso;
  body += F("\"}}");

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    std::unique_ptr<SecureClient> client(
        platformMakeSecureClient(4096, nullptr, 512, false));
    if (!client) {
      setError("TLS ALLOCATION FAILED");
      return false;
    }

    HTTPClient http;
    const uint16_t timeoutMs = settings.httpTimeout > 8000
        ? 8000 : settings.httpTimeout;
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    http.useHTTP10(true);
    if (!http.begin(*client, F("https://api.github.com/graphql"))) {
      if (attempt == 0) {
        delay(100);
        continue;
      }
      setError("CONNECTION FAILED");
      return false;
    }
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("Authorization", "Bearer " + settings.github.token);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-GitHub-Api-Version", "2022-11-28");
    http.addHeader("Connection", "close");
    http.setUserAgent(FW_NAME);

    const int code = http.POST(body);
    if (code != HTTP_CODE_OK) {
      http.end();
      if (code == 401 || code == 403) {
        setError(code == 401 ? "INVALID TOKEN" : "TOKEN PERMISSION", code);
        return false;
      }
      if (attempt == 0 && (code < 0 || code >= 500)) {
        delay(100);
        continue;
      }
      setError("GITHUB HTTP ERROR", code);
      return false;
    }

    GithubData next;
    char graphError[72] = "";
    Stream& stream = http.getStream();
    const bool parsed = parseGithubResponse(
        stream, *client, http.getSize(), timeoutMs, next,
        graphError, sizeof(graphError));
    http.end();

    if (graphError[0]) {
      setError(graphError, code);
      return false;
    }
    if (!parsed) {
      if (attempt == 0) {
        delay(100);
        continue;
      }
      setError("BAD GRAPHQL DATA", code);
      return false;
    }

    next.valid = true;
    next.error = false;
    next.httpCode = code;
    next.errorText[0] = 0;
    next.updatedMs = millis();
    G = next;
    return true;
  }

  setError("GITHUB REQUEST FAILED");
  return false;
}
}  // namespace

void GithubMode::fetch(const Settings& settings) {
  fetchGraphql(settings);
  dirty_ = true;
}

void GithubMode::begin(const Settings&) {
  // Let Wi-Fi, NTP, the web portal, and the first screen settle before the
  // memory-heavy TLS request. This also prevents an immediate boot-loop if a
  // broken token/configuration had GitHub selected at startup.
  nextPoll_ = millis() + 5000UL;
  dirty_ = true;
}

void GithubMode::invalidate(const Settings&) {
  nextPoll_ = millis() + 1000UL;
  dirty_ = true;
}

void GithubMode::wake(const Settings&) { dirty_ = true; }

void GithubMode::render(const Settings&) {
  gfxRenderTiled(drawGithub, nullptr, BG);
}

void GithubMode::service(const Settings& settings) {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextPoll_) >= 0) {
    nextPoll_ = now + static_cast<uint32_t>(settings.github.pollSec) * 1000UL;
    fetch(settings);
  }
  if (dirty_) {
    render(settings);
    dirty_ = false;
  }
}
