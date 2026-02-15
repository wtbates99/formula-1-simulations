#include <iostream>
#include <sqlite3.h>

int main() {
    sqlite3* db;
    sqlite3_stmt* stmt;

    if (sqlite3_open("f1_history.db", &db) != SQLITE_OK)
        return 1;

    const char* sql =
        "SELECT r.season_year, r.round, r.race_name, rr.position, d.family_name "
        "FROM race_results rr "
        "JOIN races r ON rr.race_id = r.race_id "
        "JOIN drivers d ON rr.driver_id = d.driver_id "
        "WHERE rr.position = 1 "
        "ORDER BY r.season_year DESC, r.round DESC "
        "LIMIT 9;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 1;

    bool printed_any = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printed_any = true;
        for (int i = 0; i < sqlite3_column_count(stmt); i++) {
            std::cout << sqlite3_column_text(stmt, i) << " ";
        }
        std::cout << '\n';
    }

    if (!printed_any)
        std::cout << "No matching race results found.\n";

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}