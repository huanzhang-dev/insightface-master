// sqlite_db.h - the SQLite form of the database of registered people.
//
// A thin wrapper around the SQLite C library (third_party/sqlite, public domain) that stores the
// same things as the CSV file: one table `people`, one row per registered person.
//
//   CREATE TABLE people (
//     id      TEXT NOT NULL PRIMARY KEY,   -- unique text identifier
//     name    TEXT NOT NULL,
//     card_id TEXT UNIQUE,                 -- RF card number; NULL = no card; unique among the people
//     dim     INTEGER NOT NULL CHECK (dim > 0),
//     feature BLOB NOT NULL CHECK (length(feature) = dim * 4)   -- dim 32-bit floats, little endian
//   );
//   PRAGMA user_version = 1;               -- the version of this layout
//
// The rows are read in the order they were inserted (rowid), which is the order of the people in
// FeatureStore::records() and so the position of their feature in the matcher's table. Unlike the
// CSV file, a change touches ONE row (SQLite writes it crash-safely through its own journal), so
// registering a person costs milliseconds even in a database of 10,000 people.
//
// FeatureStore is the only user of this class (it adds the lock file, the checks of the features
// and the undo of failed changes); the class itself does no checking of the features.
// File names are passed to SQLite as they are: use ASCII (or UTF-8) names.
#ifndef FA_SQLITE_DB_H
#define FA_SQLITE_DB_H

#include <string>
#include <vector>

#include "record.h"

struct sqlite3;  // from sqlite3.h

namespace fa {

class SqliteDb {
public:
    SqliteDb() {}
    ~SqliteDb() { close(); }
    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;

    // Open the database file. With `create` a missing file is created and given the table; without it
    // the file must exist and must hold this table (and no newer layout), otherwise false and `error`
    // says why (also for a file that is not a SQLite database at all).
    bool open(const std::string& path, bool create, std::string& error);
    void close();
    bool is_open() const { return db_ != nullptr; }

    // Read every row, in insertion order. A row whose feature blob does not fit its dim is an error.
    bool load_all(std::vector<Record>& records, std::string& error);
    // One row in, out or changed (each is one atomic SQLite transaction). erase and update need an
    // existing id (false otherwise); insert needs a new id and a new card.
    bool insert(const Record& record, std::string& error);
    bool erase(const std::string& id, std::string& error);
    bool update(const Record& record, std::string& error);  // the row with record.id gets the new name, card and feature
    // Make the table hold exactly `records` (one transaction: all or nothing). Used to write a new file.
    bool replace_all(const std::vector<Record>& records, std::string& error);

private:
    bool exec(const char* sql, std::string& error);

    sqlite3* db_ = nullptr;
};

// True if the file starts with the SQLite file header ("SQLite format 3").
bool is_sqlite_file(const std::string& path);

}  // namespace fa

#endif  // FA_SQLITE_DB_H
