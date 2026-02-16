#pragma once

#include "f1sim/sim/simulator.hpp"

#include <string>
#include <vector>

struct sqlite3;

namespace f1sim::support {

class ReplayLogger {
public:
    ReplayLogger();
    ~ReplayLogger();

    bool open(const std::string& db_path, const std::string& sim_id, std::string* error_message);
    bool log_frame(const f1sim::RaceSimulator& sim, int frame_idx, std::string* error_message);
    bool log_new_pit_events(const f1sim::RaceSimulator& sim, std::string* error_message);
    void close();

private:
    sqlite3* db_ = nullptr;
    std::string sim_id_;
    std::size_t pit_events_logged_ = 0;

    bool exec(const char* sql, std::string* error_message);
};

} // namespace f1sim::support
