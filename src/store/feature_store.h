// feature_store.h - the database of registered people: a file (CSV or SQLite) and its copy in memory.
//
// TWO FILE FORMATS, THE USER CHOOSES (DatabaseFormat)
//   CSV     the default. A text file, one line per person. No library needed, can be read and fixed in
//           a text editor and in a spreadsheet. Every change rewrites the whole file (see below).
//   SQLite  one table `people` in a SQLite file (sqlite_db.h). A change touches ONE row, so it costs
//           milliseconds however many people are registered; the features are stored as exact binary
//           numbers. Needs the SQLite library (third_party/sqlite, public domain, compiled into
//           the project).
// Both formats hold exactly the same, are checked by the same rules and behave the same through
// this class. Either way the whole database is also kept in memory, because the matcher compares a
// camera face with EVERY registered feature (one flat table in RAM, see matcher.h): the file is the
// safe place to keep it between runs. The choice is made when the database is opened:
//   open(path)                      the format follows the file: an existing file is recognised by its
//                                   content (SQLite files start with "SQLite format 3"), a new file by
//                                   its extension (.db .sqlite .sqlite3 = SQLite, anything else = CSV)
//   open(path, DatabaseFormat::Csv) or (path, DatabaseFormat::Sqlite): the user's explicit choice; an
//                                   existing file of the other kind is refused (never misread).
// tools/db_convert converts a database from one format to the other.
//
// CSV format: a header line, then one line per registered person:
//
//     id,name,card_id,dim,values
//     E001,Kim Min-su,0004A1B2,128,v0,v1,v2,...,v127
//
//   id       unique text identifier (e.g. an employee number)
//   name     display name (written in "quotes" when it contains a comma)
//   card_id  the person's RF card number, as text; empty when the person has no card; unique
//            among all people (the card is how a person is looked up before the face is compared,
//            see "CARD FIRST" below)
//   dim      how many numbers follow
//   v...     the feature numbers (9 significant digits, enough to restore a float exactly)
// Older files have no header line and no card_id column (`id,name,dim,v0,...`): they are still
// read (every card_id empty) and become the format above the next time they are written. The
// header is recognised by its third field being the text `card_id`.
// SQLite format: see sqlite_db.h (the same five columns; the feature is a BLOB of dim 32-bit floats).
//
// CARD FIRST (N is a subset of the database)
//   Usually the faces of a camera frame are compared with ALL N registered features (M:N matching,
//   matcher.h). When the person has been identified by an RF card, the card comes FIRST: the card
//   number is looked up in the database and the feature stored with it is retrieved; only those
//   features (N = the number of cards, which can be smaller than, equal to or larger than the
//   number M of detected faces) are compared with the faces. A card that is not in the database
//   is a failed comparison. find_by_card / index_of_card do the lookup, match_cards the whole
//   card-first comparison.
//
// MANAGED DATABASE (use open())
//   open(path) binds the store to its database file. From then on the management operations of
//   requirement 2.4 (add = register, remove = delete, update = modify, and set_card_id) are
//   WRITE-THROUGH: the change is made in memory, the file is written, and only if that worked does
//   the call return true. If the file cannot be written the change is UNDONE and the call returns
//   false, so the memory and the file never disagree. These guarantees hold:
//     * a crash or power loss while saving cannot damage the database: a CSV file is written
//       completely under another name and then swapped in with one operation (save()); SQLite does
//       the same with its own journal;
//     * a damaged file is refused when it is opened (CSV: with the line number; load rules below);
//     * only ONE program can have the database open: open() creates a lock (path + ".lock",
//       which Windows removes when the program ends, even when it crashes). A second program
//       that opens the same path gets false, so two programs cannot overwrite each other's
//       changes.
//   Cost of a change: with CSV one write of the whole file, which grows with the number of people
//   (docs/RESULTS.md has the measured times for 10,000 people); with SQLite one row, a few
//   milliseconds at any size.
//
//   Without open() the store is a plain in-memory list (add / remove / update change only the
//   memory); load() and save(path) then read and write a file of either format on request. The
//   tests and the speed measurements use it that way.
#ifndef FA_FEATURE_STORE_H
#define FA_FEATURE_STORE_H

#include <memory>
#include <string>
#include <vector>

#include "matcher.h"
#include "record.h"
#include "types.h"

namespace fa {

class SqliteDb;  // sqlite_db.h

// The file format of a database. Auto = decide from the file (see the top of this file).
enum class DatabaseFormat { Auto, Csv, Sqlite };

// The result of a card-first comparison (FeatureStore::match_cards): M faces against the N registered
// features that the N cards stand for. Both views of the M x N comparison are given.
struct CardMatch {
    struct Card {
        std::string card_id;          // as given
        bool found = false;           // the card is in the database (false = this card's comparison failed)
        int record = -1;              // position of its owner in FeatureStore::records()
        int best_query = -1;          // which of the M faces is most like the owner (-1: none could be compared)
        float best_score = kNoScore;  // that face's similarity to the owner
    };
    struct Query {
        int best_card = -1;           // index into `cards` of the card whose owner this face is most like (-1: none)
        float score = kNoScore;       // that similarity
    };
    std::vector<Card> cards;     // N entries, in the order of the card numbers given
    std::vector<Query> queries;  // M entries, in the order of the faces given
};

class FeatureStore {
public:
    FeatureStore();
    ~FeatureStore();  // releases the lock of an open database
    FeatureStore(const FeatureStore&) = delete;
    FeatureStore& operator=(const FeatureStore&) = delete;

