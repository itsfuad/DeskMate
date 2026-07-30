#include "GithubMode.h"
#include "Platform.h"
#include "Gfx.h"
#include "TileRenderer.h"
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <time.h>

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
constexpr uint16_t GREEN_1 = rgb565(14, 68, 41);
constexpr uint16_t GREEN_2 = rgb565(0, 109, 50);
constexpr uint16_t GREEN_3 = rgb565(38, 166, 65);
constexpr uint16_t GREEN_4 = rgb565(57, 211, 83);
constexpr uint16_t ERROR_C = rgb565(248, 81, 73);

struct GithubData {
  bool valid = false;
  bool error = false;
  int httpCode = 0;
  char errorText[48] = "";
  char login[28] = "";
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
  G.valid = false;
  G.error = true;
  G.httpCode = httpCode;
  strlcpy(G.errorText, message ? message : "GITHUB API ERROR", sizeof(G.errorText));
}

void calculateDerived(JsonArrayConst weeks) {
  memset(G.graph, 0, sizeof(G.graph));
  memset(G.graphLevel, 0, sizeof(G.graphLevel));
  uint8_t linear[GITHUB_GRAPH_WEEKS * 7] = {0};
  uint16_t linearCount = 0;
  uint8_t maxCount = 0;

  const int weekCount = static_cast<int>(weeks.size());
  const int first = max(0, weekCount - GITHUB_GRAPH_WEEKS);
  int outWeek = 0;
  for (int wi = first; wi < weekCount && outWeek < GITHUB_GRAPH_WEEKS; ++wi, ++outWeek) {
    JsonArrayConst days = weeks[wi]["contributionDays"].as<JsonArrayConst>();
    for (JsonObjectConst day : days) {
      const int weekday = constrain(day["weekday"] | 0, 0, 6);
      const int count = constrain(day["contributionCount"] | 0, 0, 255);
      G.graph[outWeek][weekday] = static_cast<uint8_t>(count);
      maxCount = max(maxCount, static_cast<uint8_t>(count));
      if (linearCount < sizeof(linear)) linear[linearCount++] = static_cast<uint8_t>(count);
    }
  }

  for (uint8_t w = 0; w < GITHUB_GRAPH_WEEKS; ++w) {
    for (uint8_t d = 0; d < 7; ++d) {
      const uint8_t value = G.graph[w][d];
      if (!value) G.graphLevel[w][d] = 0;
      else if (maxCount <= 4) G.graphLevel[w][d] = value > 4 ? 4 : value;
      else {
        const uint8_t level = static_cast<uint8_t>((value * 4UL + maxCount - 1) / maxCount);
        G.graphLevel[w][d] = constrain(level, 1, 4);
      }
    }
  }

  G.weekTotal = 0;
  const uint16_t startWeek = linearCount > 7 ? linearCount - 7 : 0;
  for (uint16_t i = startWeek; i < linearCount; ++i) G.weekTotal += linear[i];

  G.streak = 0;
  int i = static_cast<int>(linearCount) - 1;
  // An empty current day does not end yesterday's active streak.
  if (i >= 0 && linear[i] == 0) --i;
  while (i >= 0 && linear[i] > 0) {
    ++G.streak;
    --i;
  }
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
  g.fillRoundRect(8, y, 224, 25, 7, PANEL);
  g.setTextSize(1);
  g.setTextColor(accent);
  g.fillCircle(20, y + 12, 3, accent);
  g.setTextColor(MUTED);
  g.setCursor(31, y + 9);
  g.print(label);
  char number[18];
  snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(value));
  g.setTextColor(TEXT);
  g.setCursor(224 - gfxTextW(number, 1), y + 9);
  g.print(number);
}

