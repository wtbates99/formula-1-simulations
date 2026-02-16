#include <curl/curl.h>
#include <sqlite3.h>

#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

struct LapTimingRow {
    int season = 0;
    int round = 0;
    int lap = 0;
    std::string driver_id;
    int position = 0;
    std::string lap_time;
    int lap_time_ms = -1;
};

struct PitStopRow {
    int season = 0;
    int round = 0;
    std::string driver_id;
    int stop = 0;
    int lap = 0;
    std::string time_utc_hms;
    std::string duration;
    int duration_ms = -1;
};

struct PageMeta {
    int limit = 0;
    int offset = 0;
    int total = 0;
};

struct AppConfig {
    int season = 2024;
    int round = 1;
    int page_size = 1000;
    std::string db_path = "f1_history.db";
};

static std::size_t write_body_callback(char* data, std::size_t size, std::size_t nmemb, void* user_data) {
    auto* body = static_cast<std::string*>(user_data);
    body->append(static_cast<const char*>(data), size * nmemb);
    return size * nmemb;
}

bool parse_int(const std::string& raw, int* out) {
    if (raw.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0') return false;
    *out = static_cast<int>(v);
    return true;
}

void print_usage(const char* bin) {
    std::cout << "Usage: " << bin << " [--season N] [--round N] [--page-size N] [--db PATH]\n";
}

bool parse_args(int argc, char** argv, AppConfig* cfg) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto read_value = [&](const std::string& flag) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--season") {
            const char* v = read_value(arg);
            if (!v || !parse_int(v, &cfg->season)) return false;
        } else if (arg == "--round") {
            const char* v = read_value(arg);
            if (!v || !parse_int(v, &cfg->round)) return false;
        } else if (arg == "--page-size") {
            const char* v = read_value(arg);
            if (!v || !parse_int(v, &cfg->page_size)) return false;
        } else if (arg == "--db") {
            const char* v = read_value(arg);
            if (!v) return false;
            cfg->db_path = v;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }
    if (cfg->page_size < 1) cfg->page_size = 1000;
    return true;
}

bool http_get(const std::string& url, std::string* out_body) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    out_body->clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out_body);

    const CURLcode code = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    return code == CURLE_OK && http_code == 200;
}

bool extract_json_int(const std::string& json_text, const std::string& key, int* out) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"?(\\d+)\"?");
    std::smatch match;
    if (!std::regex_search(json_text, match, pattern)) return false;
    return parse_int(match[1].str(), out);
}

bool extract_page_meta(const std::string& json_text, PageMeta* meta) {
    return extract_json_int(json_text, "limit", &meta->limit) && extract_json_int(json_text, "offset", &meta->offset) &&
           extract_json_int(json_text, "total", &meta->total);
}

int parse_lap_time_to_ms(const std::string& lap_time) {
    const std::regex ms_pattern(R"(^(\d+):(\d{2})\.(\d{3})$)");
    std::smatch match;
    if (!std::regex_match(lap_time, match, ms_pattern)) return -1;
    int minutes = 0;
    int seconds = 0;
    int millis = 0;
    if (!parse_int(match[1].str(), &minutes) || !parse_int(match[2].str(), &seconds) || !parse_int(match[3].str(), &millis)) {
        return -1;
    }
    return (minutes * 60 * 1000) + (seconds * 1000) + millis;
}

int parse_duration_to_ms(const std::string& duration) {
    const std::regex sec_pattern(R"(^(\d+)\.(\d{3})$)");
    std::smatch match;
    if (!std::regex_match(duration, match, sec_pattern)) return -1;
    int seconds = 0;
    int millis = 0;
    if (!parse_int(match[1].str(), &seconds) || !parse_int(match[2].str(), &millis)) return -1;
    return (seconds * 1000) + millis;
}

