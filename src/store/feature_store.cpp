// feature_store.cpp - see feature_store.h.
#include "feature_store.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "profile.h"  // trim()
#include "sqlite_db.h"

namespace fa {
namespace {

bool parse_dimension(const std::string& text, int& dimension) {
    const std::string number = trim(text);
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(number.c_str(), &end, 10);
    if (end == number.c_str() || end != number.c_str() + number.size() || errno == ERANGE ||
        value <= 0 || value > INT_MAX) return false;
    dimension = static_cast<int>(value);
    return true;
}

bool parse_feature_value(const std::string& text, float& value) {
    const std::string number = trim(text);
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(number.c_str(), &end);
    // Use the same double-to-float conversion as the CSV writer's previous reader.
    if (end == number.c_str() || end != number.c_str() + number.size() || errno == ERANGE ||
        !std::isfinite(parsed)) return false;
    value = static_cast<float>(parsed);
    return std::isfinite(value) && (parsed == 0 || value != 0);
}

// A feature must have only finite numbers and length 1 (within a small tolerance). The
// matcher's score is a plain dot product, which is only a cosine similarity for length-1
// vectors: a record with a huge or NaN number would otherwise score high against every query.
bool is_usable_feature(const Feature& f) {
    double sum = 0;
    for (float v : f.values) {
        if (!std::isfinite(v)) return false;
        sum += static_cast<double>(v) * v;
    }
    return sum > 0.95 * 0.95 && sum < 1.05 * 1.05;
}

// Write a text field; wrap it in quotes when it contains a comma or a quote.
std::string csv_field(const std::string& text) {
    if (text.find_first_of(",\"\n\r") == std::string::npos) return text;
    std::string out = "\"";
    for (char c : text) {
        if (c == '"') out += "\"\"";  // a quote inside quotes is doubled
        else out += c;
    }
    return out + "\"";
}

// Split one CSV line into fields (handles "quoted, fields" and "" inside them).
bool split_csv_line(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string current;
    bool in_quotes = false;
    bool closed_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { current += '"'; ++i; }
                else { in_quotes = false; closed_quote = true; }
            } else {
                current += c;
            }
        } else if (c == '"') {
            if (!current.empty() || closed_quote) return false;  // quotes must enclose the entire field
            in_quotes = true;
        } else if (c == ',') {
            fields.push_back(current);
            current.clear();
            closed_quote = false;
        } else {
            if (closed_quote) return false;  // no characters may follow a closing quote
            current += c;
        }
    }
    if (in_quotes) return false;  // quote never closed
    fields.push_back(current);
    return true;
}

bool file_exists(const std::string& path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Is `path` named like a SQLite database (.db .sqlite .sqlite3, any letter case)? Only used for a file
// that does not exist yet; an existing file is recognised by its content.
bool has_sqlite_extension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || path.find_first_of("\\/", dot) != std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return ext == "db" || ext == "sqlite" || ext == "sqlite3";
}

// The rules a file read from a SQLite database must meet, besides what SQLite itself guarantees
// (unique ids and card numbers): the same as for a CSV line. `where` names the file in messages.
bool check_loaded_records(const std::vector<Record>& records, const std::string& where, std::string& error) {
    for (size_t i = 0; i < records.size(); ++i) {
        const Record& r = records[i];
        if (r.id.empty() || r.id.find_first_of("\r\n") != std::string::npos ||
            r.name.find_first_of("\r\n") != std::string::npos || r.card_id.find_first_of("\r\n") != std::string::npos) {
            error = where + ": row needs a nonempty id and text fields without line breaks";
            return false;
        }
        if (!is_usable_feature(r.feature)) {
            error = where + ": row '" + r.id + "': the feature is not a finite vector of length 1";
            return false;
        }
        if (r.feature.dim() != records[0].feature.dim()) {
            error = where + ": row '" + r.id + "': feature length " + std::to_string(r.feature.dim()) + " differs from the first row (" +
                    std::to_string(records[0].feature.dim()) + ")";
            return false;
        }
    }
    return true;
}

