#include "GithubMode.h"
#include "Platform.h"
#include "HttpRequest.h"
#include "JsonScanner.h"
#include "Gfx.h"
#include "Icons.h"
#include "TileRenderer.h"
#include "StatusDot.h"
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
constexpr uint16_t LINE    = rgb565(43, 49, 58);
constexpr uint16_t TEXT    = rgb565(230, 237, 243);
constexpr uint16_t MUTED   = rgb565(139, 148, 158);
constexpr uint16_t BLUE    = rgb565(88, 166, 255);
constexpr uint16_t PURPLE  = rgb565(174, 124, 255);
constexpr uint16_t AMBER   = rgb565(210, 153, 34);
constexpr uint16_t ORANGE  = rgb565(219, 109, 40);
constexpr uint16_t GREEN_1 = rgb565(14, 68, 41);
constexpr uint16_t GREEN_2 = rgb565(0, 109, 50);
constexpr uint16_t GREEN_3 = rgb565(38, 166, 65);
constexpr uint16_t GREEN_4 = rgb565(57, 211, 83);
constexpr uint16_t ERROR_C = rgb565(248, 81, 73);
PollResult indicatorResult = PollResult::Skipped;

uint16_t indicatorColor() {
  switch (indicatorResult) {
    case PollResult::Success: return C_GREEN;
    case PollResult::Failed: return C_RED;
    case PollResult::Skipped: return C_WHITE;
    case PollResult::MoreWork: return ORANGE;
  }
  return C_WHITE;
}

// GitHub's own state colours (Primer, dark theme). A row is coloured by the
// state of the thing it points at and iconed by what kind of thing it is,
// which is exactly how GitHub's own inbox reads.
constexpr uint16_t ST_OPEN   = rgb565(63, 185, 80);    // #3fb950 open
constexpr uint16_t ST_DRAFT  = rgb565(139, 148, 158);  // #8b949e draft
constexpr uint16_t ST_MERGED = rgb565(163, 113, 247);  // #a371f7 merged
constexpr uint16_t ST_CLOSED = rgb565(248, 81, 73);    // #f85149 closed

// Four rows is what a 240 x 240 panel can show at a legible text size. The
// screen is a prompt to act, not a complete work queue, so the counts in each
// page header carry the full totals.
constexpr uint8_t kMaxRows = 4;
constexpr uint8_t kRepoLen = 18;
constexpr uint8_t kTitleLen = 30;
// The rotation's slowest sensible step. A page change is a full tiled repaint,
// so a very short dwell divided across every page is floored rather than
// allowed to thrash the panel.
constexpr uint32_t kMinPageMs = 1200;
constexpr uint32_t kCalendarIntervalMs = 3600000UL;  // the calendar moves daily
constexpr uint32_t kCalendarRetryMs = 300000UL;
// The response is parsed as it arrives and never buffered, so this bounds how
// long a reply may occupy the single network slot, not how much RAM it needs.
// It has to clear a full contribution calendar for a busy account with room to
// spare; the old 24 KB limit rejected the response outright, before a byte of
// it was read.
constexpr size_t kMaxResponseBytes = 96 * 1024;

enum : uint8_t { PAGE_INBOX, PAGE_PULLS, PAGE_PULSE, PAGE_TOTAL };
enum : uint8_t { KIND_REVIEW, KIND_MENTION, KIND_ISSUE, KIND_PR };
enum : uint8_t { STATE_OPEN, STATE_DRAFT, STATE_MERGED, STATE_CLOSED };
enum : uint8_t { CHECK_NONE, CHECK_PENDING, CHECK_SUCCESS, CHECK_FAILURE };
enum : uint8_t { REVIEW_NONE, REVIEW_REQUIRED, REVIEW_APPROVED, REVIEW_CHANGES };
enum : uint8_t { SEC_NONE, SEC_REV, SEC_MEN, SEC_ASG, SEC_OWN };

// drawBadge() renders review state and check state from one switch, which is
// only correct while the two enumerations agree on "waiting", "good" and "bad".
static_assert(static_cast<uint8_t>(CHECK_PENDING) ==
                  static_cast<uint8_t>(REVIEW_REQUIRED),
              "badge states must align");
static_assert(static_cast<uint8_t>(CHECK_SUCCESS) ==
                  static_cast<uint8_t>(REVIEW_APPROVED),
              "badge states must align");
static_assert(static_cast<uint8_t>(CHECK_FAILURE) ==
                  static_cast<uint8_t>(REVIEW_CHANGES),
              "badge states must align");

struct ActivityRow {
  char repo[kRepoLen];
  char title[kTitleLen];
  uint32_t created;   // unix seconds, 0 when unknown
  uint16_t number;
  uint8_t kind;
  uint8_t state;
  uint8_t checks;
  uint8_t review;
};

struct GithubData {
  bool valid = false;          // at least one successful list fetch
  bool error = false;          // last attempt failed; cached data still shown
  int httpCode = 0;
  char errorText[72] = "";
  char login[32] = "";

  // Phase A: counts and action lists.
  uint32_t openIssues = 0;
  uint32_t openPullRequests = 0;
  ActivityRow inbox[kMaxRows];
  ActivityRow mine[kMaxRows];
  uint8_t inboxCount = 0;
  uint8_t mineCount = 0;
  uint16_t inboxTotal = 0;     // reviews + mentions + assigned, not just shown
  uint16_t mineTotal = 0;

  // Phase B: contribution pulse. Its failure is kept apart from the shared
  // error state: the two phases fail independently, and a later successful
  // list fetch clearing the shared state would otherwise erase the reason the
  // pulse page has nothing to show.
  bool calendarValid = false;
  bool calendarFailed = false;
  char calendarError[72] = "";
  uint32_t calendarRetryAt = 0;   // millis() of the next attempt
  uint32_t commits = 0;
  uint32_t totalContributions = 0;
  uint16_t streak = 0;
  uint16_t weekTotal = 0;
  uint8_t weekCount = 0;
  uint8_t rangeMonths = 3;
  uint8_t graph[GITHUB_GRAPH_WEEKS][7] = {{0}};
};

// The live snapshot and the staging areas the parsers fill are static rather
// than automatic: an ESP8266 runs this parse with a TLS session resident, and
// nearly a kilobyte of extra stack under BearSSL is not worth the risk.
GithubData G;

struct ListStage {
  char login[32];
  uint32_t openIssues;
  uint32_t openPullRequests;
  ActivityRow inbox[kMaxRows];
  ActivityRow mine[kMaxRows];
  uint8_t inboxCount;
  uint8_t mineCount;
  uint16_t inboxTotal;
  uint16_t mineTotal;
  bool sawLogin;
  // Sections arrive in query order, so each one starts appending where the
  // previous section stopped. That gives review requests priority over
  // mentions, and mentions priority over assigned issues, for free.
  uint8_t lastSection;
  uint8_t sectionBase;
  char graphError[72];
};

struct CalendarStage {
  uint32_t commits;
  uint32_t totalContributions;
  uint8_t graph[GITHUB_GRAPH_WEEKS][7];
  int8_t lastWeekday;
  int8_t pendingWeekday;
  int16_t pendingCount;
  uint8_t week;
  bool sawDay;
  char graphError[72];
};