std::vector<LapTimingRow> parse_lap_timings(const std::string& json_text, int season, int round) {
    std::vector<LapTimingRow> rows;
    const std::regex lap_block_pattern(R"REGEX(\{\s*"number"\s*:\s*"(\d+)"\s*,\s*"Timings"\s*:\s*\[([\s\S]*?)\]\s*\})REGEX");
    const std::regex timing_pattern(
        R"REGEX(\{\s*"driverId"\s*:\s*"([^"]+)"\s*,\s*"position"\s*:\s*"([^"]+)"\s*,\s*"time"\s*:\s*"([^"]+)"\s*\})REGEX");

    auto lap_begin = std::sregex_iterator(json_text.begin(), json_text.end(), lap_block_pattern);
    auto lap_end = std::sregex_iterator();
    for (auto lap_it = lap_begin; lap_it != lap_end; ++lap_it) {
        int lap_number = 0;
        if (!parse_int((*lap_it)[1].str(), &lap_number)) continue;
        const std::string timings_block = (*lap_it)[2].str();
        auto timing_begin = std::sregex_iterator(timings_block.begin(), timings_block.end(), timing_pattern);
        auto timing_end = std::sregex_iterator();
        for (auto timing_it = timing_begin; timing_it != timing_end; ++timing_it) {
            LapTimingRow row;
            row.season = season;
            row.round = round;
            row.lap = lap_number;
            row.driver_id = (*timing_it)[1].str();
            if (!parse_int((*timing_it)[2].str(), &row.position)) continue;
            row.lap_time = (*timing_it)[3].str();
            row.lap_time_ms = parse_lap_time_to_ms(row.lap_time);
            rows.push_back(row);
        }
    }
    return rows;
}

std::vector<PitStopRow> parse_pit_stops(const std::string& json_text, int season, int round) {
    std::vector<PitStopRow> rows;
    const std::regex pit_pattern(
        R"REGEX(\{\s*"driverId"\s*:\s*"([^"]+)"\s*,\s*"lap"\s*:\s*"([^"]+)"\s*,\s*"stop"\s*:\s*"([^"]+)"\s*,\s*"time"\s*:\s*"([^"]+)"\s*,\s*"duration"\s*:\s*"([^"]+)"\s*\})REGEX");

    auto begin = std::sregex_iterator(json_text.begin(), json_text.end(), pit_pattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        PitStopRow row;
        row.season = season;
        row.round = round;
        row.driver_id = (*it)[1].str();
        if (!parse_int((*it)[2].str(), &row.lap)) continue;
        if (!parse_int((*it)[3].str(), &row.stop)) continue;
        row.time_utc_hms = (*it)[4].str();
        row.duration = (*it)[5].str();
        row.duration_ms = parse_duration_to_ms(row.duration);
        rows.push_back(row);
    }
    return rows;
}

bool exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "SQLite error: " << (err ? err : "unknown") << "\n";
        sqlite3_free(err);
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    AppConfig cfg;
    if (!parse_args(argc, argv, &cfg)) return 1;

    std::vector<LapTimingRow> all_lap_rows;
    for (int offset = 0;;) {
        const std::string lap_url = "https://api.jolpi.ca/ergast/f1/" + std::to_string(cfg.season) + "/" +
                                    std::to_string(cfg.round) + "/laps.json?limit=" + std::to_string(cfg.page_size) +
                                    "&offset=" + std::to_string(offset);
        std::string body;
        if (!http_get(lap_url, &body)) {
            std::cerr << "Failed to fetch lap telemetry API: " << lap_url << "\n";
            return 1;
        }
        PageMeta meta;
        if (!extract_page_meta(body, &meta) || meta.limit < 1) {
            std::cerr << "Could not read valid pagination metadata from laps response.\n";
            return 1;
        }
        const auto batch_rows = parse_lap_timings(body, cfg.season, cfg.round);
        all_lap_rows.insert(all_lap_rows.end(), batch_rows.begin(), batch_rows.end());
        if (meta.offset + meta.limit >= meta.total) break;
        offset = meta.offset + meta.limit;
    }
    if (all_lap_rows.empty()) {
        std::cerr << "No lap timing telemetry found.\n";
        return 1;
    }

    std::vector<PitStopRow> all_pit_rows;
    for (int offset = 0;;) {
        const std::string pit_url = "https://api.jolpi.ca/ergast/f1/" + std::to_string(cfg.season) + "/" +
                                    std::to_string(cfg.round) + "/pitstops.json?limit=" + std::to_string(cfg.page_size) +
                                    "&offset=" + std::to_string(offset);
        std::string body;
        if (!http_get(pit_url, &body)) {
            std::cerr << "Failed to fetch pit-stop telemetry API: " << pit_url << "\n";
            return 1;
        }
        PageMeta meta;
        if (!extract_page_meta(body, &meta) || meta.limit < 1) {
            std::cerr << "Could not read valid pagination metadata from pit-stops response.\n";
            return 1;
        }
        const auto batch_rows = parse_pit_stops(body, cfg.season, cfg.round);
        all_pit_rows.insert(all_pit_rows.end(), batch_rows.begin(), batch_rows.end());
        if (meta.offset + meta.limit >= meta.total) break;
        offset = meta.offset + meta.limit;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(cfg.db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open db: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }

    const char* create_lap_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS telemetry_lap_timings (
            season INTEGER NOT NULL,
            round INTEGER NOT NULL,
            lap INTEGER NOT NULL,
            driver_id TEXT NOT NULL,
            position INTEGER NOT NULL,
            lap_time TEXT NOT NULL,
            lap_time_ms INTEGER,
            PRIMARY KEY (season, round, lap, driver_id)
        );
    )SQL";
    const char* create_pit_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS telemetry_pit_stops (
            season INTEGER NOT NULL,
            round INTEGER NOT NULL,
            driver_id TEXT NOT NULL,
            stop INTEGER NOT NULL,
            lap INTEGER NOT NULL,
            pit_time_hms TEXT NOT NULL,
            duration TEXT NOT NULL,
            duration_ms INTEGER,
            PRIMARY KEY (season, round, driver_id, stop)
        );
    )SQL";

    if (!exec_sql(db, create_lap_sql) || !exec_sql(db, create_pit_sql) || !exec_sql(db, "BEGIN;")) {
        sqlite3_close(db);
        return 1;
    }

    const char* insert_lap_sql = R"SQL(
        INSERT INTO telemetry_lap_timings (season, round, lap, driver_id, position, lap_time, lap_time_ms)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(season, round, lap, driver_id) DO UPDATE SET
            position = excluded.position,
            lap_time = excluded.lap_time,
            lap_time_ms = excluded.lap_time_ms;
    )SQL";
    const char* insert_pit_sql = R"SQL(
        INSERT INTO telemetry_pit_stops (season, round, driver_id, stop, lap, pit_time_hms, duration, duration_ms)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(season, round, driver_id, stop) DO UPDATE SET
            lap = excluded.lap,
            pit_time_hms = excluded.pit_time_hms,
            duration = excluded.duration,
            duration_ms = excluded.duration_ms;
    )SQL";

    sqlite3_stmt* lap_stmt = nullptr;
    sqlite3_stmt* pit_stmt = nullptr;
    if (sqlite3_prepare_v2(db, insert_lap_sql, -1, &lap_stmt, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(db, insert_pit_sql, -1, &pit_stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare inserts: " << sqlite3_errmsg(db) << "\n";
        exec_sql(db, "ROLLBACK;");
        if (lap_stmt) sqlite3_finalize(lap_stmt);
        if (pit_stmt) sqlite3_finalize(pit_stmt);
        sqlite3_close(db);
        return 1;
    }

    int inserted_laps = 0;
    for (const auto& row : all_lap_rows) {
        sqlite3_bind_int(lap_stmt, 1, row.season);
        sqlite3_bind_int(lap_stmt, 2, row.round);
        sqlite3_bind_int(lap_stmt, 3, row.lap);
        sqlite3_bind_text(lap_stmt, 4, row.driver_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(lap_stmt, 5, row.position);
        sqlite3_bind_text(lap_stmt, 6, row.lap_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(lap_stmt, 7, row.lap_time_ms);

        if (sqlite3_step(lap_stmt) == SQLITE_DONE) inserted_laps++;
        sqlite3_reset(lap_stmt);
        sqlite3_clear_bindings(lap_stmt);
    }

    int inserted_pits = 0;
    for (const auto& row : all_pit_rows) {
        sqlite3_bind_int(pit_stmt, 1, row.season);
        sqlite3_bind_int(pit_stmt, 2, row.round);
        sqlite3_bind_text(pit_stmt, 3, row.driver_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pit_stmt, 4, row.stop);
        sqlite3_bind_int(pit_stmt, 5, row.lap);
        sqlite3_bind_text(pit_stmt, 6, row.time_utc_hms.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(pit_stmt, 7, row.duration.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pit_stmt, 8, row.duration_ms);

        if (sqlite3_step(pit_stmt) == SQLITE_DONE) inserted_pits++;
        sqlite3_reset(pit_stmt);
        sqlite3_clear_bindings(pit_stmt);
    }

    sqlite3_finalize(lap_stmt);
    sqlite3_finalize(pit_stmt);
    if (!exec_sql(db, "COMMIT;")) {
        sqlite3_close(db);
        return 1;
    }
    sqlite3_close(db);

    std::cout << "Stored " << inserted_laps << " lap timing rows and " << inserted_pits << " pit-stop rows into "
              << cfg.db_path << "\n";
    return 0;
}