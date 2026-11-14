// sqlite_db.cpp - see sqlite_db.h.
#include "sqlite_db.h"

#include <cstring>
#include <fstream>

#include "sqlite3.h"

namespace fa {
namespace {

const int kLayoutVersion = 1;       // PRAGMA user_version of the table layout described in sqlite_db.h
const int kBusyTimeoutMs = 500;     // how long a change waits for another program's lock on the file

const char* const kCreateTable =
    "CREATE TABLE people ("
    " id TEXT NOT NULL PRIMARY KEY,"
    " name TEXT NOT NULL,"
    " card_id TEXT UNIQUE,"
    " dim INTEGER NOT NULL CHECK (dim > 0),"
    " feature BLOB NOT NULL CHECK (length(feature) = dim * 4));";

// A prepared statement that is finalized when it goes out of scope.
class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) stmt_ = nullptr;
    }
    ~Statement() {
        if (stmt_) sqlite3_finalize(stmt_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    sqlite3_stmt* get() const { return stmt_; }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

std::string failure(sqlite3* db, const std::string& what) {
    return what + ": " + (db ? sqlite3_errmsg(db) : "no database");
}

std::string text_column(sqlite3_stmt* s, int column) {
    const unsigned char* t = sqlite3_column_text(s, column);  // null for a NULL value
    return t ? std::string(reinterpret_cast<const char*>(t), static_cast<size_t>(sqlite3_column_bytes(s, column))) : std::string();
}

// Bind the five columns of a record to parameters ?1 .. ?5 (id, name, card_id, dim, feature).
bool bind_record(sqlite3_stmt* s, const Record& r) {
    bool ok = sqlite3_bind_text(s, 1, r.id.c_str(), static_cast<int>(r.id.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    ok = ok && sqlite3_bind_text(s, 2, r.name.c_str(), static_cast<int>(r.name.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    // no card = NULL (several people may have none; a real card number is unique)
    ok = ok && (r.card_id.empty() ? sqlite3_bind_null(s, 3)
                                  : sqlite3_bind_text(s, 3, r.card_id.c_str(), static_cast<int>(r.card_id.size()), SQLITE_TRANSIENT)) == SQLITE_OK;
    ok = ok && sqlite3_bind_int(s, 4, r.feature.dim()) == SQLITE_OK;
    ok = ok && sqlite3_bind_blob(s, 5, r.feature.values.data(), r.feature.dim() * static_cast<int>(sizeof(float)), SQLITE_TRANSIENT) == SQLITE_OK;
    return ok;
}

}  // namespace

bool is_sqlite_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    char header[16] = {0};
    file.read(header, sizeof(header));
    return file.gcount() == 16 && std::memcmp(header, "SQLite format 3", 16) == 0;  // 15 characters and the terminating zero
}

bool SqliteDb::exec(const char* sql, std::string& error) {
    char* message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        error = std::string(message ? message : "SQLite error");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool SqliteDb::open(const std::string& path, bool create, std::string& error) {
    close();
    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0), nullptr);
    if (rc != SQLITE_OK) {
        error = failure(db, "cannot open the SQLite database " + path);
        sqlite3_close(db);
        return false;
    }
    db_ = db;
    sqlite3_busy_timeout(db_, kBusyTimeoutMs);

    // Which layout does the file have? (A file that is not a SQLite database fails right here.)
    int version = 0;
    {
        Statement s(db_, "PRAGMA user_version");
        if (!s.get() || sqlite3_step(s.get()) != SQLITE_ROW) {
            error = failure(db_, "cannot read " + path + " as a SQLite database");
            close();
            return false;
        }
        version = sqlite3_column_int(s.get(), 0);
    }
    bool has_table = false;
    {
        Statement s(db_, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'people'");
        has_table = s.get() && sqlite3_step(s.get()) == SQLITE_ROW;
    }
    if (version > kLayoutVersion) {
        error = path + " has table layout " + std::to_string(version) + ", newer than this program understands (" + std::to_string(kLayoutVersion) + ")";
        close();
        return false;
    }
    if (version == 0 && !has_table && create) {  // a new (empty) file: make the table
        if (!exec(("BEGIN; " + std::string(kCreateTable) + " PRAGMA user_version = " + std::to_string(kLayoutVersion) + "; COMMIT;").c_str(), error)) {
            error = "cannot create the table in " + path + ": " + error;
            std::string ignored;
            exec("ROLLBACK", ignored);
            close();
            return false;
        }
        return true;
    }
    if (version != kLayoutVersion || !has_table) {
        error = path + " is not a face_auth database (no table 'people' of layout " + std::to_string(kLayoutVersion) + ")";
        close();
        return false;
    }
    return true;
}

void SqliteDb::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteDb::load_all(std::vector<Record>& records, std::string& error) {
    records.clear();
    if (!db_) {
        error = "the SQLite database is not open";
        return false;
    }
    Statement s(db_, "SELECT id, name, card_id, dim, feature FROM people ORDER BY rowid");
    if (!s.get()) {
        error = failure(db_, "cannot read the table people");
        return false;
    }
    int step;
    while ((step = sqlite3_step(s.get())) == SQLITE_ROW) {
        Record r;
        r.id = text_column(s.get(), 0);
        r.name = text_column(s.get(), 1);
        r.card_id = text_column(s.get(), 2);
        const sqlite3_int64 dim = sqlite3_column_int64(s.get(), 3);
        const int bytes = sqlite3_column_bytes(s.get(), 4);
        const void* blob = sqlite3_column_blob(s.get(), 4);
        // Validate in 64 bits before allocation; SQLite integers need not fit in an int.
        // Compare against the blob length before multiplying to avoid integer overflow.
        if (sqlite3_column_type(s.get(), 3) != SQLITE_INTEGER || dim <= 0 || !blob ||
            bytes % sizeof(float) != 0 || dim != bytes / static_cast<int>(sizeof(float))) {
            error = "row '" + r.id + "': the feature does not have " + std::to_string(dim) + " numbers";
            records.clear();
            return false;
        }
        r.feature.values.resize(dim);
        std::memcpy(r.feature.values.data(), blob, static_cast<size_t>(bytes));
        records.push_back(std::move(r));
    }
    if (step != SQLITE_DONE) {
        error = failure(db_, "cannot read the table people");
        records.clear();
        return false;
    }
    return true;
}

bool SqliteDb::insert(const Record& record, std::string& error) {
    if (!db_) {
        error = "the SQLite database is not open";
        return false;
    }
    Statement s(db_, "INSERT INTO people (id, name, card_id, dim, feature) VALUES (?1, ?2, ?3, ?4, ?5)");
    if (!s.get() || !bind_record(s.get(), record) || sqlite3_step(s.get()) != SQLITE_DONE) {
        error = failure(db_, "cannot insert '" + record.id + "'");
        return false;
    }
    return true;
}

bool SqliteDb::erase(const std::string& id, std::string& error) {
    if (!db_) {
        error = "the SQLite database is not open";
        return false;
    }
    Statement s(db_, "DELETE FROM people WHERE id = ?1");
    if (!s.get() || sqlite3_bind_text(s.get(), 1, id.c_str(), static_cast<int>(id.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(s.get()) != SQLITE_DONE) {
        error = failure(db_, "cannot delete '" + id + "'");
        return false;
    }
    if (sqlite3_changes(db_) != 1) {
        error = "unknown id in the database file: " + id;
        return false;
    }
    return true;
}

bool SqliteDb::update(const Record& record, std::string& error) {
    if (!db_) {
        error = "the SQLite database is not open";
        return false;
    }
    Statement s(db_, "UPDATE people SET name = ?2, card_id = ?3, dim = ?4, feature = ?5 WHERE id = ?1");
    if (!s.get() || !bind_record(s.get(), record) || sqlite3_step(s.get()) != SQLITE_DONE) {
        error = failure(db_, "cannot change '" + record.id + "'");
        return false;
    }
    if (sqlite3_changes(db_) != 1) {
        error = "unknown id in the database file: " + record.id;
        return false;
    }
    return true;
}

bool SqliteDb::replace_all(const std::vector<Record>& records, std::string& error) {
    if (!db_) {
        error = "the SQLite database is not open";
        return false;
    }
    if (!exec("BEGIN IMMEDIATE", error)) return false;
    bool ok = exec("DELETE FROM people", error);
    if (ok) {
        Statement s(db_, "INSERT INTO people (id, name, card_id, dim, feature) VALUES (?1, ?2, ?3, ?4, ?5)");
        ok = s.get() != nullptr;
        for (size_t i = 0; ok && i < records.size(); ++i) {
            sqlite3_reset(s.get());
            ok = bind_record(s.get(), records[i]) && sqlite3_step(s.get()) == SQLITE_DONE;
            if (!ok) error = failure(db_, "cannot insert '" + records[i].id + "'");
        }
        if (!s.get()) error = failure(db_, "cannot prepare the insert");
    }
    if (ok) ok = exec("COMMIT", error);
    if (!ok) {
        std::string ignored;
        exec("ROLLBACK", ignored);
    }
    return ok;
}

}  // namespace fa