ListStage gListStage;
CalendarStage gCalendarStage;

void isoUtc(time_t value, char* out, size_t outSize) {
  struct tm t;
  gmtime_r(&value, &t);
  strftime(out, outSize, "%Y-%m-%dT%H:%M:%SZ", &t);
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// civil-calendar algorithm). timegm() is not portable across the three
// toolchains this firmware builds under, and mktime() would apply the device's
// local offset to an explicitly UTC timestamp.
int32_t daysFromCivil(int32_t year, uint8_t month, uint8_t day) {
  year -= month <= 2;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(year - era * 400);
  const uint32_t doy =
      (153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u + day - 1u;
  const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

// "2024-05-01T12:34:56Z" -> unix seconds. Returns 0 for anything else; the
// renderer then omits the age badge instead of showing a wrong one.
uint32_t parseIso8601(const char* text) {
  if (!text) return 0;
  uint16_t field[6] = {0, 0, 0, 0, 0, 0};
  uint8_t index = 0;
  uint8_t digits = 0;
  for (const char* cursor = text; *cursor && index < 6; ++cursor) {
    if (*cursor >= '0' && *cursor <= '9') {
      field[index] = static_cast<uint16_t>(field[index] * 10 + (*cursor - '0'));
      if (++digits > 4) return 0;
    } else if (digits) {
      ++index;
      digits = 0;
    }
  }
  if (index + (digits ? 1 : 0) < 6) return 0;
  if (field[0] < 1970 || field[1] < 1 || field[1] > 12 ||
      field[2] < 1 || field[2] > 31) {
    return 0;
  }
  const int32_t days = daysFromCivil(field[0], static_cast<uint8_t>(field[1]),
                                     static_cast<uint8_t>(field[2]));
  if (days < 0) return 0;
  return static_cast<uint32_t>(days) * 86400UL +
         static_cast<uint32_t>(field[3]) * 3600UL +
         static_cast<uint32_t>(field[4]) * 60UL + field[5];
}

void setError(const char* message, int httpCode = 0) {
  // Keep the last good dashboard visible. A transient API/TLS failure is shown
  // as a red header rule instead of discarding valid cached data.
  G.error = true;
  G.httpCode = httpCode;
  strlcpy(G.errorText, message ? message : "GITHUB API ERROR",
          sizeof(G.errorText));
}

void copyDisplayLabel(const char* source, char* output, size_t outputSize,
                      uint8_t maxChars) {
  if (!outputSize) return;
  if (!source) source = "";
  const size_t length = strlen(source);
  if (length <= maxChars) {
    strlcpy(output, source, outputSize);
    return;
  }
  const size_t keep = maxChars > 3 ? maxChars - 3 : maxChars;
  const size_t copied = min<size_t>(keep, outputSize - 1);
  memcpy(output, source, copied);
  output[copied] = 0;
  if (maxChars > 3 && outputSize - copied > 3) strlcat(output, "...", outputSize);
}

void formatCompactCount(uint32_t value, char* output, size_t outputSize) {
  if (!outputSize) return;
  if (value < 1000UL) {
    snprintf(output, outputSize, "%lu", static_cast<unsigned long>(value));
  } else if (value < 1000000UL) {
    snprintf(output, outputSize, "%lu.%luK",
             static_cast<unsigned long>(value / 1000UL),
             static_cast<unsigned long>((value % 1000UL) / 100UL));
  } else {
    snprintf(output, outputSize, "%lu.%luM",
             static_cast<unsigned long>(value / 1000000UL),
             static_cast<unsigned long>((value % 1000000UL) / 100000UL));
  }
}

// A queue age is the whole point of the inbox: a review requested ten minutes
// ago and one requested nine days ago demand different responses.
void formatAge(uint32_t created, uint32_t now, char* output, size_t outputSize) {
  if (!outputSize) return;
  output[0] = 0;
  if (!created || now < 1609459200UL || now <= created) return;
  const uint32_t seconds = now - created;
  if (seconds < 3600UL) {
    snprintf(output, outputSize, "%lum",
             static_cast<unsigned long>(seconds / 60UL));
  } else if (seconds < 86400UL) {
    snprintf(output, outputSize, "%luh",
             static_cast<unsigned long>(seconds / 3600UL));
  } else if (seconds < 86400UL * 28UL) {
    snprintf(output, outputSize, "%lud",
             static_cast<unsigned long>(seconds / 86400UL));
  } else {
    snprintf(output, outputSize, "%luw",
             static_cast<unsigned long>(seconds / (86400UL * 7UL)));
  }
}

// ---------------------------------------------------------------------------
// Response parsing
// ---------------------------------------------------------------------------

uint8_t sectionOf(const JsonScanner& scanner) {
  if (scanner.under("rev")) return SEC_REV;
  if (scanner.under("men")) return SEC_MEN;
  if (scanner.under("asg")) return SEC_ASG;
  if (scanner.under("own")) return SEC_OWN;
  return SEC_NONE;
}

uint8_t kindOfSection(uint8_t section) {
  switch (section) {
    case SEC_REV: return KIND_REVIEW;
    case SEC_MEN: return KIND_MENTION;
    case SEC_OWN: return KIND_PR;
    default: return KIND_ISSUE;
  }
}

ActivityRow* resolveRow(ListStage& stage, uint8_t section, int16_t index) {
  if (index < 0) return nullptr;
  if (section == SEC_OWN) {
    if (index >= kMaxRows) return nullptr;
    if (index + 1 > stage.mineCount) {
      stage.mineCount = static_cast<uint8_t>(index + 1);
    }
    stage.mine[index].kind = KIND_PR;
    return &stage.mine[index];
  }
  if (section != stage.lastSection) {
    stage.lastSection = section;
    stage.sectionBase = stage.inboxCount;
  }
  const int16_t slot = stage.sectionBase + index;
  if (slot >= kMaxRows) return nullptr;
  if (slot + 1 > stage.inboxCount) {
    stage.inboxCount = static_cast<uint8_t>(slot + 1);
  }
  stage.inbox[slot].kind = kindOfSection(section);
  return &stage.inbox[slot];
}

uint8_t checkStateOf(const char* text) {
  if (!strcmp(text, "SUCCESS")) return CHECK_SUCCESS;
  if (!strcmp(text, "FAILURE") || !strcmp(text, "ERROR")) return CHECK_FAILURE;
  if (!strcmp(text, "PENDING") || !strcmp(text, "EXPECTED")) return CHECK_PENDING;
  return CHECK_NONE;
}

uint8_t reviewStateOf(const char* text) {
  if (!strcmp(text, "APPROVED")) return REVIEW_APPROVED;
  if (!strcmp(text, "CHANGES_REQUESTED")) return REVIEW_CHANGES;
  if (!strcmp(text, "REVIEW_REQUIRED")) return REVIEW_REQUIRED;
  return REVIEW_NONE;
}

void resetListStage(void* context) {
  ListStage& stage = *static_cast<ListStage*>(context);
  memset(&stage, 0, sizeof(stage));
  stage.lastSection = SEC_NONE;
}

void onListValue(void* context, const JsonScanner& scanner,
                 JsonScanner::Value type, const char* text, uint32_t number) {
  ListStage& stage = *static_cast<ListStage*>(context);
  const char* key = scanner.key();

  // A GraphQL failure is reported inside a top-level "errors" array. Matching
  // on the container rather than the bare key means a "message" field in the
  // payload can never be mistaken for one.
  if (scanner.under("errors")) {
    if (!strcmp(key, "message") && type == JsonScanner::Value::String &&
        !stage.graphError[0]) {
      strlcpy(stage.graphError, text, sizeof(stage.graphError));
    }
    return;
  }

  if (scanner.under("viewer")) {
    if (!strcmp(key, "login") && type == JsonScanner::Value::String) {
      strlcpy(stage.login, text, sizeof(stage.login));
      stage.sawLogin = stage.login[0] != 0;
    } else if (!strcmp(key, "totalCount") &&
               type == JsonScanner::Value::Number) {
      if (scanner.under("openIssues")) stage.openIssues = number;
      else if (scanner.under("openPrs")) stage.openPullRequests = number;
    }
    return;
  }

  const uint8_t section = sectionOf(scanner);
  if (section == SEC_NONE) return;

  if (!strcmp(key, "issueCount") && type == JsonScanner::Value::Number) {
    const uint16_t total = number > 0xFFFFUL ? 0xFFFF
                                             : static_cast<uint16_t>(number);
    if (section == SEC_OWN) stage.mineTotal = total;
    else stage.inboxTotal = static_cast<uint16_t>(
        min<uint32_t>(0xFFFFUL, stage.inboxTotal + total));
    return;
  }

  ActivityRow* row = resolveRow(stage, section, scanner.indexUnder("nodes"));
  if (!row) return;

  if (!strcmp(key, "number") && type == JsonScanner::Value::Number) {
    row->number = number > 0xFFFFUL ? 0 : static_cast<uint16_t>(number);
  } else if (!strcmp(key, "title") && type == JsonScanner::Value::String) {
    copyDisplayLabel(text, row->title, sizeof(row->title), kTitleLen - 1);
  } else if (!strcmp(key, "nameWithOwner") &&
             type == JsonScanner::Value::String) {
    copyDisplayLabel(text, row->repo, sizeof(row->repo), kRepoLen - 1);
  } else if (!strcmp(key, "createdAt") && type == JsonScanner::Value::String) {
    row->created = parseIso8601(text);
  } else if (!strcmp(key, "isDraft")) {
    // A draft is an open pull request, so it overrides the OPEN the state
    // field reports; the two arrive independently and in either order.
    if (!strcmp(text, "true")) row->state = STATE_DRAFT;
  } else if (!strcmp(key, "pstate") && type == JsonScanner::Value::String) {
    if (!strcmp(text, "MERGED")) row->state = STATE_MERGED;
    else if (!strcmp(text, "CLOSED")) row->state = STATE_CLOSED;
    else if (row->state != STATE_DRAFT) row->state = STATE_OPEN;
  } else if (!strcmp(key, "review") && type == JsonScanner::Value::String) {
    row->review = reviewStateOf(text);
  } else if (!strcmp(key, "state") && type == JsonScanner::Value::String &&
             !strcmp(scanner.container(0), "checks")) {
    row->checks = checkStateOf(text);
  }
}

void commitCalendarDay(CalendarStage& stage) {
  if (stage.pendingWeekday < 0 || stage.pendingCount < 0) return;
  const int8_t weekday = constrain(stage.pendingWeekday, 0, 6);
  if (stage.lastWeekday >= 0 && weekday <= stage.lastWeekday) {
    if (stage.week + 1 < GITHUB_GRAPH_WEEKS) {
      ++stage.week;
    } else {
      // The window is a fixed number of weeks. Scroll rather than grow so a
      // range longer than the grid keeps the most recent weeks.
      memmove(stage.graph[0], stage.graph[1],
              (GITHUB_GRAPH_WEEKS - 1) * 7 * sizeof(uint8_t));
      memset(stage.graph[GITHUB_GRAPH_WEEKS - 1], 0, 7 * sizeof(uint8_t));
      stage.week = GITHUB_GRAPH_WEEKS - 1;
    }
  }
  stage.graph[stage.week][weekday] =
      static_cast<uint8_t>(constrain(stage.pendingCount, 0, 255));
  stage.lastWeekday = weekday;
  stage.pendingWeekday = -1;
  stage.pendingCount = -1;
  stage.sawDay = true;
}

void resetCalendarStage(void* context) {
  CalendarStage& stage = *static_cast<CalendarStage*>(context);
  memset(&stage, 0, sizeof(stage));
  stage.lastWeekday = -1;
  stage.pendingWeekday = -1;
  stage.pendingCount = -1;
}

void onCalendarValue(void* context, const JsonScanner& scanner,
                     JsonScanner::Value type, const char* text,
                     uint32_t number) {
  CalendarStage& stage = *static_cast<CalendarStage*>(context);
  const char* key = scanner.key();

  if (scanner.under("errors")) {
    if (!strcmp(key, "message") && type == JsonScanner::Value::String &&
        !stage.graphError[0]) {
      strlcpy(stage.graphError, text, sizeof(stage.graphError));
    }
    return;
  }
  if (type != JsonScanner::Value::Number) return;
  (void)text;

  if (!strcmp(key, "commits")) stage.commits = number;
  else if (!strcmp(key, "total")) stage.totalContributions = number;
  else if (!strcmp(key, "weekday")) {
    stage.pendingWeekday = static_cast<int8_t>(number > 6 ? 6 : number);
    commitCalendarDay(stage);
  } else if (!strcmp(key, "n")) {
    stage.pendingCount = static_cast<int16_t>(number > 255 ? 255 : number);
    commitCalendarDay(stage);
  }
}

// Totals, streak and the five render levels are all derived from the raw
// counts, so the grid is quantized in place once the scan is complete.
void finishCalendar(CalendarStage& stage) {
  uint8_t maximum = 0;
  for (uint8_t week = 0; week < GITHUB_GRAPH_WEEKS; ++week) {
    for (uint8_t day = 0; day < 7; ++day) {
      maximum = max(maximum, stage.graph[week][day]);
    }
  }

  G.weekCount = stage.sawDay
      ? min<uint8_t>(static_cast<uint8_t>(stage.week + 1), GITHUB_GRAPH_WEEKS)
      : 0;
  G.weekTotal = 0;
  if (stage.sawDay) {
    for (uint8_t day = 0; day < 7; ++day) {
      G.weekTotal = static_cast<uint16_t>(G.weekTotal +
                                          stage.graph[stage.week][day]);
    }
  }

  G.streak = 0;
  if (stage.sawDay) {
    int week = stage.week;
    int day = stage.lastWeekday;
    // Today counting zero does not break a streak until the day is over.
    if (day >= 0 && stage.graph[week][day] == 0) {
      if (--day < 0) { day = 6; --week; }
    }
    while (week >= 0 && day >= 0) {
      if (stage.graph[week][day] == 0) break;
      ++G.streak;
      if (--day < 0) { day = 6; --week; }
    }
  }

  for (uint8_t week = 0; week < GITHUB_GRAPH_WEEKS; ++week) {
    for (uint8_t day = 0; day < 7; ++day) {
      const uint8_t value = stage.graph[week][day];
      if (!value) {
        G.graph[week][day] = 0;
      } else if (maximum <= 4) {
        G.graph[week][day] = value > 4 ? 4 : value;
      } else {
        const uint8_t level =
            static_cast<uint8_t>((value * 4UL + maximum - 1) / maximum);
        G.graph[week][day] = constrain(level, 1, 4);
      }
    }
  }

  G.commits = stage.commits;
  G.totalContributions = stage.totalContributions;
  G.rangeMonths = 3;
  G.calendarValid = stage.sawDay;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

namespace layout {
constexpr int16_t HeaderIconX = 8;
constexpr int16_t HeaderIconY = 6;
constexpr int16_t TitleX = 30;
constexpr int16_t TitleY = 10;
constexpr int16_t RuleY = 27;
constexpr int16_t RowTop = 33;
constexpr int16_t RowH = 42;
constexpr int16_t RowPitch = RowH + 4;
constexpr int16_t RowX = 10;
constexpr int16_t RowW = 220;
constexpr int16_t RowRight = RowX + RowW;      // exclusive
constexpr int16_t RowTextX = 44;
constexpr int16_t DotsY = 226;
// The busy lamp sits opposite the page dots, on the row they share.
constexpr int16_t BusyX = 222;
static_assert(DisplayLayout::fitsSafe(RowX, RowTop, RowW,
                                      RowPitch * (kMaxRows - 1) + RowH),
              "GitHub row list must fit the safe display area");
}  // namespace layout

Icon iconForRow(uint8_t kind, uint8_t state) {
  switch (kind) {
    case KIND_REVIEW: return Icon::MessageSquareCode;
    case KIND_MENTION: return Icon::AtSign;
    case KIND_ISSUE:
      return state == STATE_OPEN ? Icon::CircleDot : Icon::CircleCheck;
    default:
      switch (state) {
        case STATE_DRAFT: return Icon::GitPullRequestDraft;
        case STATE_MERGED: return Icon::GitMerge;
        case STATE_CLOSED: return Icon::GitPullRequestClosed;
        default: return Icon::GitPullRequest;
      }
  }
}

uint16_t colorForState(uint8_t state) {
  switch (state) {
    case STATE_DRAFT: return ST_DRAFT;
    case STATE_MERGED: return ST_MERGED;
    case STATE_CLOSED: return ST_CLOSED;
    default: return ST_OPEN;
  }
}

void drawBadge(TileCanvas& g, int16_t x, int16_t y, uint8_t state,
               bool isReview) {
  switch (state) {
    case CHECK_SUCCESS:  // shares a value with REVIEW_APPROVED
      gfxDrawIcon(g, Icon::CircleCheck14, x, y, GREEN_4);
      break;
    case CHECK_FAILURE:  // shares a value with REVIEW_CHANGES
      gfxDrawIcon(g, Icon::CircleX14, x, y, ERROR_C);
      break;
    case CHECK_PENDING:  // shares a value with REVIEW_REQUIRED
      gfxDrawIcon(g, Icon::CircleSmall14, x, y, isReview ? MUTED : AMBER);
      break;
    default:
      gfxDrawIcon(g, Icon::CircleSmall14, x, y, rgb565(72, 79, 88));
      break;
  }
}

void drawRow(TileCanvas& g, const ActivityRow& row, int16_t top, uint32_t now,
             bool showBadges) {
  using namespace layout;
  g.fillRoundRect(RowX, top, RowW, RowH, 8, PANEL);
  gfxDrawIconCentered(g, iconForRow(row.kind, row.state), 27, top + 21,
                      colorForState(row.state));

  g.setTextSize(1);

  // Line one carries everything that identifies the item; line two is left
  // entirely to the title, which is the only part that cannot be abbreviated
  // without losing meaning.
  int16_t line1Right = RowRight - 8;
  char age[8];
  formatAge(row.created, now, age, sizeof(age));
  if (age[0]) {
    g.setTextColor(MUTED);
    g.setCursor(line1Right - gfxTextW(age, 1), top + 9);
    g.print(age);
    line1Right = static_cast<int16_t>(line1Right - gfxTextW(age, 1) - 8);
  }

  char number[10];
  snprintf(number, sizeof(number), "#%u", row.number);
  const int16_t numberW = gfxTextW(number, 1);
  char repo[kRepoLen + 4];
  const int16_t repoRoom = line1Right - numberW - 5 - RowTextX;
  copyDisplayLabel(row.repo, repo, sizeof(repo),
                   static_cast<uint8_t>(max<int16_t>(1, repoRoom / 6)));
  g.setTextColor(MUTED);
  g.setCursor(RowTextX, top + 9);
  g.print(repo);
  g.setTextColor(BLUE);
  g.setCursor(RowTextX + gfxTextW(repo, 1) + 5, top + 9);
  g.print(number);

  // The badge column is reserved on the pull-request page whether or not a
  // given row has state yet, so titles never reflow between refreshes.
  const int16_t titleRight = showBadges ? RowRight - 44 : RowRight - 8;
  char title[kTitleLen + 4];
  copyDisplayLabel(row.title, title, sizeof(title),
                   static_cast<uint8_t>(max<int16_t>(1, (titleRight - RowTextX) / 6)));
  g.setTextColor(TEXT);
  g.setCursor(RowTextX, top + 24);
  g.print(title);

  if (showBadges) {
    drawBadge(g, RowRight - 39, top + 21, row.review, true);
    drawBadge(g, RowRight - 21, top + 21, row.checks, false);
  }
}

void drawEmptyState(TileCanvas& g, Icon icon, uint16_t iconColor,
                    const char* headline, const char* detail) {
  using namespace layout;
  // The empty panel occupies exactly the area the four rows would, so a page
  // does not visibly change weight when the last item is cleared.
  constexpr int16_t height = RowPitch * (kMaxRows - 1) + RowH;
  g.fillRoundRect(RowX, RowTop, RowW, height, 12, PANEL);
  gfxDrawIconCentered(g, icon, 120, RowTop + 66, iconColor);
  g.setTextSize(2);
  g.setTextColor(TEXT);
  g.setCursor((TFT_WIDTH - gfxTextW(headline, 2)) / 2, RowTop + 96);
  g.print(headline);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  // API messages are written for a browser, not a 240 px panel, so the detail
  // line is clipped to the card rather than bleeding off both edges.
  char line[40];
  copyDisplayLabel(detail, line, sizeof(line),
                   static_cast<uint8_t>((RowW - 16) / 6));
  g.setCursor((TFT_WIDTH - gfxTextW(line, 1)) / 2, RowTop + 122);
  g.print(line);
}

void drawStat(TileCanvas& g, int16_t centerX, int16_t iconY, Icon icon,
              uint32_t value, uint16_t accent) {
  gfxDrawIconCentered(g, icon, centerX, iconY, accent);
  char number[12];
  formatCompactCount(value, number, sizeof(number));
  const uint8_t size = gfxTextW(number, 2) <= 60 ? 2 : 1;
  g.setTextSize(size);
  g.setTextColor(accent);
  g.setCursor(centerX - gfxTextW(number, size) / 2,
              iconY + 14 + (size == 1 ? 4 : 0));
  g.print(number);
}

void drawInboxPage(TileCanvas& g, uint32_t now) {
  using namespace layout;
  if (!G.inboxCount) {
    drawEmptyState(g, Icon::CircleCheck24, GREEN_4, "ALL CLEAR",
                   "NOTHING IS WAITING ON YOU");
    return;
  }
  for (uint8_t i = 0; i < G.inboxCount && i < kMaxRows; ++i) {
    drawRow(g, G.inbox[i], RowTop + i * RowPitch, now, false);
  }
}

void drawPullRequestPage(TileCanvas& g, uint32_t now) {
  using namespace layout;
  if (!G.mineCount) {
    drawEmptyState(g, Icon::GitPullRequest, MUTED, "NO OPEN PRS",
                   "NOTHING OF YOURS IS IN FLIGHT");
    return;
  }
  for (uint8_t i = 0; i < G.mineCount && i < kMaxRows; ++i) {
    drawRow(g, G.mine[i], RowTop + i * RowPitch, now, true);
  }

  // A single legend line costs one row of pixels and removes the guesswork
  // about which of the two badges is review state and which is CI.
  g.setTextSize(1);
  g.setTextColor(rgb565(72, 79, 88));
  const char* legend = "REVIEW / CHECKS";
  g.setCursor(layout::RowRight - gfxTextW(legend, 1) - 4,
              RowTop + kMaxRows * RowPitch - 2);
  g.print(legend);
}

void drawPulsePage(TileCanvas& g) {
  if (!G.calendarValid) {
    // The contribution calendar is a separate polling phase, on a separate
    // schedule, needing a separate token permission. All three states are
    // reported distinctly: a pending first attempt is not a failure, and a
    // failure is worth naming rather than leaving as a silent gap.
    drawEmptyState(g, Icon::Activity, G.calendarFailed ? ERROR_C : MUTED,
                   G.calendarFailed ? "NO PULSE" : "LOADING",
                   G.calendarFailed ? G.calendarError
                                    : "FETCHING CONTRIBUTIONS");
    if (!G.calendarFailed) return;

    g.setTextSize(1);
    g.setTextColor(rgb565(72, 79, 88));
    const char* hint = "LISTS OK - CALENDAR PHASE FAILED";
    g.setCursor((TFT_WIDTH - gfxTextW(hint, 1)) / 2, layout::RowTop + 138);
    g.print(hint);

    // The phase retries on its own timer, so say when rather than leaving the
    // screen looking permanently stuck.
    char retry[28] = "";
    const uint32_t now = millis();
    if (static_cast<int32_t>(G.calendarRetryAt - now) > 0) {
      const uint32_t seconds = (G.calendarRetryAt - now) / 1000UL;
      if (seconds >= 60) {
        snprintf(retry, sizeof(retry), "RETRY IN %lum",
                 static_cast<unsigned long>(seconds / 60UL));
      } else {
        snprintf(retry, sizeof(retry), "RETRY IN %lus",
                 static_cast<unsigned long>(seconds));
      }
    } else {
      strlcpy(retry, "RETRYING", sizeof(retry));
    }
    g.setCursor((TFT_WIDTH - gfxTextW(retry, 1)) / 2, layout::RowTop + 154);
    g.print(retry);
    return;
  }
  constexpr int16_t statsY = 33;
  constexpr int16_t statsH = 58;
  static_assert(DisplayLayout::fitsSafe(10, statsY, 220, statsH),
                "GitHub stats panel must fit the safe display area");
  g.fillRoundRect(10, statsY, 220, statsH, 10, PANEL);
  g.drawFastVLine(83, statsY + 8, statsH - 16, LINE);
  g.drawFastVLine(157, statsY + 8, statsH - 16, LINE);
  const int16_t iconY = statsY + 17;
  drawStat(g, 46, iconY, Icon::CircleDot, G.openIssues, PURPLE);
  drawStat(g, 120, iconY, Icon::GitPullRequest, G.openPullRequests, BLUE);
  drawStat(g, 194, iconY, Icon::GitCommitHorizontal, G.commits, GREEN_4);

  g.setTextSize(1);
  g.setTextColor(MUTED);
  constexpr int16_t metaY = statsY + statsH + 8;
  char rangeText[8];
  snprintf(rangeText, sizeof(rangeText), "%uM", G.rangeMonths);
  g.setCursor(12, metaY);
  g.print(rangeText);

  char compactTotal[12];
  char totalText[20];
  formatCompactCount(G.totalContributions, compactTotal, sizeof(compactTotal));
  snprintf(totalText, sizeof(totalText), "%s TOTAL", compactTotal);
  g.setCursor((TFT_WIDTH - gfxTextW(totalText, 1)) / 2, metaY);
  g.print(totalText);

  char streakText[16];
  snprintf(streakText, sizeof(streakText), "%uD", G.streak);
  const int16_t streakX = 228 - gfxTextW(streakText, 1);
  g.setTextColor(G.streak ? ORANGE : MUTED);
  g.setCursor(streakX, metaY);
  g.print(streakText);
  gfxDrawIcon(g, Icon::Flame14, streakX - 17, metaY - 3,
              G.streak ? ORANGE : LINE);

  const uint16_t levelColors[5] = {PANEL, GREEN_1, GREEN_2, GREEN_3, GREEN_4};
  constexpr int16_t graphAreaX = 12;
  constexpr int16_t graphAreaY = metaY + 12;
  constexpr int16_t graphAreaW = 216;
  constexpr int16_t graphAreaH = 100;
  constexpr int16_t dayRows = 7;
  static_assert(DisplayLayout::fitsSafe(graphAreaX, graphAreaY, graphAreaW,
                                        graphAreaH),
                "GitHub contribution graph must fit the safe display area");

  const uint8_t weeks = max<uint8_t>(1, G.weekCount);
  // A day is always a 1:1 square. Shorter ranges get larger cells; the grid is
  // centered whenever its aspect ratio cannot occupy both dimensions at once.
  const int gap = weeks >= 26 ? 1 : weeks >= 10 ? 2 : 3;
  const int maxCellByWidth =
      (graphAreaW - gap * (static_cast<int>(weeks) - 1)) / weeks;
  const int maxCellByHeight = (graphAreaH - gap * (dayRows - 1)) / dayRows;
  const int cell = max(1, min(maxCellByWidth, maxCellByHeight));
  const int actualW = cell * weeks + gap * (weeks - 1);
  const int actualH = cell * dayRows + gap * (dayRows - 1);
  const int graphX = graphAreaX + (graphAreaW - actualW) / 2;
  const int graphY = graphAreaY + (graphAreaH - actualH) / 2;
  const int radius = cell >= 6 ? 1 : 0;

  for (uint8_t week = 0; week < weeks; ++week) {
    for (uint8_t day = 0; day < dayRows; ++day) {
      g.fillRoundRect(graphX + week * (cell + gap), graphY + day * (cell + gap),
                      cell, cell, radius, levelColors[G.graph[week][day]]);
    }
  }
}

struct RenderContext {
  uint8_t page;        // PAGE_* id being drawn
  uint8_t position;    // its place in the rotation, for the page dots
  uint8_t pageCount;
  uint32_t now;
};

void drawGithub(TileCanvas& g, void* context) {
  using namespace layout;
  const RenderContext& view = *static_cast<const RenderContext*>(context);
  g.fillScreen(BG);
  g.setTextWrap(false);

  if (!G.valid) {
    gfxDrawIcon(g, Icon::GitGraph, HeaderIconX, HeaderIconY, TEXT);
    g.setTextSize(1);
    g.setTextColor(MUTED);
    g.setCursor(TitleX, TitleY);
    g.print("GITHUB");
    g.fillRoundRect(10, 40, 220, 184, 14, PANEL);
    g.setTextSize(2);
    g.setTextColor(G.error ? ERROR_C : BLUE);
    g.setCursor(26, 64);
    g.print(G.error ? "API ERROR" : "LOADING");
    g.setTextSize(1);
    g.setTextColor(MUTED);
    g.setCursor(26, 99);
    g.print(G.errorText[0] ? G.errorText : "REQUESTING YOUR ACTIVITY");
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

  Icon headerIcon = Icon::Inbox;
  const char* title = "INBOX";
  char summary[24] = "";
  switch (view.page) {
    case PAGE_PULLS:
      headerIcon = Icon::GitPullRequest;
      title = "MY PULL REQUESTS";
      snprintf(summary, sizeof(summary), "%lu OPEN",
               static_cast<unsigned long>(G.openPullRequests));
      break;
    case PAGE_PULSE:
      headerIcon = Icon::Activity;
      title = "PULSE";
      snprintf(summary, sizeof(summary), "%u THIS WEEK", G.weekTotal);
      break;
    default:
      if (G.inboxTotal) {
        snprintf(summary, sizeof(summary), "%u WAITING", G.inboxTotal);
      } else {
        copyDisplayLabel(G.login, summary + 1, sizeof(summary) - 1, 18);
        summary[0] = '@';
      }
      break;
  }

  gfxDrawIcon(g, headerIcon, HeaderIconX, HeaderIconY, TEXT);
  g.setTextSize(1);
  g.setTextColor(TEXT);
  g.setCursor(TitleX, TitleY);
  g.print(title);
  if (summary[0]) {
    g.setTextColor(MUTED);
    g.setCursor(DisplayLayout::Right - gfxTextW(summary, 1), TitleY);
    g.print(summary);
  }
  // A stale snapshot keeps its content and reports the failure in the rule, so
  // a transient API error never blanks a screen that is still useful.
  g.drawFastHLine(8, RuleY, DisplayLayout::Width, G.error ? ERROR_C : LINE);

  switch (view.page) {
    case PAGE_PULLS: drawPullRequestPage(g, view.now); break;
    case PAGE_PULSE: drawPulsePage(g); break;
    default: drawInboxPage(g, view.now); break;
  }

  if (view.pageCount > 1) {
    const int16_t spacing = 10;
    const int16_t start = 120 - (view.pageCount - 1) * spacing / 2;
    for (uint8_t i = 0; i < view.pageCount; ++i) {
      g.fillCircle(start + i * spacing, DotsY, 2,
                   i == view.position ? TEXT : LINE);
    }
  }
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

// Both phases post to the same endpoint. Splitting them keeps the frequently
// refreshed action lists small: the three-month calendar is most of the bytes
// and almost none of the volatility.
static const char QUERY_LISTS[] PROGMEM =
    "query($rev:String!,$men:String!,$asg:String!,$own:String!){"
    "viewer{login "
    "openIssues:issues(states:[OPEN]){totalCount} "
    "openPrs:pullRequests(states:[OPEN]){totalCount}}"
    "rev:search(query:$rev,type:ISSUE,first:4){issueCount nodes{"
    "...on PullRequest{number title createdAt pstate:state isDraft "
    "repository{nameWithOwner}}}}"
    "men:search(query:$men,type:ISSUE,first:2){issueCount nodes{"
    "...on Issue{number title createdAt pstate:state repository{nameWithOwner}} "
    "...on PullRequest{number title createdAt pstate:state isDraft "
    "repository{nameWithOwner}}}}"
    "asg:search(query:$asg,type:ISSUE,first:3){issueCount nodes{"
    "...on Issue{number title createdAt pstate:state repository{nameWithOwner}} "
    "...on PullRequest{number title createdAt pstate:state isDraft "
    "repository{nameWithOwner}}}}"
    "own:search(query:$own,type:ISSUE,first:4){issueCount nodes{"
    "...on PullRequest{number title createdAt pstate:state isDraft "
    "review:reviewDecision repository{nameWithOwner} "
    "commits(last:1){nodes{commit{checks:statusCheckRollup{state}}}}}}}}";

// Passing the search expressions as variables keeps every quotation mark out
// of the GraphQL document, so the JSON body needs no escaping pass.
static const char VARS_LISTS[] PROGMEM =
    "{\"rev\":\"is:open is:pr review-requested:@me archived:false\","
    "\"men\":\"is:open mentions:@me archived:false\","
    "\"asg\":\"is:open assignee:@me archived:false\","
    "\"own\":\"is:pr author:@me archived:false sort:updated-desc\"}";

// Field names are aliased down to at most JsonScanner::MaxKey - 1 characters
// so the scanner can record them without truncating.
static const char QUERY_CALENDAR[] PROGMEM =
    "query($from:DateTime!,$to:DateTime!){"
    "viewer{period:contributionsCollection(from:$from,to:$to){"
    "commits:totalCommitContributions "
    "cal:contributionCalendar{total:totalContributions "
    "weeks{days:contributionDays{weekday n:contributionCount}}}}}}";

struct BodyPart {
  const char* data;
  uint16_t length;
  bool flash;
};

bool writeBody(NetClient& client, const BodyPart* parts, uint8_t count) {
  char chunk[96];
  for (uint8_t i = 0; i < count; ++i) {
    const BodyPart& part = parts[i];
    if (!part.flash) {
      if (client.write(reinterpret_cast<const uint8_t*>(part.data),
                       part.length) != part.length) {
        return false;
      }
      continue;
    }
    // PROGMEM query text is copied out in small pieces rather than staged in a
    // full-size RAM buffer; the request is assembled every poll and a
    // kilobyte-scale buffer would compete with the TLS session for heap.
    uint16_t sent = 0;
    while (sent < part.length) {
      const uint16_t take = min<uint16_t>(sizeof(chunk), part.length - sent);
      memcpy_P(chunk, part.data + sent, take);
      if (client.write(reinterpret_cast<const uint8_t*>(chunk), take) != take) {
        return false;
      }
      sent = static_cast<uint16_t>(sent + take);
      yield();
    }
  }
  return true;
}

// Called before each attempt. A first attempt that fails partway leaves the
// staging area half-populated, so a retry must start from a clean slate rather
// than append to whatever the abandoned parse committed.
using StageReset = void (*)(void* context);

bool postGraphql(const Settings& settings, uint16_t budgetMs,
                 const char* queryFlash, const char* varsData, bool varsInFlash,
                 JsonScanner::Handler handler, StageReset reset, void* context,
                 const char* graphError) {
  static const char kPrefix[] = "{\"query\":\"";
  static const char kMiddle[] = "\",\"variables\":";
  static const char kSuffix[] = "}";

  BodyPart parts[5] = {
    {kPrefix, sizeof(kPrefix) - 1, false},
    {queryFlash, static_cast<uint16_t>(strlen_P(queryFlash)), true},
    {kMiddle, sizeof(kMiddle) - 1, false},
    {varsData,
     static_cast<uint16_t>(varsInFlash ? strlen_P(varsData) : strlen(varsData)),
     varsInFlash},
    {kSuffix, sizeof(kSuffix) - 1, false},
  };
  uint32_t bodyLength = 0;
  for (const BodyPart& part : parts) bodyLength += part.length;

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    std::unique_ptr<SecureClient> client(platformMakeSecureClient());
    if (!client) {
      setError("TLS ALLOCATION FAILED");
      return false;
    }

    const uint16_t timeoutMs = min<uint16_t>(
        min<uint16_t>(settings.httpTimeout, 7000), budgetMs);
    client->setTimeout(timeoutMs);
    if (!client->connect("api.github.com", 443)) {
      if (attempt == 0) {
        delay(100);
        continue;
      }
      setError("CONNECTION FAILED");
      return false;
    }

    client->print(F("POST /graphql HTTP/1.0\r\nHost: api.github.com\r\n"));
    client->print(F("Accept: application/vnd.github+json\r\nAuthorization: Bearer "));
    client->print(settings.github.token.c_str());
    client->print(F("\r\nContent-Type: application/json\r\n"));
    client->print(F("X-GitHub-Api-Version: 2022-11-28\r\nUser-Agent: "));
    client->print(FW_NAME);
    client->print(F("\r\nContent-Length: "));
    client->print(static_cast<unsigned long>(bodyLength));
    client->print(F("\r\nConnection: close\r\n\r\n"));
    if (!writeBody(*client, parts, 5)) {
      client->stop();
      setError("REQUEST SEND FAILED");
      return false;
    }

    int code = 0;
    int contentLength = -1;
    bool chunked = false;
    // GitHub's GraphQL endpoint frames a response by closing the connection,
    // sending no Content-Length and no Transfer-Encoding, so unknown-length
    // bodies must be accepted here. JsonScanner already stops at end of
    // stream; only this check ever refused them.
    if (!httpReadResponseHeaders(*client, timeoutMs, kMaxResponseBytes, &code,
                                 &contentLength, &chunked, true)) {
      client->stop();
      if (attempt == 0 && (code <= 0 || code >= 500)) {
        delay(100);
        continue;
      }
      char detail[40];
      if (code > 0) snprintf(detail, sizeof(detail), "HTTP %d - BAD RESPONSE", code);
      else strlcpy(detail, "NO RESPONSE FROM GITHUB", sizeof(detail));
      setError(detail, code);
      return false;
    }
    if (chunked) {
      client->stop();
      setError("CHUNKED RESPONSE", code);
      return false;
    }

    Stream& stream = *client;
    reset(context);
    JsonScanner& scanner = JsonScanner::shared(
        stream, *client, contentLength, timeoutMs);
    const bool walked = scanner.walk(handler, context);
    const bool timedOut = scanner.timedOut();
    client->stop();

    if (graphError[0]) {
      setError(graphError, code);
      return false;
    }
    if (code >= 400) {
      char detail[40];
      if (code == 401) strlcpy(detail, "INVALID TOKEN", sizeof(detail));
      else if (code == 403) strlcpy(detail, "TOKEN PERMISSION", sizeof(detail));
      else snprintf(detail, sizeof(detail), "GITHUB HTTP %d", code);
      setError(detail, code);
      return false;
    }
    if (!walked) {
      if (attempt == 0 && !timedOut) {
        delay(100);
        continue;
      }
      setError(timedOut ? "GITHUB TIMED OUT" : "BAD GRAPHQL DATA", code);
      return false;
    }
    G.httpCode = code;
    return true;
  }

  setError("GITHUB REQUEST FAILED");
  return false;
}

bool tokenAndClockReady() {
  const time_t now = time(nullptr);
  if (now < 1609459200) {
    setError("WAITING FOR CLOCK");
    return false;
  }
#if defined(DESKMATE_ESP8266)
  if (!platformTlsMemoryReady()) {
    setError("LOW HEAP - RETRY LATER");
    return false;
  }
#endif
  return true;
}

bool fetchLists(const Settings& settings, uint16_t budgetMs) {
  if (!tokenAndClockReady()) return false;

  if (!postGraphql(settings, budgetMs, QUERY_LISTS, VARS_LISTS, true,
                   onListValue, resetListStage, &gListStage,
                   gListStage.graphError)) {
    return false;
  }
  if (!gListStage.sawLogin) {
    setError("BAD GRAPHQL DATA", G.httpCode);
    return false;
  }

  strlcpy(G.login, gListStage.login, sizeof(G.login));
  G.openIssues = gListStage.openIssues;
  G.openPullRequests = gListStage.openPullRequests;
  memcpy(G.inbox, gListStage.inbox, sizeof(G.inbox));
  memcpy(G.mine, gListStage.mine, sizeof(G.mine));
  G.inboxCount = gListStage.inboxCount;
  G.mineCount = gListStage.mineCount;
  G.inboxTotal = gListStage.inboxTotal;
  G.mineTotal = gListStage.mineTotal;
  G.valid = true;
  G.error = false;
  G.errorText[0] = 0;
  return true;
}

// Runs the calendar phase, keeping any failure out of the shared error state.
// The shared state belongs to the action lists, which are what the other two
// pages draw; a pulse failure must not make them look broken, and a later list
// success must not silently erase why the pulse page is empty.
bool fetchCalendar(const Settings& settings, uint16_t budgetMs) {
  const bool priorError = G.error;
  const int priorCode = G.httpCode;
  char priorText[sizeof(G.errorText)];
  strlcpy(priorText, G.errorText, sizeof(priorText));

  const auto keepCalendarFailure = [&]() {
    G.calendarFailed = true;
    strlcpy(G.calendarError, G.errorText, sizeof(G.calendarError));
    if (!G.calendarError[0]) {
      strlcpy(G.calendarError, "CONTRIBUTIONS UNAVAILABLE",
              sizeof(G.calendarError));
    }
    G.error = priorError;
    G.httpCode = priorCode;
    strlcpy(G.errorText, priorText, sizeof(G.errorText));
  };

  if (!tokenAndClockReady()) {
    keepCalendarFailure();
    return false;
  }

  const time_t now = time(nullptr);
  char periodStart[24];
  char nowIso[24];
  constexpr uint16_t rangeDays = 92;
  isoUtc(now - static_cast<time_t>(rangeDays) * 86400, periodStart,
         sizeof(periodStart));
  isoUtc(now, nowIso, sizeof(nowIso));
  char vars[72];
  snprintf(vars, sizeof(vars), "{\"from\":\"%s\",\"to\":\"%s\"}", periodStart,
           nowIso);

  if (!postGraphql(settings, budgetMs, QUERY_CALENDAR, vars, false,
                   onCalendarValue, resetCalendarStage, &gCalendarStage,
                   gCalendarStage.graphError)) {
    keepCalendarFailure();
    return false;
  }
  if (!gCalendarStage.sawDay) {
    setError("NO CONTRIBUTION DATA", G.httpCode);
    keepCalendarFailure();
    return false;
  }
  finishCalendar(gCalendarStage);
  G.calendarFailed = false;
  G.calendarError[0] = 0;
  G.error = priorError;
  G.httpCode = priorCode;
  strlcpy(G.errorText, priorText, sizeof(G.errorText));
  return true;
}
}  // namespace

uint32_t GithubMode::pollIntervalMs(const Settings& settings) const {
  return static_cast<uint32_t>(settings.github.pollSec) * 1000UL;
}

uint16_t GithubMode::pollBudgetMs(const Settings& settings) const {
  return min<uint16_t>(settings.httpTimeout, 7000);
}

bool GithubMode::calendarDue() const {
  if (!calendarAttempted_) return true;
  const uint32_t age = millis() - calendarAt_;
  return age >= (G.calendarValid ? kCalendarIntervalMs : kCalendarRetryMs);
}

PollResult GithubMode::poll(const Settings& settings, uint16_t budgetMs) {
  if (!settings.github.token.length()) {
    setError("TOKEN REQUIRED");
    dirty_ = true;
    return PollResult::Skipped;
  }
  if (!tokenAndClockReady()) {
    dirty_ = true;
    return PollResult::Skipped;
  }

  if (phase_ == Phase::Calendar) {
    // The result is deliberately not propagated: calendarDue() already backs
    // a failed calendar off to its own retry interval.
    fetchCalendar(settings, budgetMs);
    calendarAt_ = millis();
    calendarAttempted_ = true;
    G.calendarRetryAt = calendarAt_ + (G.calendarValid ? kCalendarIntervalMs
                                                       : kCalendarRetryMs);
    phase_ = Phase::Lists;
    dirty_ = true;
    // The lists this cycle already succeeded. Reporting a calendar failure as
    // a failed job would back the whole screen off, so the pulse page simply
    // retries on its own shorter schedule.
    return PollResult::Success;
  }

  const bool ok = fetchLists(settings, budgetMs);
  dirty_ = true;
  if (!ok) {
    if (!strcmp(G.errorText, "LOW HEAP - RETRY LATER")) {
      G.error = false;
      return PollResult::Skipped;
    }
    return PollResult::Failed;
  }
  if (calendarDue()) {
    phase_ = Phase::Calendar;
    return PollResult::MoreWork;
  }
  return PollResult::Success;
}

void GithubMode::begin(const Settings& settings) {
  G.rangeMonths = 3;
  phase_ = Phase::Lists;
  calendarAttempted_ = false;
  page_ = firstPage(settings);
  pageAt_ = millis();
  dirty_ = true;
}

void GithubMode::invalidate(const Settings& settings) {
  G.rangeMonths = 3;
  phase_ = Phase::Lists;
  calendarAttempted_ = false;
  page_ = firstPage(settings);
  pageAt_ = millis();
  dirty_ = true;
}

void GithubMode::wake(const Settings& settings) {
  // Whatever is waiting on the viewer is the reason to look at this screen, so
  // a carousel arrival always starts at the first selected page rather than
  // resuming mid-rotation.
  page_ = firstPage(settings);
  pageAt_ = millis();
  dirty_ = true;
}

bool GithubMode::pageEnabled(const Settings& settings, uint8_t page) const {
  switch (page) {
    case PAGE_INBOX: return settings.github.pageInbox;
    case PAGE_PULLS: return settings.github.pagePulls;
    // The pulse page is selectable but cannot be shown before its calendar
    // phase has landed, so it joins the rotation only once there is data.
    case PAGE_PULSE: return settings.github.pagePulse;
    default: return false;
  }
}

uint8_t GithubMode::pageCount(const Settings& settings) const {
  uint8_t count = 0;
  for (uint8_t page = 0; page < PAGE_TOTAL; ++page) {
    if (pageEnabled(settings, page)) ++count;
  }
  return count;
}

uint8_t GithubMode::pageIndex(const Settings& settings, uint8_t page) const {
  uint8_t index = 0;
  for (uint8_t candidate = 0; candidate < page && candidate < PAGE_TOTAL;
       ++candidate) {
    if (pageEnabled(settings, candidate)) ++index;
  }
  return index;
}

uint8_t GithubMode::firstPage(const Settings& settings) const {
  for (uint8_t page = 0; page < PAGE_TOTAL; ++page) {
    if (pageEnabled(settings, page)) return page;
  }
  return PAGE_INBOX;  // settings guarantee one selection; data may lag
}

uint8_t GithubMode::nextPage(const Settings& settings, uint8_t from) const {
  for (uint8_t step = 1; step <= PAGE_TOTAL; ++step) {
    const uint8_t page = static_cast<uint8_t>((from + step) % PAGE_TOTAL);
    if (pageEnabled(settings, page)) return page;
  }
  return from;
}

uint32_t GithubMode::pageDwellMs(const Settings& settings) const {
  // The screen's whole share of display time is divided between its selected
  // pages, so one carousel visit covers the rotation exactly once instead of
  // stopping wherever a fixed interval happened to land.
  const uint8_t pages = pageCount(settings);
  const uint32_t window = static_cast<uint32_t>(
      settings.carouselSec ? settings.carouselSec : DEFAULT_CAROUSEL_SEC) * 1000UL;
  if (pages < 2) return window;
  return max(kMinPageMs, window / pages);
}

// The ESP8266 has one core and this screen's refresh is a blocking TLS call,
// so the panel genuinely stops for a few seconds while it runs. Painting a
// lamp for exactly that interval makes the pause legible instead of looking
// like a hung rotation. Only the lamp's own rectangle is pushed, so this costs
// nothing near a full repaint.
void GithubMode::pollResultChanged(const Settings&, PollResult result) {
  indicatorResult = result;
}

void GithubMode::pollActivityChanged(const Settings& settings, bool busy) {
  (void)settings;
  if (pollBusy_ == busy) return;
  pollBusy_ = busy;
  if (busy) busySince_ = millis();
  struct LedState { uint16_t color; bool busy; };
  LedState state{busy ? BLUE : indicatorColor(), busy};
  gfxRenderRegion([](TileCanvas& g, void* context) {
    const LedState& led = *static_cast<const LedState*>(context);
    g.fillScreen(BG);
    StatusDot::draw(g, layout::BusyX, layout::DotsY, led.color, true, led.busy);
  }, &state, BG, layout::BusyX - 5, layout::DotsY - 5, 11, 11);

  // A finished refresh leaves new data behind; repaint on the next tick.
  if (!busy) dirty_ = true;
}

void GithubMode::render(const Settings& settings) {
  RenderContext view{page_, pageIndex(settings, page_), pageCount(settings),
                     static_cast<uint32_t>(time(nullptr))};
  gfxRenderTiled(drawGithub, &view, BG);
}

void GithubMode::displayTick(const Settings& settings) {
  // A page can leave the rotation while it is showing, either because it was
  // deselected or because its data was discarded.
  if (!pageEnabled(settings, page_)) {
    page_ = firstPage(settings);
    pageAt_ = millis();
    dirty_ = true;
  }
  if (G.valid && pageCount(settings) > 1) {
    const uint32_t now = millis();
    if (now - pageAt_ >= pageDwellMs(settings)) {
      pageAt_ = now;
      page_ = nextPage(settings, page_);
      dirty_ = true;
    }
  }
  if (dirty_) {
    render(settings);
    dirty_ = false;
  }
}
