#include "f1sim/support/telemetry_seed.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace f1sim::support {

namespace {

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

} // namespace

bool apply_telemetry_seed(
    const std::string& db_path,
    int season,
    int round,
    std::vector<DriverProfile>* drivers,
    std::string* error_message
) {
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        if (error_message) *error_message = sqlite3_errmsg(db);
        return false;
    }

    const char* sql = R"SQL(
        SELECT
            driver_id,
            AVG(lap_time_ms) AS avg_ms,
            AVG(lap_time_ms * lap_time_ms) AS avg_sq_ms
        FROM telemetry_lap_timings
        WHERE season = ? AND round = ? AND lap_time_ms > 0
        GROUP BY driver_id;
    )SQL";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error_message) *error_message = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int(stmt, 1, season);
    sqlite3_bind_int(stmt, 2, round);

    struct Stats {
        double avg_ms = 0.0;
        double stddev = 0.0;
    };
    std::unordered_map<std::string, Stats> by_driver;
    double best_avg = 1e18;
    double worst_avg = 0.0;
    double best_stddev = 1e18;
    double worst_stddev = 0.0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* id_raw = sqlite3_column_text(stmt, 0);
        if (!id_raw) continue;
        const std::string id = reinterpret_cast<const char*>(id_raw);
        const double avg = sqlite3_column_double(stmt, 1);
        const double avg_sq = sqlite3_column_double(stmt, 2);
        const double variance = std::max(0.0, avg_sq - (avg * avg));
        const double stddev = std::sqrt(variance);

        by_driver[id] = Stats{avg, stddev};
        best_avg = std::min(best_avg, avg);
        worst_avg = std::max(worst_avg, avg);
        best_stddev = std::min(best_stddev, stddev);
        worst_stddev = std::max(worst_stddev, stddev);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (by_driver.empty()) {
        if (error_message) *error_message = "No telemetry rows found for requested season/round.";
        return false;
    }

    const double avg_span = std::max(1.0, worst_avg - best_avg);
    const double stddev_span = std::max(1.0, worst_stddev - best_stddev);
    for (auto& d : *drivers) {
        const auto it = by_driver.find(d.id);
        if (it == by_driver.end()) continue;

        const double pace_score = 1.0 - ((it->second.avg_ms - best_avg) / avg_span);
        const double consistency_score = 1.0 - ((it->second.stddev - best_stddev) / stddev_span);

        d.skill = clamp01((d.skill * 0.45) + (pace_score * 0.55));
        d.consistency = clamp01((d.consistency * 0.35) + (consistency_score * 0.65));
        d.aggression = clamp01((d.aggression * 0.75) + ((1.0 - consistency_score) * 0.25));
    }
    return true;
}

} // namespace f1sim::support
