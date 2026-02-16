#include "f1sim/support/replay_logger.hpp"

#include <sqlite3.h>

#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace f1sim::support {

ReplayLogger::ReplayLogger() = default;

ReplayLogger::~ReplayLogger() {
    close();
}

bool ReplayLogger::exec(const char* sql, std::string* error_message) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (error_message) *error_message = err ? err : "sqlite error";
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool ReplayLogger::open(const std::string& db_path, const std::string& sim_id, std::string* error_message) {
    close();
    sim_id_ = sim_id;
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        if (error_message) *error_message = sqlite3_errmsg(db_);
        close();
        return false;
    }

    const char* create_frames = R"SQL(
        CREATE TABLE IF NOT EXISTS sim_replay_frames (
            sim_id TEXT NOT NULL,
            frame_idx INTEGER NOT NULL,
            sim_time_s REAL NOT NULL,
            car_id TEXT NOT NULL,
            team TEXT NOT NULL,
            position INTEGER NOT NULL,
            lap INTEGER NOT NULL,
            distance_total_m REAL NOT NULL,
            speed_mps REAL NOT NULL,
            tyre REAL NOT NULL,
            fuel REAL NOT NULL,
            compound TEXT NOT NULL,
            pit_stops INTEGER NOT NULL,
            in_pit INTEGER NOT NULL,
            PRIMARY KEY (sim_id, frame_idx, car_id)
        );
    )SQL";
    const char* create_pits = R"SQL(
        CREATE TABLE IF NOT EXISTS sim_replay_pit_events (
            sim_id TEXT NOT NULL,
            event_idx INTEGER NOT NULL,
            sim_time_s REAL NOT NULL,
            driver_id TEXT NOT NULL,
            lap INTEGER NOT NULL,
            from_compound TEXT NOT NULL,
            to_compound TEXT NOT NULL,
            stationary_time_s REAL NOT NULL,
            PRIMARY KEY (sim_id, event_idx)
        );
    )SQL";
    if (!exec(create_frames, error_message) || !exec(create_pits, error_message)) {
        close();
        return false;
    }
    return true;
}

bool ReplayLogger::log_frame(const f1sim::RaceSimulator& sim, int frame_idx, std::string* error_message) {
    if (!db_) return true;

    const auto board = sim.leaderboard();
    std::unordered_map<std::string, int> positions;
    for (std::size_t i = 0; i < board.size(); ++i) positions[board[i].id] = static_cast<int>(i + 1);

    const char* insert = R"SQL(
        INSERT OR REPLACE INTO sim_replay_frames
        (sim_id, frame_idx, sim_time_s, car_id, team, position, lap, distance_total_m, speed_mps, tyre, fuel, compound, pit_stops, in_pit)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insert, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error_message) *error_message = sqlite3_errmsg(db_);
        return false;
    }

    for (const auto& c : sim.cars()) {
        sqlite3_bind_text(stmt, 1, sim_id_.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, frame_idx);
        sqlite3_bind_double(stmt, 3, sim.simulation_time_seconds());
        sqlite3_bind_text(stmt, 4, c.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, c.team.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, positions[c.id]);
        sqlite3_bind_int(stmt, 7, c.lap);
        sqlite3_bind_double(stmt, 8, c.distance_total_m);
        sqlite3_bind_double(stmt, 9, c.speed_mps);
        sqlite3_bind_double(stmt, 10, c.tyre);
        sqlite3_bind_double(stmt, 11, c.fuel);
        const std::string compound = f1sim::to_string(c.compound);
        sqlite3_bind_text(stmt, 12, compound.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 13, c.pit_stops);
        sqlite3_bind_int(stmt, 14, c.in_pit ? 1 : 0);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            if (error_message) *error_message = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);
    return true;
}

bool ReplayLogger::log_new_pit_events(const f1sim::RaceSimulator& sim, std::string* error_message) {
    if (!db_) return true;

    const auto& events = sim.pit_events();
    if (pit_events_logged_ >= events.size()) return true;

    const char* insert = R"SQL(
        INSERT OR REPLACE INTO sim_replay_pit_events
        (sim_id, event_idx, sim_time_s, driver_id, lap, from_compound, to_compound, stationary_time_s)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )SQL";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insert, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error_message) *error_message = sqlite3_errmsg(db_);
        return false;
    }

    for (std::size_t i = pit_events_logged_; i < events.size(); ++i) {
        const auto& ev = events[i];
        sqlite3_bind_text(stmt, 1, sim_id_.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(i + 1));
        sqlite3_bind_double(stmt, 3, ev.sim_time_s);
        sqlite3_bind_text(stmt, 4, ev.driver_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, ev.lap);
        const std::string from = f1sim::to_string(ev.from_compound);
        const std::string to = f1sim::to_string(ev.to_compound);
        sqlite3_bind_text(stmt, 6, from.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, to.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 8, ev.stationary_time_s);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            if (error_message) *error_message = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    pit_events_logged_ = events.size();
    sqlite3_finalize(stmt);
    return true;
}

void ReplayLogger::close() {
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    pit_events_logged_ = 0;
    sim_id_.clear();
}

} // namespace f1sim::support