// The lock of an open database: a file `lock_path` that is opened so that nobody else can open
// it (sharing mode 0) and that Windows deletes when the last handle is closed, which also
// happens when the program crashes or is killed (so a lock can never be left behind). Returns
// the handle, or null when another program holds the lock (or the file cannot be created).
void* take_lock(const std::string& lock_path) {
    HANDLE h = CreateFileA(lock_path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    return h == INVALID_HANDLE_VALUE ? nullptr : h;
}

void release_lock(void* lock) {
    if (lock) CloseHandle(static_cast<HANDLE>(lock));
}

// Write `records` as a CSV file at `path`: first completely into path + ".tmp", then swapped in.
// On failure `error` says what went wrong and `path` is untouched.
bool write_csv_file(const std::string& path, const std::vector<Record>& records, std::string& error) {
    const std::string temp = path + ".tmp";
    {
        std::ofstream file(temp, std::ios::trunc);
        if (!file) {
            error = "cannot write " + temp;
            return false;
        }
        char number[32];
        file << "id,name,card_id,dim,values\n";  // the header line (its third field marks the format, see read_csv)
        for (const Record& r : records) {
            file << csv_field(r.id) << ',' << csv_field(r.name) << ',' << csv_field(r.card_id) << ',' << r.feature.dim();
            for (float v : r.feature.values) {
                std::snprintf(number, sizeof(number), "%.9g", v);
                file << ',' << number;
            }
            file << '\n';
        }
        file.flush();
        if (!file) {
            error = "write error in " + temp;
            file.close();
            std::remove(temp.c_str());
            return false;
        }
    }
    // Replace the old file only after the new one is completely written. MoveFileEx with
    // MOVEFILE_REPLACE_EXISTING swaps the files in one step, so at every moment the path
    // holds either the complete old file or the complete new one (std::rename cannot do
    // that on Windows: it refuses to overwrite, which would force us to delete first).
    if (!MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "cannot replace " + path + " with " + temp;
        std::remove(temp.c_str());  // do not leave the finished copy lying around
        return false;
    }
    return true;
}

}  // namespace

FeatureStore::FeatureStore() {}

FeatureStore::~FeatureStore() { close(); }