    // ----- the database file -----
    // Open the database `path` (see "MANAGED DATABASE"): take the lock, read the file, bind the
    // store to it. `format` is the user's choice (see the top of this file); Auto follows the file.
    // A file that does not exist yet is an empty database; it is created at once (so that a folder
    // that cannot be written is noticed now, not at the first registration).
    // The old content of the store is replaced. Returns false and sets last_error() when the
    // path is empty, the file is damaged or is of another format than the one asked for, the file
    // cannot be created or another program has the database open; then the store is exactly as
    // before (still bound to its old database, if it had one). Opening the path the store is already
    // bound to reads the file again.
    bool open(const std::string& path, DatabaseFormat format = DatabaseFormat::Auto);
    // Forget the binding and release the lock. The records stay in memory; from now on changes
    // are no longer written to the file.
    void close();
    bool is_open() const { return !path_.empty(); }
    const std::string& path() const { return path_; }  // the bound database file; empty when not open
    // The format of the open database (Csv or Sqlite); Csv when the store is not open.
    DatabaseFormat format() const { return format_; }

    // ----- plain file functions (do not use the binding) -----
    // Read a database file of either format (Auto = recognised by content, then by extension). The old
    // content is replaced. Returns false and sets last_error() on a missing file or a damaged
    // line / row (the old content is kept). Refused while the store is open (the memory would no
    // longer match the database file): close() first.
    // A file is accepted only if every line has the right number of numbers, every feature
    // is a finite vector of length 1 (0.95 .. 1.05), all features have the same length and
    // every id and every card number is unique.
    bool load(const std::string& path, DatabaseFormat format = DatabaseFormat::Auto);
    // Write all records to `path` as a new file of the given format (Auto: by the extension, CSV unless it
    // is .db .sqlite .sqlite3). The data is written to a temporary file (path + ".tmp") first and
    // then swapped in with one operation, so a crash cannot leave a half-written file at `path`.
    // Ids, names and card numbers must not contain line breaks (add() refuses them).
    bool save(const std::string& path, DatabaseFormat format = DatabaseFormat::Auto) const;

    // ----- management (requirement 2.4) -----
    // When the store is open, each of these writes the database file before it returns true and
    // undoes its change when the write fails (last_error() then says so).
    bool add(const Record& record);                      // false if the id already exists
    bool remove(const std::string& id);                  // false if the id is unknown
    // Change the name and/or the feature of an existing id. Pass an empty name
    // or an empty feature to keep the old one.
    bool update(const std::string& id, const std::string& new_name, const Feature& new_feature);
    // Give the person `id` the RF card `card_id`, or take the card away (empty `card_id`). False for
    // an unknown id, for a card that another person owns, and for a card with a line break in it.
    bool set_card_id(const std::string& id, const std::string& card_id);

    // ----- reading -----
    int count() const { return static_cast<int>(records_.size()); }
    // Changes only after a successful content mutation or database reload.
    uint64_t revision() const { return revision_; }
    const std::vector<Record>& records() const { return records_; }
    const Record* find(const std::string& id) const;
    // The person who owns the RF card, or null (also null for an empty `card_id`: "no card" is not a card).
    const Record* find_by_card(const std::string& card_id) const;
    // The position of that person in records() (= the position in the matcher's table), or -1.
    int index_of_card(const std::string& card_id) const;

    // CARD FIRST. The cards are looked up in the database FIRST and the features stored with them are
    // retrieved; then the M faces (`queries`, the features of the currently detected faces) are compared with
    // exactly those N features (N = card_ids.size()). M and N are independent: fewer faces than cards,
    // as many, or more. See CardMatch for the result: for every card whether it was found and which
    // face is most like its owner, and for every face which card's owner it is most like.
    // Returns true only if card_ids is not empty and EVERY card is in the database. A card that is not
    // in the database is a failed comparison: it is marked found = false, nothing is compared for it,
    // and the call returns false (the other cards are still compared, so the caller can use their
    // results or reject the whole attempt). There is never a fallback to the whole database.
    // It is up to the caller to accept a face when the score reaches its threshold.
    bool match_cards(const std::vector<Feature>& queries, const std::vector<std::string>& card_ids, CardMatch& result) const;

    // The M:N matcher holding the N registered features (it compares M query features with all
    // of them in one call). Its table is rebuilt (only when something changed) the first time
    // it is requested afterwards.
    const FeatureMatcher& matcher() const;

    const std::string& last_error() const { return error_; }

private:
    // Read and check a CSV file into `out` (shared by load() and open()).
    bool read_csv(const std::string& path, std::vector<Record>& out) const;
    // Read and check a SQLite database file into `out`; `db` (when given) is left open on it.
    bool read_sqlite(const std::string& path, bool create, std::vector<Record>& out, std::unique_ptr<SqliteDb>* db) const;
    // Which format does `path` have under the user's choice `format` (Auto: content, then extension)?
    // False, with the reason in error_, when the file is of the other kind than asked for.
    bool resolve_format(const std::string& path, DatabaseFormat format, DatabaseFormat& resolved) const;
    // The kinds of change that persist() writes to the database file.
    enum class Change { Add, Remove, Update };
    // Write one change to the database file when the store is open (nothing to do otherwise): CSV
    // rewrites the file, SQLite changes the row of `record`.
    bool persist(Change what, const Record& record);

    std::vector<Record> records_;
    uint64_t revision_ = 0;
    std::string path_;            // the bound database file; empty = not open
    DatabaseFormat format_ = DatabaseFormat::Csv;  // its format while open
    std::unique_ptr<SqliteDb> sqlite_;  // the open SQLite connection (SQLite format only)
    void* lock_ = nullptr;        // the Windows handle of the lock file while open (null otherwise)
    mutable FeatureMatcher matcher_;
    mutable bool matcher_dirty_ = true;
    mutable std::string error_;
};

}  // namespace fa

#endif  // FA_FEATURE_STORE_H
