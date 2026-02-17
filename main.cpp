#include <curl/curl.h>
#include <sqlite3.h>

#include "f1sim/sim/simulator.hpp"
#include "f1sim/support/replay_logger.hpp"
#include "f1sim/support/scenario_loader.hpp"
#include "f1sim/support/telemetry_seed.hpp"

#include <cstdlib>
#include <iomanip>
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
    int from_year = -1;
    int to_year = -1;
    int page_size = 1000;
    bool all_rounds = false;
    bool continue_on_error = false;
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

std::string prompt_line(const std::string& label, const std::string& default_value) {
    std::cout << label << " [" << default_value << "]: ";
    std::string line;
    std::getline(std::cin, line);
    if (line.empty()) return default_value;
    return line;
}

int prompt_int(const std::string& label, int default_value) {
    while (true) {
        const std::string raw = prompt_line(label, std::to_string(default_value));
        int value = default_value;
        if (parse_int(raw, &value)) return value;
        std::cout << "Please enter a valid integer.\n";
    }
}

double prompt_double(const std::string& label, double default_value) {
    while (true) {
        const std::string raw = prompt_line(label, std::to_string(default_value));
        char* end = nullptr;
        const double value = std::strtod(raw.c_str(), &end);
        if (end != raw.c_str() && *end == '\0') return value;
        std::cout << "Please enter a valid number.\n";
    }
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

int fetch_round_count_for_season(int season) {
    const std::string season_url =
        "https://api.jolpi.ca/ergast/f1/" + std::to_string(season) + ".json?limit=1000&offset=0";
    std::string body;
    if (!http_get(season_url, &body)) return -1;

    const std::regex round_re(R"REGEX("round"\s*:\s*"(\d+)")REGEX");
    auto begin = std::sregex_iterator(body.begin(), body.end(), round_re);
    auto end = std::sregex_iterator();

    int max_round = 0;
    for (auto it = begin; it != end; ++it) {
        int round = 0;
        if (!parse_int((*it)[1].str(), &round)) continue;
        if (round > max_round) max_round = round;
    }
    return max_round;
}

bool ingest_single_race(const AppConfig& cfg, int season, int round, int* inserted_laps, int* inserted_pits) {
    std::vector<LapTimingRow> all_lap_rows;
    for (int offset = 0;;) {
        const std::string lap_url = "https://api.jolpi.ca/ergast/f1/" + std::to_string(season) + "/" +
                                    std::to_string(round) + "/laps.json?limit=" + std::to_string(cfg.page_size) +
                                    "&offset=" + std::to_string(offset);
        std::string body;
        if (!http_get(lap_url, &body)) {
            std::cerr << "Failed to fetch lap telemetry API: " << lap_url << "\n";
            return false;
        }
        PageMeta meta;
        if (!extract_page_meta(body, &meta) || meta.limit < 1) {
            std::cerr << "Could not read valid pagination metadata from laps response.\n";
            return false;
        }
        const auto batch_rows = parse_lap_timings(body, season, round);
        all_lap_rows.insert(all_lap_rows.end(), batch_rows.begin(), batch_rows.end());
        if (meta.offset + meta.limit >= meta.total) break;
        offset = meta.offset + meta.limit;
    }
    if (all_lap_rows.empty()) {
        std::cerr << "No lap timing telemetry found for season " << season << " round " << round << ".\n";
        return false;
    }

    std::vector<PitStopRow> all_pit_rows;
    for (int offset = 0;;) {
        const std::string pit_url = "https://api.jolpi.ca/ergast/f1/" + std::to_string(season) + "/" +
                                    std::to_string(round) + "/pitstops.json?limit=" + std::to_string(cfg.page_size) +
                                    "&offset=" + std::to_string(offset);
        std::string body;
        if (!http_get(pit_url, &body)) {
            std::cerr << "Failed to fetch pit-stop telemetry API: " << pit_url << "\n";
            return false;
        }
        PageMeta meta;
        if (!extract_page_meta(body, &meta) || meta.limit < 1) {
            std::cerr << "Could not read valid pagination metadata from pit-stops response.\n";
            return false;
        }
        const auto batch_rows = parse_pit_stops(body, season, round);
        all_pit_rows.insert(all_pit_rows.end(), batch_rows.begin(), batch_rows.end());
        if (meta.offset + meta.limit >= meta.total) break;
        offset = meta.offset + meta.limit;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(cfg.db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open db: " << sqlite3_errmsg(db) << "\n";
        return false;
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
        return false;
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
        return false;
    }

    *inserted_laps = 0;
    for (const auto& row : all_lap_rows) {
        sqlite3_bind_int(lap_stmt, 1, row.season);
        sqlite3_bind_int(lap_stmt, 2, row.round);
        sqlite3_bind_int(lap_stmt, 3, row.lap);
        sqlite3_bind_text(lap_stmt, 4, row.driver_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(lap_stmt, 5, row.position);
        sqlite3_bind_text(lap_stmt, 6, row.lap_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(lap_stmt, 7, row.lap_time_ms);

        if (sqlite3_step(lap_stmt) == SQLITE_DONE) (*inserted_laps)++;
        sqlite3_reset(lap_stmt);
        sqlite3_clear_bindings(lap_stmt);
    }

    *inserted_pits = 0;
    for (const auto& row : all_pit_rows) {
        sqlite3_bind_int(pit_stmt, 1, row.season);
        sqlite3_bind_int(pit_stmt, 2, row.round);
        sqlite3_bind_text(pit_stmt, 3, row.driver_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pit_stmt, 4, row.stop);
        sqlite3_bind_int(pit_stmt, 5, row.lap);
        sqlite3_bind_text(pit_stmt, 6, row.time_utc_hms.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(pit_stmt, 7, row.duration.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pit_stmt, 8, row.duration_ms);

        if (sqlite3_step(pit_stmt) == SQLITE_DONE) (*inserted_pits)++;
        sqlite3_reset(pit_stmt);
        sqlite3_clear_bindings(pit_stmt);
    }

    sqlite3_finalize(lap_stmt);
    sqlite3_finalize(pit_stmt);
    if (!exec_sql(db, "COMMIT;")) {
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);
    return true;
}

void run_single_race_ingest() {
    AppConfig cfg;
    cfg.db_path = prompt_line("SQLite DB path", "telemetry.db");
    cfg.season = prompt_int("Season", 2024);
    cfg.round = prompt_int("Round", 1);
    cfg.page_size = prompt_int("Page size", 1000);

    int laps = 0;
    int pits = 0;
    if (!ingest_single_race(cfg, cfg.season, cfg.round, &laps, &pits)) {
        std::cout << "Ingest failed.\n";
        return;
    }
    std::cout << "Stored " << laps << " lap timings and " << pits << " pit stops into " << cfg.db_path << "\n";
}

void run_full_ingest() {
    AppConfig cfg;
    cfg.db_path = prompt_line("SQLite DB path", "telemetry.db");
    cfg.from_year = prompt_int("From year", 1950);
    cfg.to_year = prompt_int("To year", 2025);
    cfg.page_size = prompt_int("Page size", 1000);
    cfg.continue_on_error = true;
    const std::string coe = prompt_line("Continue on errors? (y/n)", "y");
    if (!coe.empty() && (coe[0] == 'n' || coe[0] == 'N')) cfg.continue_on_error = false;

    int total_laps = 0;
    int total_pits = 0;
    int races_ok = 0;
    int races_failed = 0;
    for (int season = cfg.from_year; season <= cfg.to_year; ++season) {
        const int rounds = fetch_round_count_for_season(season);
        if (rounds < 1) {
            std::cout << "Season " << season << ": could not determine rounds.\n";
            races_failed++;
            if (!cfg.continue_on_error) break;
            continue;
        }
        std::cout << "Season " << season << ": " << rounds << " rounds\n";
        for (int round = 1; round <= rounds; ++round) {
            int laps = 0;
            int pits = 0;
            std::cout << "  Ingesting round " << round << "... ";
            if (!ingest_single_race(cfg, season, round, &laps, &pits)) {
                std::cout << "failed\n";
                races_failed++;
                if (!cfg.continue_on_error) break;
                continue;
            }
            std::cout << "ok (" << laps << " laps, " << pits << " pits)\n";
            total_laps += laps;
            total_pits += pits;
            races_ok++;
        }
        if (races_failed > 0 && !cfg.continue_on_error) break;
    }
    std::cout << "Done. Races ok: " << races_ok << ", failed: " << races_failed << ", rows: " << total_laps
              << " lap timings, " << total_pits << " pit stops.\n";
}

void run_simulation_text_mode() {
    f1sim::SimConfig config;
    std::vector<f1sim::DriverProfile> drivers = f1sim::build_demo_grid();

    const std::string scenario = prompt_line("Scenario path", "examples/scenarios/short_race.json");
    const std::string telemetry_db = prompt_line("Telemetry DB path", "telemetry.db");
    const std::string replay_db = prompt_line("Replay DB path", "sim_replay.db");
    const int season = prompt_int("Season for telemetry seeding", 2024);
    const int round = prompt_int("Round for telemetry seeding", 1);
    const double tick_seconds = prompt_double("Tick seconds per print", 5.0);

    std::string err;
    if (!f1sim::support::load_scenario_json(scenario, &config, &drivers, &err)) {
        std::cout << "Scenario load failed: " << err << "\n";
        return;
    }
    if (!f1sim::support::apply_telemetry_seed(telemetry_db, season, round, &drivers, &err)) {
        std::cout << "Telemetry seed warning: " << err << "\n";
    }

    f1sim::RaceSimulator sim(config, drivers);
    f1sim::support::ReplayLogger logger;
    if (!logger.open(replay_db, "interactive_sim_s" + std::to_string(season) + "_r" + std::to_string(round), &err)) {
        std::cout << "Replay logger warning: " << err << "\n";
    }

    int frame_idx = 0;
    while (!sim.all_finished()) {
        sim.run_for(tick_seconds);
        frame_idx++;
        logger.log_frame(sim, frame_idx, &err);
        logger.log_new_pit_events(sim, &err);

        const auto board = sim.leaderboard();
        std::cout << "\nT+" << static_cast<int>(sim.simulation_time_seconds()) << "s lap " << sim.leader_lap() << "/"
                  << config.total_laps << "\n";
        std::cout << "pos driver            lap speed(km/h) tyre fuel cmp pits\n";
        for (std::size_t i = 0; i < board.size() && i < 6; ++i) {
            const auto& c = board[i];
            std::cout << std::setw(3) << (i + 1) << " " << std::left << std::setw(16) << c.id << std::right << " "
                      << std::setw(3) << c.lap << " " << std::setw(10) << std::fixed << std::setprecision(1)
                      << (c.speed_mps * 3.6) << " " << std::setw(4) << std::setprecision(2) << c.tyre << " "
                      << std::setw(4) << c.fuel << " " << std::setw(6) << f1sim::to_string(c.compound) << " "
                      << std::setw(3) << c.pit_stops << "\n";
        }
    }
    std::cout << "\nSimulation complete.\n";
}

void show_quick_db_counts() {
    const std::string db_path = prompt_line("SQLite DB path", "telemetry.db");
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cout << "Failed to open DB.\n";
        return;
    }

    const char* sql = R"SQL(
        SELECT
          (SELECT COUNT(*) FROM telemetry_lap_timings),
          (SELECT COUNT(*) FROM telemetry_pit_stops);
    )SQL";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cout << "Failed to query DB.\n";
        sqlite3_close(db);
        return;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const int laps = sqlite3_column_int(stmt, 0);
        const int pits = sqlite3_column_int(stmt, 1);
        std::cout << "telemetry_lap_timings rows: " << laps << "\n";
        std::cout << "telemetry_pit_stops rows: " << pits << "\n";
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

int main() {
    std::cout << "F1 CLI\n";
    std::cout << "Type a menu number and press Enter.\n";
    while (true) {
        std::cout << "\n--- Main Menu ---\n";
        std::cout << "1) Ingest one race telemetry\n";
        std::cout << "2) Full telemetry pull (year range, all rounds)\n";
        std::cout << "3) Run text simulation\n";
        std::cout << "4) Show telemetry row counts\n";
        std::cout << "5) Exit\n";
        std::cout << "> ";
        std::string choice;
        std::getline(std::cin, choice);
        if (choice == "1") {
            run_single_race_ingest();
        } else if (choice == "2") {
            run_full_ingest();
        } else if (choice == "3") {
            run_simulation_text_mode();
        } else if (choice == "4") {
            show_quick_db_counts();
        } else if (choice == "5" || choice == "q" || choice == "quit" || choice == "exit") {
            break;
        } else {
            std::cout << "Unknown choice. Use 1-5.\n";
        }
    }
    std::cout << "Bye.\n";
    return 0;
}