bool FeatureStore::read_csv(const std::string& path, std::vector<Record>& loaded) const {
    std::ifstream file(path);
    if (!file) {
        error_ = "cannot open " + path;
        return false;
    }
    loaded.clear();
    std::string line;
    int line_number = 0;
    std::vector<std::string> fields;
    bool with_cards = false;      // the format with the card_id column (it has a header line); older files lack it
    bool first_line = true;
    std::unordered_set<std::string> ids, cards;  // to find duplicates quickly (10,000 lines)
    while (std::getline(file, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF record ending, not field content
        if (line_number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // UTF-8 byte order mark
        if (trim(line).empty()) continue;
        if (!split_csv_line(line, fields) || fields.size() < 3) {
            error_ = path + ":" + std::to_string(line_number) + ": malformed line";
            return false;
        }
        if (first_line) {
            first_line = false;
            if (fields[2] == "card_id") {  // the header line: "id,name,card_id,dim,values"
                const std::vector<std::string> header = {"id", "name", "card_id", "dim", "values"};
                if (fields != header) {
                    error_ = path + ":" + std::to_string(line_number) + ": invalid CSV header";
                    return false;
                }
                with_cards = true;
                continue;
            }
        }
        // Fields of a line: id, name, [card_id,] dim, then `dim` numbers.
        const size_t dim_field = with_cards ? 3 : 2;
        if (fields.size() <= dim_field) {
            error_ = path + ":" + std::to_string(line_number) + ": malformed line";
            return false;
        }
        int dim = 0;
        if (!parse_dimension(fields[dim_field], dim) || fields.size() != dim_field + 1 + static_cast<size_t>(dim)) {
            error_ = path + ":" + std::to_string(line_number) + ": expected " + std::to_string(dim) + " numbers";
            return false;
        }
        Record r;
        r.id = fields[0];
        r.name = fields[1];
        if (with_cards) r.card_id = fields[2];
        if (r.id.empty() || r.id.find_first_of("\r\n") != std::string::npos ||
            r.name.find_first_of("\r\n") != std::string::npos || r.card_id.find_first_of("\r\n") != std::string::npos) {
            error_ = path + ":" + std::to_string(line_number) + ": empty id or line break in a text field";
            return false;
        }
        r.feature.values.resize(dim);
        for (int d = 0; d < dim; ++d) {
            if (!parse_feature_value(fields[dim_field + 1 + d], r.feature.values[d])) {
                error_ = path + ":" + std::to_string(line_number) + ": invalid feature number in column " +
                         std::to_string(dim_field + 2 + d);
                return false;
            }
        }
        if (!is_usable_feature(r.feature)) {
            error_ = path + ":" + std::to_string(line_number) + ": the feature is not a finite vector of length 1";
            return false;
        }
        if (!loaded.empty() && loaded[0].feature.dim() != dim) {  // all features must have the same length
            error_ = path + ":" + std::to_string(line_number) + ": feature length " + std::to_string(dim) +
                     " differs from the first line (" + std::to_string(loaded[0].feature.dim()) + ")";
            return false;
        }
        if (!ids.insert(r.id).second) {
            error_ = path + ":" + std::to_string(line_number) + ": duplicate id " + r.id;
            return false;
        }
        if (!r.card_id.empty() && !cards.insert(r.card_id).second) {  // an empty card_id means "no card" and may repeat
            error_ = path + ":" + std::to_string(line_number) + ": duplicate card_id " + r.card_id;
            return false;
        }
        loaded.push_back(std::move(r));
    }
    return true;
}

bool FeatureStore::read_sqlite(const std::string& path, bool create, std::vector<Record>& loaded, std::unique_ptr<SqliteDb>* keep) const {
    std::unique_ptr<SqliteDb> db(new SqliteDb());
    if (!db->open(path, create, error_) || !db->load_all(loaded, error_) || !check_loaded_records(loaded, path, error_)) return false;
    if (keep) *keep = std::move(db);
    return true;
}

bool FeatureStore::resolve_format(const std::string& path, DatabaseFormat format, DatabaseFormat& resolved) const {
    const bool exists = file_exists(path);
    // what the file IS: by content when it exists and has content, else by its name (a new or empty file)
    bool is_sqlite = has_sqlite_extension(path);
    bool known_by_content = false;
    if (exists) {
        std::ifstream probe(path, std::ios::binary | std::ios::ate);
        if (probe && probe.tellg() > 0) {
            is_sqlite = is_sqlite_file(path);
            known_by_content = true;
        }
    }
    if (format == DatabaseFormat::Auto) {
        resolved = is_sqlite ? DatabaseFormat::Sqlite : DatabaseFormat::Csv;
        return true;
    }
    if (known_by_content && (format == DatabaseFormat::Sqlite) != is_sqlite) {
        error_ = path + " is a " + (is_sqlite ? "SQLite" : "CSV") + " file, not a " + (is_sqlite ? "CSV" : "SQLite") +
                 " database as asked (use tools/db_convert to convert it)";
        return false;
    }
    resolved = format;
    return true;
}

bool FeatureStore::load(const std::string& path, DatabaseFormat format) {
    if (is_open()) {
        error_ = "the database " + path_ + " is open: close() it before load()";
        return false;
    }
    if (!file_exists(path)) {
        error_ = "cannot open " + path;
        return false;
    }
    DatabaseFormat resolved;
    if (!resolve_format(path, format, resolved)) return false;
    std::vector<Record> loaded;
    if (!(resolved == DatabaseFormat::Sqlite ? read_sqlite(path, false, loaded, nullptr) : read_csv(path, loaded))) return false;
    records_.swap(loaded);
    ++revision_;
    matcher_dirty_ = true;
    return true;
}

bool FeatureStore::save(const std::string& path, DatabaseFormat format) const {
    DatabaseFormat resolved = format;
    if (format == DatabaseFormat::Auto) resolved = has_sqlite_extension(path) ? DatabaseFormat::Sqlite : DatabaseFormat::Csv;
    if (resolved == DatabaseFormat::Csv) return write_csv_file(path, records_, error_);

    // SQLite: build a complete new file next to the target, then swap it in (like the CSV file).
    const std::string temp = path + ".tmp";
    std::remove(temp.c_str());
    {
        SqliteDb db;
        if (!db.open(temp, true, error_) || !db.replace_all(records_, error_)) {
            db.close();
            std::remove(temp.c_str());
            return false;
        }
    }  // closed here: SQLite has written everything and removed its journal
    if (!MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error_ = "cannot replace " + path + " with " + temp;
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

bool FeatureStore::open(const std::string& path, DatabaseFormat format) {
    if (path.empty()) {
        error_ = "no database path given";
        return false;
    }
    DatabaseFormat resolved;
    if (!resolve_format(path, format, resolved)) return false;
    // Opening the path we are bound to again keeps our own lock; a new path needs a new lock.
    const bool same_path = (path == path_);
    void* new_lock = nullptr;
    if (!same_path) {
        new_lock = take_lock(path + ".lock");
        if (!new_lock) {
            error_ = "cannot open the database " + path + ": another program has it open (or " + path + ".lock cannot be created)";
            return false;
        }
    }
    std::vector<Record> loaded;
    std::unique_ptr<SqliteDb> db;
    bool ok;
    if (resolved == DatabaseFormat::Sqlite) {
        // A missing file is created, empty. So is a file of zero bytes (SQLite itself leaves one behind when a
        // database is opened and nothing is written): it holds no table yet, so it is an empty database.
        bool empty_file = !file_exists(path);
        if (!empty_file) {
            std::ifstream probe(path, std::ios::binary | std::ios::ate);
            empty_file = probe && probe.tellg() == std::streampos(0);
        }
        ok = read_sqlite(path, empty_file, loaded, &db);
    } else {
        ok = file_exists(path) ? read_csv(path, loaded) : write_csv_file(path, loaded, error_);  // missing: create it, empty
    }
    if (!ok) {
        release_lock(new_lock);
        return false;
    }
    if (!same_path) {
        release_lock(lock_);
        lock_ = new_lock;
        path_ = path;
    }
    sqlite_ = std::move(db);  // (null for CSV; the old connection, if any, is closed)
    format_ = resolved;
    records_.swap(loaded);
    ++revision_;
    matcher_dirty_ = true;
    return true;
}

void FeatureStore::close() {
    sqlite_.reset();
    release_lock(lock_);
    lock_ = nullptr;
    path_.clear();
    format_ = DatabaseFormat::Csv;
}

bool FeatureStore::persist(Change what, const Record& record) {
    if (path_.empty()) return true;  // not open: memory only
    bool ok;
    if (format_ == DatabaseFormat::Sqlite) {  // one row
        ok = what == Change::Add ? sqlite_->insert(record, error_) : what == Change::Remove ? sqlite_->erase(record.id, error_)
                                                                                           : sqlite_->update(record, error_);
    } else {  // CSV: the whole file (records_ already holds the change)
        ok = write_csv_file(path_, records_, error_);
    }
    if (!ok) error_ = "the change was not saved and has been undone: " + error_;
    return ok;
}

const Record* FeatureStore::find(const std::string& id) const {
    for (const Record& r : records_)
        if (r.id == id) return &r;
    return nullptr;
}

bool FeatureStore::add(const Record& record) {
    if (record.id.empty() || record.feature.dim() == 0) {
        error_ = "record needs an id and a feature";
        return false;
    }
    if (!is_usable_feature(record.feature)) {
        error_ = "the feature must be a finite vector of length 1";
        return false;
    }
    // The CSV has one line per person and is read line by line, so a line break inside a
    // text field could not be read back.
    if (record.id.find_first_of("\r\n") != std::string::npos || record.name.find_first_of("\r\n") != std::string::npos ||
        record.card_id.find_first_of("\r\n") != std::string::npos) {
        error_ = "id, name and card_id must not contain line breaks";
        return false;
    }
    if (find(record.id)) {
        error_ = "id already registered: " + record.id;
        return false;
    }
    if (!record.card_id.empty() && find_by_card(record.card_id)) {
        error_ = "card already belongs to " + find_by_card(record.card_id)->id + ": " + record.card_id;
        return false;
    }
    if (!records_.empty() && records_[0].feature.dim() != record.feature.dim()) {
        error_ = "feature length differs from the registered ones";
        return false;
    }
    records_.push_back(record);
    matcher_dirty_ = true;
    if (!persist(Change::Add, record)) {  // the file could not be written: undo
        records_.pop_back();
        return false;
    }
    ++revision_;
    return true;
}

bool FeatureStore::remove(const std::string& id) {
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].id == id) {
            const Record removed = records_[i];
            records_.erase(records_.begin() + i);
            matcher_dirty_ = true;
            if (!persist(Change::Remove, removed)) {  // the file could not be written: put the person back where he was
                records_.insert(records_.begin() + i, removed);
                return false;
            }
            ++revision_;
            return true;
        }
    }
    error_ = "unknown id: " + id;
    return false;
}

bool FeatureStore::update(const std::string& id, const std::string& new_name, const Feature& new_feature) {
    if (new_name.find_first_of("\r\n") != std::string::npos) {  // same rule as add(): the CSV is read line by line
        error_ = "id and name must not contain line breaks";
        return false;
    }
    if (new_feature.dim() > 0 && !is_usable_feature(new_feature)) {
        error_ = "the feature must be a finite vector of length 1";
        return false;
    }
    for (Record& r : records_) {
        if (r.id != id) continue;
        if (new_feature.dim() > 0 && new_feature.dim() != r.feature.dim()) {
            error_ = "feature length differs from the registered one";
            return false;
        }
        const Record before = r;
        if (!new_name.empty()) r.name = new_name;
        if (new_feature.dim() > 0) {
            r.feature = new_feature;
            matcher_dirty_ = true;  // (a new name alone does not change the matcher's table)
        }
        if (!persist(Change::Update, r)) {  // the file could not be written: restore the old name and feature
            r = before;  // (matcher_dirty_ stays true if the feature had been replaced: the table is rebuilt from the restored one)
            return false;
        }
        ++revision_;
        return true;
    }
    error_ = "unknown id: " + id;
    return false;
}

bool FeatureStore::set_card_id(const std::string& id, const std::string& card_id) {
    if (card_id.find_first_of("\r\n") != std::string::npos) {  // same rule as add(): the CSV is read line by line
        error_ = "id, name and card_id must not contain line breaks";
        return false;
    }
    for (Record& r : records_) {
        if (r.id != id) continue;
        const Record* owner = card_id.empty() ? nullptr : find_by_card(card_id);
        if (owner && owner != &r) {
            error_ = "card already belongs to " + owner->id + ": " + card_id;
            return false;
        }
        const std::string before = r.card_id;
        r.card_id = card_id;
        if (!persist(Change::Update, r)) {  // the file could not be written: restore the old card
            r.card_id = before;
            return false;
        }
        ++revision_;
        return true;
    }
    error_ = "unknown id: " + id;
    return false;
}

const Record* FeatureStore::find_by_card(const std::string& card_id) const {
    if (card_id.empty()) return nullptr;  // "no card" is not a card that somebody owns
    for (const Record& r : records_)
        if (r.card_id == card_id) return &r;
    return nullptr;
}

int FeatureStore::index_of_card(const std::string& card_id) const {
    const Record* r = find_by_card(card_id);
    return r ? static_cast<int>(r - records_.data()) : -1;
}

bool FeatureStore::match_cards(const std::vector<Feature>& queries, const std::vector<std::string>& card_ids, CardMatch& result) const {
    result = CardMatch();
    result.queries.assign(queries.size(), CardMatch::Query());

    // 1. The cards come FIRST: look every card up in the database and retrieve the record it belongs to.
    std::vector<int> positions;       // the registered features that the found cards stand for (N_s of them)
    std::vector<size_t> card_of;      // card_of[k] = index into result.cards of positions[k]
    for (size_t c = 0; c < card_ids.size(); ++c) {
        CardMatch::Card card;
        card.card_id = card_ids[c];
        card.record = index_of_card(card_ids[c]);
        card.found = card.record >= 0;  // not found: a failed comparison, nothing is compared for this card
        if (card.found) {
            positions.push_back(card.record);
            card_of.push_back(c);
        }
        result.cards.push_back(card);
    }
    const bool all_found = !card_ids.empty() && positions.size() == card_ids.size();
    if (!all_found && card_ids.empty()) error_ = "no card given";
    else if (!all_found) error_ = "a card is not in the database";
    if (positions.empty() || queries.empty()) return all_found;

    // 2. Compare the M faces with exactly those features: all M x N_s similarities in one call.
    std::vector<float> scores;
    matcher().score_matrix(queries, positions, scores);
    const size_t n_s = positions.size();
    for (size_t q = 0; q < queries.size(); ++q) {
        for (size_t k = 0; k < n_s; ++k) {
            const float s = scores[q * n_s + k];
            if (s <= kNoScore) continue;  // this face could not be compared
            CardMatch::Query& face = result.queries[q];
            if (s > face.score) {
                face.score = s;
                face.best_card = static_cast<int>(card_of[k]);
            }
            CardMatch::Card& card = result.cards[card_of[k]];
            if (s > card.best_score) {
                card.best_score = s;
                card.best_query = static_cast<int>(q);
            }
        }
    }
    return all_found;
}

const FeatureMatcher& FeatureStore::matcher() const {
    if (matcher_dirty_) {
        std::vector<const Feature*> features;
        features.reserve(records_.size());
        for (const Record& r : records_) features.push_back(&r.feature);
        // set_gallery only fails for features of different lengths, which add() and load()
        // already refuse; on failure the matcher is left empty (matches nothing).
        matcher_.set_gallery(features);
        matcher_dirty_ = false;
    }
    return matcher_;
}

}  // namespace fa