void drawGithub(TileCanvas& g, void*) {
  g.fillScreen(BG);
  g.setTextWrap(false);

  drawBranchIcon(g, 14, 10);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(37, 9);
  g.print("GITHUB ACTIVITY");

  if (!G.valid) {
    g.fillRoundRect(10, 43, 220, 178, 14, PANEL);
    g.setTextSize(2);
    g.setTextColor(G.error ? ERROR_C : BLUE);
    g.setCursor(28, 69);
    g.print(G.error ? "API ERROR" : "LOADING");
    g.setTextSize(1);
    g.setTextColor(MUTED);
    g.setCursor(28, 103);
    g.print(G.errorText[0] ? G.errorText : "REQUESTING CONTRIBUTIONS");
    if (G.httpCode) {
      g.setCursor(28, 120);
      g.print("HTTP ");
      g.print(G.httpCode);
    }
    g.setCursor(28, 172);
    g.print("A user token is required.");
    g.setCursor(28, 188);
    g.print("Set it in the web UI.");
    return;
  }

  g.setTextSize(2);
  g.setTextColor(TEXT);
  g.setCursor(10, 34);
  g.print('@');
  g.print(G.login);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  char pulse[28];
  snprintf(pulse, sizeof(pulse), "%u THIS WEEK", G.weekTotal);
  g.setCursor(TFT_WIDTH - gfxTextW(pulse, 1) - 10, 42);
  g.print(pulse);

  statRow(g, 62, "COMMITS THIS YEAR", G.commits, GREEN_4);
  statRow(g, 91, "OPEN PULL REQUESTS", G.openPullRequests, BLUE);
  statRow(g, 120, "OPEN ISSUES", G.openIssues, rgb565(174, 124, 255));

  char streak[30];
  snprintf(streak, sizeof(streak), "STREAK %u DAYS", G.streak);
  g.setTextColor(MUTED);
  g.setCursor(10, 153);
  g.print(streak);
  char total[24];
  snprintf(total, sizeof(total), "%lu TOTAL",
           static_cast<unsigned long>(G.totalContributions));
  g.setCursor(TFT_WIDTH - gfxTextW(total, 1) - 10, 153);
  g.print(total);

  g.setTextColor(MUTED);
  g.setCursor(10, 181);
  g.print("52 WEEK CONTRIBUTIONS");

  // A complete rolling-year contribution graph fits the 240 px panel as
  // 52 narrow columns. Three-pixel cells leave a one-pixel gutter, preserving
  // the familiar GitHub heat-map rhythm without consuming network/heap on art.
  const uint16_t levelColors[5] = {PANEL, GREEN_1, GREEN_2, GREEN_3, GREEN_4};
  const int startX = 16;
  const int startY = 197;
  for (uint8_t w = 0; w < GITHUB_GRAPH_WEEKS; ++w) {
    for (uint8_t d = 0; d < 7; ++d) {
      const int x = startX + w * 4;
      const int y = startY + d * 5;
      g.fillRect(x, y, 3, 4, levelColors[G.graphLevel[w][d]]);
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

  char yearStart[24], recentStart[24], nowIso[24];
  yearStartIso(now, yearStart, sizeof(yearStart));
  isoUtc(now - static_cast<time_t>(GITHUB_GRAPH_WEEKS * 7 - 1) * 86400,
         recentStart, sizeof(recentStart));
  isoUtc(now, nowIso, sizeof(nowIso));

  static const char QUERY[] PROGMEM =
      "query($yearStart:DateTime!,$recentStart:DateTime!,$to:DateTime!){"
      "viewer{login "
      "openIssues:issues(states:OPEN){totalCount} "
      "openPullRequests:pullRequests(states:OPEN){totalCount} "
      "year:contributionsCollection(from:$yearStart,to:$to){"
      "totalCommitContributions "
      "contributionCalendar{totalContributions}} "
      "recent:contributionsCollection(from:$recentStart,to:$to){"
      "contributionCalendar{weeks{contributionDays{contributionCount weekday}}}}}}";

  JsonDocument request;
  request["query"] = FPSTR(QUERY);
  JsonObject variables = request["variables"].to<JsonObject>();
  variables["yearStart"] = yearStart;
  variables["recentStart"] = recentStart;
  variables["to"] = nowIso;
  String body;
  serializeJson(request, body);

  JsonDocument filter;
  filter["errors"][0]["message"] = true;
  filter["data"]["viewer"]["login"] = true;
  filter["data"]["viewer"]["openIssues"]["totalCount"] = true;
  filter["data"]["viewer"]["openPullRequests"]["totalCount"] = true;
  filter["data"]["viewer"]["year"]["totalCommitContributions"] = true;
  filter["data"]["viewer"]["year"]["contributionCalendar"]["totalContributions"] = true;
  filter["data"]["viewer"]["recent"]["contributionCalendar"]["weeks"][0]
        ["contributionDays"][0]["contributionCount"] = true;
  filter["data"]["viewer"]["recent"]["contributionCalendar"]["weeks"][0]
        ["contributionDays"][0]["weekday"] = true;

  // One retry handles the occasional ESP8266 TLS/stream truncation without
  // turning an invalid token or GraphQL permission error into API hammering.
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    std::unique_ptr<SecureClient> client(
        platformMakeSecureClient(4096, nullptr, 512, false));
    HTTPClient http;
    http.setTimeout(settings.httpTimeout);
    http.setReuse(false);
    // Force a non-chunked response before handing the stream to ArduinoJson.
    http.useHTTP10(true);
    if (!http.begin(*client, F("https://api.github.com/graphql"))) {
      if (attempt == 0) { delay(120); continue; }
      setError("CONNECTION FAILED");
      return false;
    }
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("Authorization", "Bearer " + settings.github.token);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-GitHub-Api-Version", "2022-11-28");
    http.setUserAgent(FW_NAME);

    const int code = http.POST(body);
    G.httpCode = code;
    if (code != HTTP_CODE_OK) {
      http.end();
      if (code == 401 || code == 403) {
        setError(code == 401 ? "INVALID TOKEN" : "TOKEN PERMISSION", code);
        return false;
      }
      if (attempt == 0 && (code < 0 || code >= 500)) {
        delay(120);
        continue;
      }
      setError("GITHUB HTTP ERROR", code);
      return false;
    }

    JsonDocument response;
    const DeserializationError err = deserializeJson(
        response, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
      if (attempt == 0) { delay(120); continue; }
      setError("BAD GRAPHQL DATA");
      return false;
    }

    const char* graphError = response["errors"][0]["message"] | "";
    if (graphError[0]) {
      setError(graphError);
      return false;
    }

    JsonObjectConst viewer = response["data"]["viewer"];
    if (viewer.isNull()) {
      setError("NO VIEWER DATA");
      return false;
    }
    strlcpy(G.login, viewer["login"] | "", sizeof(G.login));
    JsonObjectConst year = viewer["year"];
    G.commits = year["totalCommitContributions"] | 0UL;
    G.openIssues = viewer["openIssues"]["totalCount"] | 0UL;
    G.openPullRequests = viewer["openPullRequests"]["totalCount"] | 0UL;
    G.totalContributions =
        year["contributionCalendar"]["totalContributions"] | 0UL;
    calculateDerived(
        viewer["recent"]["contributionCalendar"]["weeks"].as<JsonArrayConst>());
    G.valid = true;
    G.error = false;
    G.errorText[0] = 0;
    G.updatedMs = millis();
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
  nextPoll_ = 0;
  dirty_ = true;
}

void GithubMode::invalidate(const Settings&) {
  nextPoll_ = 0;
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
