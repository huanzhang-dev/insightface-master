// db_convert.cpp - convert the database of registered people from one file format to the other.
//
//   db_convert <input> <output> [--format csv|sqlite] [--force]
//
// Reads the input database (CSV or SQLite, recognised by its content), checks it by the same rules as
// the program does when it opens a database, and writes the output database. The output format is the
// one asked for with --format; without it the file name decides (.db .sqlite .sqlite3 = SQLite,
// anything else = CSV, the default). Every person is converted with his name, RF card number and the
// exact feature numbers. An existing output file is not overwritten unless --force is given; the input
// file is never changed.
//
// Examples
//   db_convert people.csv people.db                  CSV -> SQLite
//   db_convert people.db people_copy.csv             SQLite -> CSV
//   db_convert people.csv backup.dat --format sqlite SQLite file with a name of your choice
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "feature_store.h"
#include "sqlite_db.h"
#include "timer.h"

using namespace fa;

static long long file_size(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f ? static_cast<long long>(f.tellg()) : -1;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string input, output;
    DatabaseFormat format = DatabaseFormat::Auto;
    bool force = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--force")) {
            force = true;
        } else if (!std::strcmp(argv[i], "--format")) {
            const char* value = i + 1 < argc ? argv[++i] : "";
            if (!std::strcmp(value, "csv")) format = DatabaseFormat::Csv;
            else if (!std::strcmp(value, "sqlite")) format = DatabaseFormat::Sqlite;
            else {
                std::printf("--format needs csv or sqlite\n");
                return 2;
            }
        } else if (input.empty()) {
            input = argv[i];
        } else if (output.empty()) {
            output = argv[i];
        } else {
            std::printf("too many arguments\n");
            return 2;
        }
    }
    if (input.empty() || output.empty()) {
        std::printf("usage: db_convert <input> <output> [--format csv|sqlite] [--force]\n"
                    "  converts a database of registered people between CSV (default) and SQLite;\n"
                    "  without --format the output file name decides (.db .sqlite .sqlite3 = SQLite, else CSV)\n");
        return 2;
    }
    if (input == output) {
        std::printf("the input and the output are the same file\n");
        return 2;
    }
    if (file_size(output) >= 0 && !force) {
        std::printf("%s exists already (use --force to replace it)\n", output.c_str());
        return 2;
    }

    const double start = now_ms();
    FeatureStore store;
    if (!store.load(input)) {  // the format is recognised by the file's content
        std::printf("cannot read %s: %s\n", input.c_str(), store.last_error().c_str());
        return 1;
    }
    const bool input_is_sqlite = is_sqlite_file(input);
    if (!store.save(output, format)) {
        std::printf("cannot write %s: %s\n", output.c_str(), store.last_error().c_str());
        return 1;
    }
    // read the new file back and compare, so that a conversion is only reported as good when it is
    FeatureStore check;
    bool same = check.load(output) && check.count() == store.count();
    for (int i = 0; same && i < store.count(); ++i) {
        const Record &a = store.records()[i], &b = check.records()[i];
        same = a.id == b.id && a.name == b.name && a.card_id == b.card_id && a.feature.values == b.feature.values;
    }
    if (!same) {
        std::printf("the written file %s does not read back the same: %s\n", output.c_str(), check.last_error().c_str());
        return 1;
    }
    int with_cards = 0;
    for (const Record& r : store.records())
        if (!r.card_id.empty()) ++with_cards;
    std::printf("converted %d people (%d with an RF card, features of %d numbers)\n", store.count(), with_cards,
                store.count() ? store.records()[0].feature.dim() : 0);
    std::printf("  %s  %s  %.1f MB\n", input.c_str(), input_is_sqlite ? "SQLite" : "CSV   ", file_size(input) / 1048576.0);
    std::printf("  %s  %s  %.1f MB   (read back and compared: identical; %.0f ms)\n", output.c_str(), is_sqlite_file(output) ? "SQLite" : "CSV   ",
                file_size(output) / 1048576.0, now_ms() - start);
    return 0;
}
