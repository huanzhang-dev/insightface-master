# Feature store (`src/store/`)

## What this is for

The feature store is the **database of registered people**: for each person an id, a display name, a feature (the 128 to 512 numbers made by the extraction engine, see `feature.md`) and, if the person has one, the number of the person's **RF card** (`card_id`). The database is a file that the store **manages**, in one of **two formats that the user chooses**: **CSV** (the default: plain text, a header line and then one line per person) or **SQLite** (one table in a SQLite database file). A copy of the whole database lives in memory while the program runs. The store does six jobs.

1. **The managed database file.** `open(path, format)` binds the store to a database file: it takes a lock so that no second program can open the same database, reads the file (a file that does not exist yet is created at once, empty) and from then on keeps file and memory in step. Every change is **written through**: `add`, `remove`, `update` and `set_card_id` change the list in memory, write the change to the file (CSV: the whole file is rewritten; SQLite: one row is inserted, deleted or changed), and return `true` only if the write worked. If it did not work, the change is undone and the call returns `false`, so the memory and the file never disagree. A crash or a power loss while saving cannot damage the file (CSV: it is written under another name and swapped in; SQLite: its own journal does the same job), and a damaged file is refused when it is opened. `close()` (or the destructor) releases the lock.
2. **The three management operations** (requirement 2.4): **register** (`add`), **delete** (`remove`) and **modify** (`update`), refusing ids and names with line breaks and features that are not finite vectors of length 1.
3. **Plain file functions.** Without `open` the store is a plain in-memory list (`add`, `remove` and `update` then change only the memory); `load(path, format)` and `save(path, format)` read and write a file of either format on request. The tests and the speed measurements use the store that way, and so does `tools/db_convert`, which converts a database from one format to the other.
4. **The matcher.** `matcher()` returns a `FeatureMatcher` that already holds all N registered features (see `match.md`), and it rebuilds that matcher only when something has changed.
5. **Card first: N as a subset.** Usually a camera face is compared with all N registered people. When RF cards have been read, the card information is identified FIRST: the card number is looked up in the database, the feature stored with it is retrieved, and only then are the faces of the current frame compared with the retrieved features (`find_by_card` and `index_of_card` do the look-up, `match_cards` the whole comparison, `set_card_id` gives a person a card or takes it away). A card that is not in the database is a failed comparison. See "Background: card first".
6. **The choice of the format.** `DatabaseFormat` (`Auto`, `Csv`, `Sqlite`) is a parameter of `open`, `load` and `save`; the program that uses the store passes the user's choice on (`AuthPipeline::init` has a `db_format` parameter). See "Background: choosing the format".

In the pipeline the store is owned by `AuthPipeline` (`store_`): `init` opens the database, `process` uses it for matching, and `enroll_track`, `enroll_image`, `remove_person` and `rename_or_refresh` change it. They do not save by themselves, because every change is already in the file when the store call returns. Nothing in the store talks to a camera or a network; it can be used and tested alone (`tests/test_match.cpp`). The store builds on Windows only (it uses `<windows.h>` for the lock and for replacing the file). The SQLite format needs the SQLite library, which is part of the project (`third_party/sqlite`, see "Background: the SQLite format" and `MODELS.md`).

### Background: two formats, and why CSV is the default

The database can be a CSV file or a SQLite database, and **the user chooses**. **CSV is the default**, for these reasons:

* **The whole database has to be in memory anyway.** The matcher compares a camera face with every registered feature, so all N features are kept in one flat table in RAM (see `match.md`). The file is only a safe place to keep that table between runs; nothing is looked up in it. What a database engine is best at, finding a few rows without reading the rest, is not needed.
* **A CSV file needs no library**, and it was the original requirement (requirement 2.3 in `RESULTS.md`: registered features in a CSV file).
* **A CSV file can be read and edited by a person**: in a text editor or a spreadsheet, a person can be looked up, corrected or removed by hand. The loader refuses a damaged file and says the line number.

**What SQLite adds** is the thing CSV cannot do: **a change touches one row**, not the whole file, so registering, deleting or modifying a person costs milliseconds however many people are registered. It also stores the features as exact binary numbers, enforces by itself that an id and a card number belong to one person only, and is a real database file that other tools (the `sqlite3` shell, database browsers) can open. **What it costs:** one more library (the SQLite source, public domain, compiled into the project) and a binary file that cannot be edited in a text editor.

Measured on the development PC by `tests/test_match.cpp` (`test_database_speed`, one line per format; 10,000 people with 512 numbers each, the same people in both files; the test checks only that a SQLite change is faster than a CSV change; the numbers are those of the run of 2026-11-12):

| | file size | write the whole file | open (read all) | one change written (register / rename / give a card / delete) |
|---|---|---|---|---|
| CSV | 66.5 MB | 1306 ms | 1023 ms | 1271.6 / 1284.8 / 1290.6 / 1269.2 ms |
| SQLite | 39.4 MB | 97 ms | 33 ms | 3.3 / 3.0 / 3.0 / 3.5 ms |

So with CSV every change rewrites the whole file and costs about 1.3 s at 10,000 people of 512 numbers (a registration, a deletion and a modification alike), and proportionally less for fewer people: about 26 ms for 200 people (an **extrapolation** by proportion, 1290 ms x 200 / 10,000, not a measurement). With SQLite a change costs 3 to 3.5 ms at 10,000 people, about 360 to 430 times less; opening the database is 31 times faster and writing a whole new file 13 times. The SQLite file is smaller than the CSV file (4 bytes per number instead of about 13 characters) but **not as small as the numbers alone** (10,000 x 512 x 4 bytes = 20,480,000 bytes): a feature of 512 numbers is a 2 KB BLOB, and with SQLite's 4 KB pages one such row fills a page almost alone, so the file is about 4 KB per person (39.4 MB for 10,000). The page size was not changed. A call that changes the database returns only after the file is written, so a program that registers a person on the thread that also calls `AuthPipeline::process` waits that long for the registration (about 1.3 s with CSV at 10,000 people, about 3 ms with SQLite). The times are those of the development PC, not of the 4th-generation i5 target.

### Background: choosing the format

`DatabaseFormat` has three values. The choice is made when the database is opened (`open`), read (`load`) or written (`save`):

| Value | `open` and `load` | `save` |
|---|---|---|
| `Auto` (the default of the parameter) | An **existing file with content** is recognised by its **content**: a SQLite file starts with the 16 bytes `SQLite format 3` and a zero. Anything else is CSV. A file that does not exist yet (or is empty) is recognised by its **extension**: `.db`, `.sqlite` and `.sqlite3`, in any letter case, mean SQLite; anything else (`.csv`, `.txt`, no extension) means CSV, the default. | By the extension, as above (the existing content of the target is not looked at). |
| `Csv` | The file is read (or created) as CSV. If the existing file is a SQLite file, the call is **refused** (`<path> is a SQLite file, not a CSV database as asked (use tools/db_convert to convert it)`): a file is never misread. | CSV is written, whatever the name. |
| `Sqlite` | The file is read (or created) as a SQLite database. If the existing file is a CSV file, the call is refused (`<path> is a CSV file, not a SQLite database as asked ...`). | A SQLite file is written, whatever the name. |

A file's content wins over its name: a CSV file called `people.db` is opened as CSV by `Auto` (the test makes such a file). Only an existing file with at least one byte is judged by its content; an empty file is judged by its name, so `Csv` or `Sqlite` is accepted for it as asked. An empty file with a SQLite name (`.db`, `.sqlite`, `.sqlite3`) is an empty SQLite database: `open` gives it its table (SQLite itself leaves a zero-byte file behind when a database is opened and nothing is written; `test_format_choice` tests this).

Where the user makes the choice: the `format` parameter of `open`, `load` and `save`; the `db_format` parameter of `AuthPipeline::init`; the extension of the file name for `tools/gen_features` and for the output of `tools/db_convert` (which also has `--format csv|sqlite`); `tests/test_pipeline` has the option `--sqlite`. To change the format of an existing database, convert it with `tools/db_convert` (see `tools.md`); it reads either format, writes the other, reads the result back and compares.

Everything else is the same for both formats: the same rules for what a database may hold (every feature a finite vector of length 1, all features of the same length, every id and every non-empty card number unique, no line breaks in ids, names and card numbers), the same lock file `<path>.lock`, the same write-through with undo, the same `last_error()` texts for the store's own refusals, and the same order of the people (the order in which they were registered).

### Background: the managed database

`open(path, format)` binds the store to one database file. From then on:

| Guarantee | How it is kept |
|---|---|
| Memory and file never disagree | `add`, `remove`, `update` and `set_card_id` are **write-through**: the change is made in memory, then written to the file (`persist`), and only if that worked does the call return `true`. If the file cannot be written, the change is **undone** in memory (a registered person is taken out again, a deleted person is put back at the same position in the list, a changed name, card and feature are restored) and the call returns `false`; `last_error()` then starts with `the change was not saved and has been undone:` and gives the reason. A change that the store refuses for its own reasons (unknown id, duplicate id, a card that belongs to somebody else, a bad feature) does not touch the file at all. |
| A crash cannot damage the file | **CSV:** the new content is written completely to `<csv>.tmp` and then swapped in with one operation (`MoveFileExA`, see `write_csv_file`); at every moment the path holds either the complete old file or the complete new one. **SQLite:** every change is one SQLite transaction (one `INSERT`, `DELETE` or `UPDATE`); SQLite writes it through its rollback journal (`<db>-journal`, which exists only during the write), so after a crash the file holds either the old or the new state. |
| A damaged file is refused | `open` reads the whole file when it opens it. **CSV:** `read_csv` checks every line and names the line number of the first problem. **SQLite:** the file must be a SQLite database with the table `people` of the layout this program knows (a foreign file, a file of a newer layout and a row whose feature is not a finite vector of length 1 are refused) and every row is checked like a CSV line (`read_sqlite`). The store is then left as it was. |
| Only one program at a time | `open` takes a lock: the file `<path>.lock`, opened with sharing mode 0 (nobody else can open it) and with `FILE_FLAG_DELETE_ON_CLOSE`, so Windows deletes it when the handle is closed. That also happens when the program crashes or is killed, so a lock can never be left behind. A second program that calls `open` on the same path gets `false` (`another program has it open`), so two programs cannot overwrite each other's changes. The lock is the same for both formats. |
| A missing file is created at once | `open` creates an empty database file when there is none (CSV: only the header line; SQLite: an empty `people` table), so that a folder that cannot be written is noticed when the program starts, not at the first registration. |

Two limits to know. First, the lock only keeps out other programs that use `FeatureStore::open`; a text editor can still open the CSV file, other SQLite tools can still open the SQLite file, and `load()` of another store is not stopped by the lock either (the tests use that to look at the file while a store has it open). Second, a change made to the file by hand (CSV) while a program has the database open is lost at that program's next change, because every CSV change rewrites the whole file from memory: edit the file only while no program has it open. (A SQLite change touches only its own row, but the program's memory still does not see what another tool changed in the file until the database is opened again.)

Without `open` the store is not bound to a file: `add`, `remove` and `update` change only the memory, and `load(path, format)` / `save(path, format)` read and write a file on request. `load` is refused while the store is open, because the memory would then no longer match the database file.

### Background: the CSV file format

The default format. A **header line**, then one line per registered person:

```
id,name,card_id,dim,values
id,name,card_id,dim,v0,v1,v2,...,v(dim-1)
```

The first line is the header, written exactly as `id,name,card_id,dim,values`; the second line only shows the layout of every person's line.

| Field | Meaning |
|---|---|
| `id` | Unique text identifier (for example an employee number). |
| `name` | Display name. Written inside double quotes when it contains a comma (or a quote or a line break, see the quoting rules). |
| `card_id` | The person's RF card number, as text. **Empty** when the person has no card. Unique among all people (the card is how a person is looked up before the face is compared, see "Background: card first"); any number of people may have an empty one. |
| `dim` | How many numbers follow. |
| `v0 ... v(dim-1)` | The feature numbers, each written with 9 significant digits (the C format `%.9g`), which is enough to restore the 32-bit float exactly. |

An example with three people (the feature length is 4 only to keep the lines short; real files have 128, 256 or 512 numbers per line):

```
id,name,card_id,dim,values
E001,"Kim, Min-su",04A1B2C3,4,0.5,-0.5,0.5,-0.5
E002,"Lee ""JJ"" Park",,4,0.600000024,0.800000012,0,0
E003,Alice Kim,,4,0.5,0.5,0.5,0.5
```

- Line 1: the header line. The reader recognises it by its **third field being the text `card_id`**, and does not take it for a person.
- Line 2: the name contains a comma, so it is wrapped in quotes; otherwise the comma would split the name in two fields. This person has the card `04A1B2C3`.
- Line 3: the name contains quote characters, so it is wrapped in quotes and every quote inside is written twice (`""`). `0.6` is written as `0.600000024` because 0.6 cannot be stored exactly in a 32-bit float; nine digits are enough to read back the very same float. Zero is written as `0`. The card field is empty (two commas in a row): no card.
- Line 4: a plain name needs no quotes.
- Very small or very large numbers are written in exponent form such as `1.23e-05` (that is how `%g` prints them); the reader understands this form too.

**Older files.** Before the RF card was added the format had no header line and no card column (`id,name,dim,v0,...`). Such a file is still read: the first line is not a header (its third field is a number, not `card_id`), so the reader takes the file as the older format and every card is empty. `open` does not rewrite the file; the next change (or `save`) writes it in the new format. (A file in the new format whose header line was deleted by hand is taken for the older format and is refused in practice, with `expected <n> numbers`, because the card text is not a count of numbers.) The file `data\synthetic\features_10000_d512.csv` of the project was written by an earlier version and is in the older format.

Two rules apply when a file is read (`read_csv`, used by `open` and `load`):
- Every id must be unique, and so must every non-empty `card_id` (error `duplicate id <id>` or `duplicate card_id <card>` with the line number). Empty card numbers may repeat. The check uses a hash set (`std::unordered_set`), so reading a file takes time proportional to its length.
- The feature on every line must pass `is_usable_feature`: every number finite (no NaN, no infinity) and the length of the vector within 0.95 to 1.05 (about 1). Why: the matcher's score is a plain dot product (see `match.md`), which is a cosine similarity only for length-1 vectors; a record with a huge or NaN number would otherwise score high against every query. The same check is applied in `add` and `update`.
- A UTF-8 byte order mark (the three bytes EF BB BF that some editors put at the very start of a text file) at the start of the first line is skipped.

**Quoting rules.**
- Writing: only the three text fields (`id`, `name` and `card_id`) can be quoted. A text is wrapped in `"..."` if it contains a comma, a double quote, a line feed or a carriage return, and every `"` inside it is doubled. The `dim` field and the numbers are never quoted. A line feed or carriage return in an id, a name or a card number cannot be read back (the file is read line by line), so `add` refuses such ids, names and card numbers, `update` refuses such names and `set_card_id` refuses such card numbers (see there); the quoting of those two characters is only a safety net.
- Reading: a comma outside quotes ends a field; a `"` starts or ends a quoted part; inside a quoted part, `""` means one literal quote; a quote that is never closed makes the line malformed. Fields are not trimmed (spaces are kept).

### Background: the SQLite format

The other format. A SQLite database file with one table, `people`, one row per registered person (the same five things as a CSV line). The code is in `sqlite_db.h` / `sqlite_db.cpp` (class `SqliteDb`, below); `FeatureStore` adds the lock file, the checks of the features and the undo of failed changes. The table:

```sql
CREATE TABLE people (
  id      TEXT NOT NULL PRIMARY KEY,   -- unique text identifier
  name    TEXT NOT NULL,
  card_id TEXT UNIQUE,                 -- RF card number; NULL = no card; unique among the people
  dim     INTEGER NOT NULL CHECK (dim > 0),
  feature BLOB NOT NULL CHECK (length(feature) = dim * 4)   -- dim 32-bit floats, little endian
);
PRAGMA user_version = 1;               -- the version of this layout
```

* **No card is NULL, not an empty text.** `UNIQUE` lets any number of rows have `NULL` and allows a real card number only once, which is the rule of the CSV format too (many empty cards, every other card once). `SqliteDb` writes an empty `card_id` as `NULL` and reads `NULL` as an empty text.
* **The feature is the exact binary float numbers** (`dim` of them, 4 bytes each, little endian, as an x86 PC holds them). Nothing is rounded: a feature written and read back is bit for bit the same (the CSV format reaches the same by writing 9 significant digits).
* **`PRAGMA user_version = 1`** is the version of the layout. A file with a higher number is refused (`has table layout N, newer than this program understands (1)`); a SQLite file that has no table `people` of version 1 is refused too (`is not a face_auth database`), so a SQLite file of another application is never changed.
* **The rows are read in the order they were inserted** (`ORDER BY rowid`), which is the order of the people in `FeatureStore::records()` and so the position of their feature in the matcher's table. New people are added at the end in memory and in the file; a deletion keeps the order of the rest.
* **Every change is one statement and one row:** `INSERT INTO people ...` (register), `DELETE FROM people WHERE id = ?` (delete), `UPDATE people SET name, card_id, dim, feature WHERE id = ?` (modify, and give or take away a card). Each is its own transaction, written crash-safely by SQLite itself. A change waits up to 500 ms for another connection that holds the write lock, then fails (and the store undoes the change in memory).
* **Writing a whole new file** (`save`, `tools/db_convert`): `FeatureStore::save` creates a complete new database in `<path>.tmp` (one transaction that fills the table, `SqliteDb::replace_all`) and swaps it in with `MoveFileExA`, like the CSV file.
* **File names** are passed to SQLite as they are: use ASCII (or UTF-8) names, like everywhere in the project.
* **The library** is the SQLite amalgamation (`third_party/sqlite/sqlite3.c` and `sqlite3.h`, version 3.54.1, public domain) compiled as plain C by `build.bat` (once, into `build\obj\sqlite3.obj`) and by the project `fa_sqlite` of the Visual Studio solutions, with `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_THREADSAFE=1`, `SQLITE_DEFAULT_MEMSTATUS=0` and `SQLITE_OMIT_SHARED_CACHE`. See `MODELS.md` for where it came from.

### Background: card first

In `match.md` and `pipeline.md`, **M** is the number of currently detected faces and **N** the number of faces registered in the database. Normally every face is compared with all N people. In some scenes the set of candidates is only a **subset** of the database: a person shows an RF card. The rule is that **the card information is always identified first**:

1. The card number is looked up in the database (`find_by_card`, `index_of_card`) and the record of its owner, with the feature stored in it, is retrieved. So each database record holds **both** the face feature and the card number (`card_id`).
2. Only then are the faces detected in the current frame (M of them) compared with the retrieved features (N cards give N features): `FeatureStore::match_cards`, which uses `FeatureMatcher::score_matrix` to compute all M x N similarities in one call.
3. **If the card is not in the database, the comparison for it is treated as a failure**: the card is marked `found = false`, nothing is compared for it, `match_cards` returns `false`. There is **never a fallback** to a search among all registered people (that would accept a stranger who holds an unknown card).

M and N are independent of each other: **M < N** (one face, three cards), **M = N** and **M > N** (four faces, two cards) are all handled in the same call, and the result tells both ways round: for every card which face looks most like its owner, and for every face which card's owner it looks most like (`CardMatch`). The store only reports similarities; the **caller decides** with its threshold (`AuthPipeline` verifies a face as the owner of the card it is most like when the similarity reaches the profile's threshold). An empty card number means "no card": it is never found, and no two people may share a non-empty card number. `set_card_id` gives a person a card or takes it away (written through like the other changes).

### Background: lazy matcher rebuild

The matching engine works on its own padded copy of all features (see `match.md`). Building that copy takes time proportional to the number of registered people, so the store does not rebuild it after every change. It keeps a flag, `matcher_dirty_`, that says "the copy is out of date". `open`, `load`, `add`, `remove` and `update` (when a feature is replaced) set the flag; `matcher()` rebuilds only if the flag is set, and then clears it. Registering five people in a row therefore causes one rebuild, at the next call to `matcher()`, not five. A change that is undone because the file could not be written leaves the flag set: the next `matcher()` rebuilds the table from the restored list.

## src/store/record.h

The small header that holds `Record`, the struct of one registered person. It is separate from `feature_store.h` so that `sqlite_db.h`, which reads and writes records, does not have to include the whole store. Macro: `FA_RECORD_H` is the include guard (it stops the header from being read twice). `feature_store.h` includes it, so every user of the store sees `Record` as before.

### Record (struct)

```cpp
struct Record {
    std::string id;
    std::string name;
    Feature feature;
    std::string card_id;  // RF card number of the person; empty = no card (unique among the records)
};
```

**What it does.** One registered person. `id` is the unique key (for example an employee number), `name` is for display, `feature` is the feature vector (type `Feature` from `types.h`), and `card_id` is the number of the person's RF card, or an empty text when the person has none. The store accepts a record only if its feature is a finite vector of length 1 (see `is_usable_feature`) and its card (when not empty) belongs to nobody else; `Record` itself does not check anything.

**How it works.** A plain struct with no functions, so it can be created with braces, for example `Record{"E001", "Kim, Min-su", feature}` (no card) or `Record{"E001", "Kim, Min-su", feature, "04A1B2C3"}`. The store never changes `id` after registration; `update` can change `name` and `feature`, and `set_card_id` the card. The same struct is what both file formats hold (a CSV line, a row of the SQLite table `people`).

**Used by.** `FeatureStore` (the list `records_`), `SqliteDb` (`load_all` fills records, `insert`, `update` and `replace_all` write them), `AuthPipeline::enroll_track` and `AuthPipeline::enroll_image` (build a record to register, with the card when one is given), `AuthPipeline::match_results` (reads the matched record), `make_synthetic_gallery` in `src/testdata/synthetic.cpp`, `tools/gen_features.cpp`, `tools/db_convert.cpp`, `tools/eval_footage.cpp`, `tools/bench_match_part.h` (`bench_matching`, run by `bench` and `bench_match`), `tests/test_match.cpp`.

## src/store/feature_store.h

The public header of the store. Its long header comment is the specification of the managed database: the two file formats and the user's choice, the CSV format, the card-first rule, the guarantees, and the cost of a change. Macro: `FA_FEATURE_STORE_H` is the include guard. It includes `matcher.h` (for `FeatureMatcher` and `kNoScore`), `record.h` and `types.h`, and forward-declares `class SqliteDb;` (`sqlite_db.h`) so that the header needs neither the SQLite header nor `<windows.h>`.

| Function | Purpose |
|---|---|
| `DatabaseFormat` (enum class) | The file format of a database: `Auto`, `Csv`, `Sqlite`. |
| `CardMatch` (struct) | The result of a card-first comparison: for every card and for every face. |
| `FeatureStore` (class) | The list of records, its database file (CSV or SQLite), its matcher. |
| `FeatureStore()` | Creates an empty store that is not open. |
| `FeatureStore(const FeatureStore&)` and `operator=` (deleted) | Forbid copying. |
| `is_open()` | Is the store bound to a database file? |
| `path()` | The bound database file (empty when the store is not open). |
| `format()` | The format of the open database (`Csv` or `Sqlite`). |
| `count()` | Number of registered people. |
| `records()` | Read-only access to all records. |
| `last_error()` | Text of the most recent failure. |

Declared here and documented under `feature_store.cpp`, where they are defined: `~FeatureStore`, `open`, `close`, `load`, `save`, `add`, `remove`, `update`, `set_card_id`, `find`, `find_by_card`, `index_of_card`, `match_cards`, `matcher`, and the private `read_csv`, `read_sqlite`, `resolve_format` and `persist` (with its enum `Change`).

### DatabaseFormat (enum class)

```cpp
enum class DatabaseFormat { Auto, Csv, Sqlite };
```

**What it does.** The file format of a database, and the way the user chooses it. `Csv` is the default format of the project (a text file, see "Background: the CSV file format"), `Sqlite` is the SQLite database (see "Background: the SQLite format"), and `Auto` means "decide from the file": an existing file by its content, a new file by its extension. The full rules are in "Background: choosing the format".

**How it works.** A scoped enumeration of three values. It is the type of the parameter `format` of `FeatureStore::open`, `load` and `save` (all default to `Auto`), of `FeatureStore::format()`, and of the parameter `db_format` of `AuthPipeline::init`.

**Used by.** `FeatureStore`, `AuthPipeline::init`, `tools/db_convert.cpp` (`--format csv|sqlite`), `tests/test_match.cpp` (`test_database`, `test_format_choice`), `tests/test_pipeline.cpp` (`main`: `--sqlite`).

### CardMatch (struct)

```cpp
struct CardMatch {
    struct Card {
        std::string card_id;
        bool found = false;
        int record = -1;
        int best_query = -1;
        float best_score = kNoScore;
    };
    struct Query {
        int best_card = -1;
        float score = kNoScore;
    };
    std::vector<Card> cards;
    std::vector<Query> queries;
};
```

**What it does.** The result of a card-first comparison (`FeatureStore::match_cards`): M faces against the N registered features that N cards stand for. It gives both views of the M x N comparison. `cards` has one entry for every card that was given, in the order given (N entries); `queries` has one entry for every face that was given, in the order given (M entries).

| Field | Meaning |
|---|---|
| `Card::card_id` | The card number, as given. |
| `Card::found` | The card is in the database. `false` means that the comparison for this card **failed**: nothing was compared for it. |
| `Card::record` | The position of the card's owner in `FeatureStore::records()` (also his position in the matcher's table); `-1` when the card was not found. |
| `Card::best_query` | Which of the M faces (an index into `queries` as given) is most like the card's owner; `-1` when no face could be compared with him (the card is not found, or there is no face, or the faces have the wrong length). |
| `Card::best_score` | That face's similarity to the owner; `kNoScore` (`-2`) when `best_query` is `-1`. |
| `Query::best_card` | An index into `cards`: the card whose owner this face is most like; `-1` when the face could not be compared with any card. |
| `Query::score` | That similarity; `kNoScore` when `best_card` is `-1`. |

**How it works.** A plain struct, filled by `FeatureStore::match_cards`, which starts by resetting it. The scores are cosine similarities (the features have length 1, see `match.md`); the struct does not apply a threshold: the caller accepts a face when `Query::score` reaches its threshold (`AuthPipeline::match_results` does).

**Used by.** `FeatureStore::match_cards`, `AuthPipeline::match_results` and `AuthPipeline::begin_card_check`, `tests/test_match.cpp` (`test_card_first`).

### FeatureStore (class)

```cpp
class FeatureStore {
public:
    ...
private:
    bool read_csv(const std::string& path, std::vector<Record>& out) const;
    bool read_sqlite(const std::string& path, bool create, std::vector<Record>& out, std::unique_ptr<SqliteDb>* db) const;
    bool resolve_format(const std::string& path, DatabaseFormat format, DatabaseFormat& resolved) const;
    enum class Change { Add, Remove, Update };
    bool persist(Change what, const Record& record);

    std::vector<Record> records_;
    std::string path_;
    DatabaseFormat format_ = DatabaseFormat::Csv;
    std::unique_ptr<SqliteDb> sqlite_;
    void* lock_ = nullptr;
    mutable FeatureMatcher matcher_;
    mutable bool matcher_dirty_ = true;
    mutable std::string error_;
};
```

**What it does.** The container for the registered people and for their database file. The public functions fall into groups: the database file (`open`, `close`, `is_open`, `path`, `format`), plain file functions (`load`, `save`), management (`add`, `remove`, `update`, `set_card_id`), reading (`count`, `records`, `find`, `find_by_card`, `index_of_card`, `match_cards`, `matcher`) and error text (`last_error`). The private functions are `read_csv` (read and check a CSV file), `read_sqlite` (read and check a SQLite database), `resolve_format` (which format has this file under the user's choice) and `persist` (write one change to the database file). `Change` is the private enumeration of the three kinds of change that `persist` knows (`Add`, `Remove`, `Update`; `set_card_id` and a name or feature change both count as `Update`).

**How it works.** The members:

| Member | Role |
|---|---|
| `records_` | All registered people, in the order of the file. The position in this list is the `index` that the matcher reports. |
| `path_` | The database file the store is bound to by `open`; empty when the store is not open. It is also the test for "open" (`is_open()`). |
| `format_` | The format of that database while the store is open (`Csv` or `Sqlite`, never `Auto`); `Csv` when the store is not open. `format()` returns it. |
| `sqlite_` | The open SQLite connection (a `SqliteDb`) while the store is open on a SQLite database; null for CSV and when the store is not open. It is a `std::unique_ptr` of a class that the header only forward-declares. |
| `lock_` | The Windows handle of the lock file `<path_>.lock` while the store is open, null otherwise. It is declared `void*` so that the header does not need `<windows.h>`. |
| `matcher_` | The `FeatureMatcher` holding a fast copy of all features. |
| `matcher_dirty_` | `true` when `matcher_` is out of date. Starts as `true`. |
| `error_` | Text of the most recent failure (returned by `last_error()`). |

The last three are declared `mutable`, which is what allows the `const` functions `matcher()` (to rebuild), `save()`, `read_csv()`, `read_sqlite()`, `resolve_format()` and `match_cards()` (which calls `matcher()` and sets an error text) to change them. The store contains no locking between threads, so it should be used from one thread at a time (the lock described under "Background: the managed database" works between programs, not between threads). The copy operations are deleted: `FeatureMatcher` and `std::unique_ptr` cannot be copied anyway, and a copy would also share the lock handle, which only one owner may release.

**Used by.** `AuthPipeline` (member `store_`, with the accessor `AuthPipeline::store()`), `tools/gen_features.cpp`, `tools/db_convert.cpp`, `tools/eval_footage.cpp`, `tools/bench_match_part.h` (`bench_matching`, run by `bench` and `bench_match`), `tests/test_match.cpp`, `tests/test_pipeline.cpp`.

### FeatureStore()

```cpp
FeatureStore();
```

**What it does.** Creates an empty store that is not open: no records, no database file (`is_open()` is `false`, `path()` is empty, `format()` is `Csv`), no lock, no SQLite connection. Add records with `add`, read a file with `load`, or open a database with `open`.

**How it works.** The constructor is declared in the header and **defined in `feature_store.cpp`** (`FeatureStore::FeatureStore() {}`), together with the destructor: the member `std::unique_ptr<SqliteDb> sqlite_` needs the complete type `SqliteDb` wherever the constructor or the destructor is compiled, and the header only forward-declares it. The members have default values, so the body is empty.

**Used by.** `AuthPipeline` (member `store_`), `tools/gen_features.cpp`, `tools/db_convert.cpp`, `tools/eval_footage.cpp`, `tools/bench_match_part.h` (`bench_matching`), `tests/test_match.cpp`, `tests/test_pipeline.cpp`.

### FeatureStore(const FeatureStore&) and operator= (deleted)

```cpp
FeatureStore(const FeatureStore&) = delete;
FeatureStore& operator=(const FeatureStore&) = delete;
```

**What it does.** Makes it a compile error to copy a `FeatureStore`.

**How it works.** A `FeatureStore` contains a `FeatureMatcher`, which cannot be copied (see `match.md`), a `std::unique_ptr` (the SQLite connection), and a lock handle that only one object may release; copying one would let two stores think they own the same database lock. Deleting the copy operations prevents that.

**Used by.** Nothing calls them; they exist to stop mistakes.

### is_open

```cpp
bool is_open() const { return !path_.empty(); }
```

**What it does.** Returns `true` while the store is bound to a database file: after a successful `open` and until `close` (or the destructor). While it is `true`, `add`, `remove` and `update` write the file, and `load` is refused.

**How it works.** A one-line test of `path_`.

**Used by.** `FeatureStore::load` (refuses when open), `tests/test_match.cpp` (`test_database`).

### path

```cpp
const std::string& path() const { return path_; }
```

**What it does.** Returns the database file (CSV or SQLite) the store is bound to, exactly as it was given to `open`, or an empty text when the store is not open. `AuthPipeline` has no path member of its own any more: the database path of a pipeline is `pipeline.store().path()`.

**How it works.** Returns a reference to `path_`; the reference refers to the old text after a later `open` or `close`.

**Used by.** `tests/test_match.cpp` (`test_database`).

### format

```cpp
DatabaseFormat format() const { return format_; }
```

**What it does.** Returns the format of the open database: `DatabaseFormat::Csv` or `DatabaseFormat::Sqlite`, never `Auto`: it is what the choice `Auto` or the explicit choice turned into when the database was opened. When the store is not open it returns `Csv`.

**How it works.** A one-line getter on `format_`.

**Used by.** `tests/test_match.cpp` (`test_database`, `test_format_choice`: the format that a database was opened in).

### count

```cpp
int count() const { return static_cast<int>(records_.size()); }
```

**What it does.** Returns how many people are registered.

**How it works.** The size of `records_`, converted to `int`.

**Used by.** `AuthPipeline::init` (checks the length of the features read from the database only if the store is not empty), `AuthPipeline::match_results` (does not call the matcher when the store is empty and no card check is active), `tools/eval_footage.cpp`, `tools/db_convert.cpp`, `tests/test_match.cpp`, `tests/test_pipeline.cpp`.

### records

```cpp
const std::vector<Record>& records() const { return records_; }
```

**What it does.** Gives read-only access to all records, in file order. The position of a record in this list equals the `index` in a `MatchResult` produced by the store's matcher.

**How it works.** Returns a reference to the internal vector; nothing is copied. The reference (and every pointer or reference into it) becomes invalid after `add`, `remove` or `load`.

**Used by.** `AuthPipeline::init` (compares the length of the first record's feature with the extractor's), `AuthPipeline::match_results` (`records()[matches[q].index]` gives the person who was matched; during a card check `records()[card.record]`, the record that a card was retrieved from, gives the owner), `tools/db_convert.cpp`, `tools/eval_footage.cpp`, `tests/test_match.cpp`.

### last_error

```cpp
const std::string& last_error() const { return error_; }
```

**What it does.** Returns a text describing the most recent failure of `open`, `load`, `save`, `add`, `remove` or `update`, for example `unknown id: E999`.

**How it works.** Returns the member `error_`. The functions set it only when they fail and never clear it, so after a later success it still holds the old text; read it only right after a call that returned `false`.

**Used by.** `AuthPipeline::init`, `AuthPipeline::enroll_track`, `AuthPipeline::enroll_image`, `AuthPipeline::remove_person` and `AuthPipeline::rename_or_refresh` (each prints it on `std::cerr` when the store refuses or cannot write), `tools/gen_features.cpp`, `tools/db_convert.cpp`, `tests/test_match.cpp`.

## src/store/feature_store.cpp

The implementation. It includes `<windows.h>`: the lock and the replacement of the file use the Windows functions `CreateFileA`, `CloseHandle`, `GetFileAttributesA` and `MoveFileExA`. It includes `sqlite_db.h` for the SQLite format. Nine helper functions in an anonymous namespace (private to this file) check a feature, do the CSV quoting, name the format of a file, and deal with the file, the lock and the safe write; the class functions follow. `trim()` is borrowed from `profile.h`.

| Function | Purpose |
|---|---|
| `is_usable_feature` | Check that a feature is a finite vector of length 1. |
| `csv_field` | Write one text field, with quotes when needed. |
| `split_csv_line` | Split one CSV line into fields, understanding quotes. |
| `file_exists` | Does a file or folder with this path exist? |
| `has_sqlite_extension` | Is a file name `.db`, `.sqlite` or `.sqlite3` (any letter case)? |
| `check_loaded_records` | The rules for rows read from a SQLite database (the same as for a CSV line). |
| `take_lock` | Take the lock of a database: a lock file nobody else can open. |
| `release_lock` | Let the lock go (Windows deletes the lock file). |
| `write_csv_file` | Write records as a CSV file safely (temporary file, then a one-step swap with `MoveFileExA`). |
| `FeatureStore::FeatureStore` | Create an empty store (defined here, with the destructor). |
| `FeatureStore::~FeatureStore` | Release the lock. |
| `FeatureStore::read_csv` | Read and check a CSV file (private). |
| `FeatureStore::read_sqlite` | Read and check a SQLite database (private). |
| `FeatureStore::resolve_format` | Which format has this file under the user's choice (private). |
| `FeatureStore::load` | Read a database file of either format, replacing the content (refused while open). |
| `FeatureStore::save` | Write a database file of either format safely. |
| `FeatureStore::open` | Open the database: choose the format, lock, read (or create) the file, bind the store to it. |
| `FeatureStore::close` | Release the lock and the SQLite connection and forget the binding. |
| `FeatureStore::persist` | Write one change to the database file (private): CSV rewrites the file, SQLite changes one row. |
| `FeatureStore::find` | Look a record up by id. |
| `FeatureStore::add` | Register a person (written through when open). |
| `FeatureStore::remove` | Delete a person (written through when open). |
| `FeatureStore::update` | Change the name and/or the feature of a person (written through when open). |
| `FeatureStore::set_card_id` | Give a person an RF card or take it away (written through when open). |
| `FeatureStore::find_by_card` | Look a record up by RF card number. |
| `FeatureStore::index_of_card` | The position of the card's owner in the list (= in the matcher), or -1. |
| `FeatureStore::match_cards` | Card first: look the cards up, retrieve their features, compare the faces with exactly those. |
| `FeatureStore::matcher` | Return the matcher, rebuilding it first if needed. |

### is_usable_feature

```cpp
bool is_usable_feature(const Feature& f)
```

**What it does.** Returns `true` if the feature `f` is safe to keep in the store: every number is finite (not NaN, not infinity) and the length of the vector is within a small tolerance of 1, strictly between 0.95 and 1.05. It does not change `f`.

**How it works.**
1. Walk over all numbers. If one is not finite (`std::isfinite`), return `false` at once.
2. Add up the squares of all numbers (in `double`).
3. Return `true` only if that sum is greater than `0.95 * 0.95` and less than `1.05 * 1.05` (that is, the length is between 0.95 and 1.05). An empty or all-zero feature has sum 0 and is refused.

Why the check exists (the code comment): the matcher's score is a plain dot product, which is only a cosine similarity for vectors of length 1. A record with a huge number, or with NaN, would otherwise score high against every query. The tolerance leaves room for float rounding (a feature saved with 9 significant digits is read back exactly, so a feature that passed once passes again after a save and load). A feature straight from `normalize_feature` (see `feature.md`) passes; an all-zero feature, which `normalize_feature` leaves zero, does not.

**Used by.** `FeatureStore::read_csv`, `check_loaded_records` (the rows of a SQLite database), `FeatureStore::add` and `FeatureStore::update`.

### csv_field

```cpp
std::string csv_field(const std::string& text)
```

**What it does.** Returns `text` ready to be written as one CSV field: unchanged if it is safe, otherwise wrapped in double quotes with every inner quote doubled.

**How it works.**
1. If `text` contains none of the characters `,` `"` line feed or carriage return, return it unchanged.
2. Otherwise start with `"`, copy every character (writing `""` for each `"`), and finish with `"`.

Examples: `Alice Kim` stays `Alice Kim`; `Kim, Min-su` becomes `"Kim, Min-su"`; `Lee "JJ" Park` becomes `"Lee ""JJ"" Park"`. The comment in `feature_store.h` mentions only commas, and the comment above `csv_field` only commas and quotes; the code also quotes line feeds and carriage returns. Such quoted line breaks could not be read back anyway, because `read_csv` reads the file line by line; that is why `add` refuses ids and names that contain them and `update` refuses such names (see `add` and `update`).

**Used by.** `write_csv_file` (for `id`, `name` and `card_id`).

### split_csv_line

```cpp
bool split_csv_line(const std::string& line, std::vector<std::string>& fields)
```

**What it does.** Splits one line of the file into its fields. Returns `true` and fills `fields` on success; returns `false` if a quote is opened but never closed.

**How it works.** It walks over the characters with a flag `in_quotes`:
- Inside quotes: a `"` followed by another `"` adds one literal quote to the field (and skips the second one); a lone `"` ends the quoted part; every other character is added to the field.
- Outside quotes: a `"` starts a quoted part; a `,` ends the field (it is stored and a new one starts); every other character is added to the field.
- At the end the last field is stored. If the line ends while still in quotes, return `false`.

The reader is lenient: a quote in the middle of an unquoted field also starts a quoted part (for example `ab"c,d"e` becomes one field `abc,de`). The function never produces fewer than one field.

**Used by.** `FeatureStore::read_csv`.

### file_exists

```cpp
bool file_exists(const std::string& path)
```

**What it does.** Returns `true` if something (a file or a folder) exists at `path`, `false` otherwise. It does not say whether the file can be read.

**How it works.** `GetFileAttributesA(path)` is not `INVALID_FILE_ATTRIBUTES`.

**Used by.** `FeatureStore::open` (to decide between reading the database file and creating an empty one), `FeatureStore::load` (a missing file is `cannot open <path>`) and `FeatureStore::resolve_format` (does the file exist yet?).

### has_sqlite_extension

```cpp
bool has_sqlite_extension(const std::string& path)
```

**What it does.** Returns `true` if the file name ends in `.db`, `.sqlite` or `.sqlite3`, in any letter case (`PEOPLE.DB` and `fmt_d.SQLITE3` count). It only looks at the name; the file need not exist. It is used for a database file that does not exist yet (or is empty), where there is no content to look at.

**How it works.** The extension is the text after the last dot of the path, and only if no folder separator (`\` or `/`) follows that dot (so `build.v2\people` has no extension). No dot: `false`. The extension is turned to lower case (`std::tolower` on each character) and compared with `db`, `sqlite` and `sqlite3`.

**Used by.** `FeatureStore::resolve_format` (the format of a new file under `Auto`) and `FeatureStore::save` (the format under `Auto`).

### check_loaded_records

```cpp
bool check_loaded_records(const std::vector<Record>& records, const std::string& where, std::string& error)
```

**What it does.** Applies to the rows that were read from a SQLite database the same rules that `read_csv` applies to the lines of a CSV file, besides what SQLite itself guarantees (unique ids and unique card numbers, a blob that fits its `dim`). Returns `true` if every row is acceptable. Otherwise it returns `false` and puts the reason into `error`: `<where>: row '<id>': the feature is not a finite vector of length 1`, or `<where>: row '<id>': feature length <n> differs from the first row (<m>)`. `where` is the file name, put in front of the message.

**How it works.** For every record: `is_usable_feature` (every number finite, length 1 within the tolerance); and its `dim()` must equal the first record's (the matcher needs all features to have the same length). The first problem ends the check. An empty list is fine.

**Used by.** `FeatureStore::read_sqlite`.

### take_lock

```cpp
void* take_lock(const std::string& lock_path)
```

**What it does.** Takes the lock of a database: creates the file `lock_path` and keeps it open in a way that nobody else can open it. Returns the Windows handle of the open file (as `void*`, so that the header needs no `<windows.h>`), or null when this fails: because another store or program holds the lock, or because the lock file cannot be created (for example the folder does not exist). It writes nothing into the file and sets no error text (the caller, `open`, does).

**How it works.** One call: `CreateFileA(lock_path, GENERIC_WRITE | DELETE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr)`.
* **Sharing mode 0** means: while this handle is open, no other open of the file succeeds, not even from the same program. That is the lock; `test_database` checks that a second store in the same program cannot open the database.
* **`FILE_FLAG_DELETE_ON_CLOSE`** (this needs the `DELETE` access that is also asked for) makes Windows delete the file when the last handle to it is closed. The handle is closed by `release_lock`, and also by Windows when the program ends, crashes or is killed, so a lock can never be left behind.
* **`OPEN_ALWAYS`** creates the file when it does not exist and opens it when it does.

**Used by.** `FeatureStore::open` (with the path of the database plus `.lock`).

### release_lock

```cpp
void release_lock(void* lock)
```

**What it does.** Lets a lock go: closes the handle, and Windows deletes the lock file because it was opened with delete-on-close. A null pointer (no lock) does nothing.

**How it works.** `CloseHandle` on the handle, if it is not null.

**Used by.** `FeatureStore::open` (the new lock when opening fails, the old lock when the store moves to a new database), `FeatureStore::close`.

### write_csv_file

```cpp
bool write_csv_file(const std::string& path, const std::vector<Record>& records, std::string& error)
```

**What it does.** Writes `records` to `path` in the CSV format above, safely. Returns `true` on success; on failure returns `false` and puts the reason into `error` (`cannot write <path>.tmp`, `write error in <path>.tmp` or `cannot replace <path> with <path>.tmp`). The target folder must already exist (the function does not create folders). An empty list writes a file with only the header line. On failure the file at `path` is untouched.

**How it works (atomic save).**
1. Open the temporary file `path + ".tmp"` in the same folder (truncating it if it exists). If that fails (the folder does not exist, or the name is taken by a folder), return `false` with `cannot write <path>.tmp`.
2. Write the header line `id,name,card_id,dim,values`. Then for every record write `csv_field(id)`, `csv_field(name)`, `csv_field(card_id)`, the `dim`, and each value formatted with `%.9g`, separated by commas, then a line feed. (The stream is in text mode, so on Windows the line feed becomes carriage return plus line feed.)
3. Flush and check that nothing went wrong; on a write error close the file, **delete the temporary file** and return `false` with `write error in <path>.tmp`.
4. Close the temporary file (the block ends), then swap it in with one Windows call, `MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. `MOVEFILE_REPLACE_EXISTING` lets the move replace a file that is already at `path`; `MOVEFILE_WRITE_THROUGH` asks Windows not to return before the move has been written to disk (this is the meaning given in the Windows API documentation; the code does not explain it). If the call fails, **delete the temporary file** (the code comment: do not leave the finished copy lying around) and return `false` with `cannot replace <path> with <path>.tmp`; the old file at `path` is untouched.

Why a temporary file and a one-step swap: the real file is never opened for writing, and the swap is a single operation, so at every moment `path` holds either the complete old file or the complete new one (this is what the comment in the code states). A crash or a full disk while writing cannot leave a half-written CSV at `path`; at worst a `.tmp` file is left behind by a crash, which nothing reads (`read_csv` is never pointed at it, and the next write truncates it). The code does not delete the old file first and then rename (`std::rename` on Windows refuses to overwrite an existing file, which is why `MoveFileExA` is used). Because of `MoveFileExA`, `feature_store.cpp` includes `<windows.h>` and the store, like the rest of the project, builds on Windows only.

The whole file is rewritten on every call, so the time grows with the number of records (see "Background: two formats, and why CSV is the default"; the SQLite format changes one row instead).

**Used by.** `FeatureStore::save` (format CSV), `FeatureStore::persist` (every change of a database in the CSV format), and `FeatureStore::open` (to create the database file when it does not exist: an empty list, so the new file holds only the header line).

### FeatureStore::FeatureStore

```cpp
FeatureStore::FeatureStore()
```

**What it does.** Creates an empty store that is not open (see `FeatureStore()` in the header section).

**How it works.** The body is empty (`{}`): every member has a default value. It is defined here and not in the header, like the destructor, because of the `std::unique_ptr<SqliteDb>` member (the header only forward-declares `SqliteDb`).

**Used by.** Wherever a `FeatureStore` is created (see `FeatureStore()`).

### FeatureStore::~FeatureStore

```cpp
FeatureStore::~FeatureStore()
```

**What it does.** Releases the lock of an open database (and closes the SQLite connection, if there is one) when the store is destroyed, so that another store or program can open the database. It writes nothing (every change was already written when it was made).

**How it works.** Calls `close()`. If the program ends without running destructors (a crash, a kill), Windows closes the lock file's handle and deletes the file anyway (see `take_lock`).

**Used by.** Runs automatically when a `FeatureStore` goes out of scope or is destroyed (member `AuthPipeline::store_`); `tests/test_match.cpp` (`test_database` checks that a database can be opened again after the store that held it was destroyed).

### FeatureStore::read_csv

```cpp
bool FeatureStore::read_csv(const std::string& path, std::vector<Record>& loaded) const
```

**What it does.** Private. Reads the CSV file at `path` and checks it, line by line, into `loaded` (which is emptied first). Returns `true` on success. Returns `false` and sets `last_error()` if the file cannot be opened or any line is damaged. It does not touch the store's own records: the caller swaps `loaded` in only when the whole file was good, so on failure the old content stays untouched. A file that exists but is empty gives an empty list and `true`. It is `const`; it only writes `error_`, which is `mutable`.

**How it works.** For each line:
1. On the first line only: if it starts with the three bytes EF BB BF (a UTF-8 byte order mark), remove them.
2. Skip lines that are empty or only whitespace (`trim`).
3. `split_csv_line`; fewer than 3 fields, or an unclosed quote: error `<path>:<line>: malformed line`.
4. On the first non-empty line only: if its third field is the text `card_id`, it is the **header line**: remember that this file has the card column (`with_cards`) and go on with the next line (the header is not a record). Otherwise the file is in the older format, without a card column.
5. The `dim` is the 4th field with the card column and the 3rd field without it. If the line has no such field: error `<path>:<line>: malformed line`. `dim` = that field converted with `atoi`. If `dim <= 0` (this includes text that is not a number) or the number of fields is not (position of `dim` + 1 + `dim`): error `<path>:<line>: expected <dim> numbers`.
6. Fill a `Record`: id, name, the card number (only in the new format; otherwise it stays empty), and `dim` numbers converted with `atof`. A number that is not valid text becomes 0 without any error.
7. If `is_usable_feature` refuses the feature (a number is NaN or infinite, or the length is not within 0.95 to 1.05): error `<path>:<line>: the feature is not a finite vector of length 1`.
8. If this is not the first record and its `dim` differs from the first record's: error `<path>:<line>: feature length <dim> differs from the first line (<first dim>)`. All features in a file must have the same length (the matcher needs that).
9. If an earlier line has the same id: error `<path>:<line>: duplicate id <id>`. If the card number is not empty and an earlier line has the same one: error `<path>:<line>: duplicate card_id <card>` (an empty card number means "no card" and may repeat). Both checks use a hash set (`std::unordered_set`) of the ids and card numbers seen so far, so the time grows in proportion to the number of lines (an earlier version compared each id with every earlier one, which grew with the square of the number of lines; 10,000 lines of 512 numbers now load in about 1 s, see "Background: two formats, and why CSV is the default").
10. Keep the record.

A missing file gives `cannot open <path>`. A file with only the header line (or an empty file) gives an empty list.

Things the reader does **not** check: that the id is not empty (`add` refuses an empty id, `read_csv` does not), and that an id contains no line break (it cannot, because the file is read line by line). The caller `AuthPipeline::init` still compares only the first record's length with the extractor's, which is enough because `read_csv` guarantees the same length on every line.

**Used by.** `FeatureStore::load` and `FeatureStore::open`.

### FeatureStore::read_sqlite

```cpp
bool FeatureStore::read_sqlite(const std::string& path, bool create, std::vector<Record>& loaded,
                               std::unique_ptr<SqliteDb>* keep) const
```

**What it does.** Private. Reads the SQLite database at `path` and checks it into `loaded`. Returns `true` on success; `false` with `last_error()` set when the file is not a usable database (see `SqliteDb::open`), a row is damaged (see `SqliteDb::load_all`) or a row breaks the rules of the store (see `check_loaded_records`). Like `read_csv` it does not touch the store's own records: the caller swaps `loaded` in only when everything was good. `create` says that a missing file is created, empty, with the table (`open` passes `true` for a file that does not exist yet; `load` passes `false`). `keep`, when it is not null, receives the open connection on success (`open` keeps it as `sqlite_` to write the changes later); when it is null the connection is closed again before the function returns (`load`). It is `const`; it only writes `error_`, which is `mutable`.

**How it works.** A new `SqliteDb` is created; `db->open(path, create, error_)`, then `db->load_all(loaded, error_)`, then `check_loaded_records(loaded, path, error_)`; if any of them fails the function returns `false` (the `SqliteDb` is destroyed, which closes the file). Otherwise the connection is moved into `*keep` if `keep` is given.

**Used by.** `FeatureStore::load` (`create = false`, `keep = nullptr`) and `FeatureStore::open` (`create` is true for a missing file, `keep = &db`).

### FeatureStore::resolve_format

```cpp
bool FeatureStore::resolve_format(const std::string& path, DatabaseFormat format, DatabaseFormat& resolved) const
```

**What it does.** Private. Decides which format the database at `path` has under the user's choice `format` (see "Background: choosing the format"), and puts it into `resolved` (`Csv` or `Sqlite`, never `Auto`). Returns `true`. Returns `false`, sets `last_error()` and leaves `resolved` alone when the user asked for one format and the existing file is of the other: `<path> is a SQLite file, not a CSV database as asked (use tools/db_convert to convert it)` or `<path> is a CSV file, not a SQLite database as asked (use tools/db_convert to convert it)`. A file is never misread.

**How it works.**
1. What the file **is**: it starts as "SQLite" if `has_sqlite_extension(path)` (this is the answer for a file that does not exist or is empty). If the file exists and has at least one byte, the answer is taken from its content instead: `is_sqlite_file(path)`, and the file counts as "known by content".
2. `Auto`: `resolved` is `Sqlite` if the file is SQLite by step 1, else `Csv`.
3. `Csv` or `Sqlite`: if the file is known by content and it is of the other kind than asked for, return `false` with the message above. Otherwise `resolved = format`, as asked (so a new file called `x.db` can be made a CSV file on purpose).

**Used by.** `FeatureStore::open` and `FeatureStore::load` (both before anything is locked, opened or read, so a refusal leaves no trace). `FeatureStore::save` does not call it: it has no existing file to protect and decides by the extension alone.

### FeatureStore::load

```cpp
bool FeatureStore::load(const std::string& path, DatabaseFormat format)
```

The parameter `format` defaults to `DatabaseFormat::Auto` in the header.

**What it does.** Reads the database file at `path` (CSV or SQLite, as `format` says; `Auto` recognises it by its content) and **replaces** the store's content with it, without binding the store to the file and without taking a lock (the plain file function; the managed way to read the database is `open`). Returns `true` on success. Returns `false` and sets `last_error()` if the file does not exist (`cannot open <path>`), is of the other format than the one asked for (see `resolve_format`), cannot be read, or is damaged (see `read_csv` and `read_sqlite` for the rules and the messages); in every such case the **old content stays untouched**. **Refused while the store is open**: the error is `the database <bound path> is open: close() it before load()`, nothing changes, and `false` is returned, because the memory would no longer match the database file. A CSV file that exists but is empty gives an empty store and `true`.

**How it works.**
1. If `is_open()`: set the error above and return `false`.
2. If the file does not exist: `cannot open <path>`, `false`.
3. `resolve_format(path, format, resolved)`; if that fails, return `false`.
4. `read_sqlite(path, false, loaded, nullptr)` for SQLite, `read_csv(path, loaded)` for CSV, into a local list; if that fails, return `false`.
5. Swap the list into `records_` and set the matcher flag (`matcher_dirty_ = true`).

**Used by.** `tools/db_convert.cpp` (reads the input database of either format, and reads the output back to compare), `tests/test_match.cpp` (`test_matching`, `test_store`, `test_card_first`, `test_database`, `test_format_choice`: several of them use a second store to read the database file while the first one has it open) and `tests/test_pipeline.cpp` (reads the pipeline's database file again). `AuthPipeline::init` does not call it; it uses `open`.

### FeatureStore::save

```cpp
bool FeatureStore::save(const std::string& path, DatabaseFormat format) const
```

The parameter `format` defaults to `DatabaseFormat::Auto` in the header.

**What it does.** Writes all records to `path` as a new database file of the given format. `Auto` decides by the extension only: `.db`, `.sqlite` and `.sqlite3` (any case) give a SQLite file, anything else a CSV file; an explicit `Csv` or `Sqlite` is obeyed whatever the name. Returns `true` on success; on failure returns `false` with a message in `last_error()`. It is a plain file function: it does not use the binding, it takes no lock and writes wherever `path` says (for example a backup copy of the database, or the other format of the same people: **this is how a database is converted**, see `tools/db_convert`). The target folder must already exist. An empty store writes an empty database (a CSV file with only the header line, a SQLite file with an empty table). An existing file at `path` is replaced completely, also by a file of the other format. In both formats the new file is written completely under another name first and then swapped in, so a crash cannot leave a half-written file at `path`.

**How it works.**
1. The format is `format`, or, for `Auto`, SQLite if `has_sqlite_extension(path)` and CSV otherwise.
2. **CSV:** `return write_csv_file(path, records_, error_);` (the safe write is described there).
3. **SQLite:** the temporary name is `path + ".tmp"`; a leftover file of that name is removed first. A `SqliteDb` is opened on it with `create = true` and `replace_all(records_)` writes all records in one transaction; if either step fails, the connection is closed, the temporary file is removed and `false` is returned. The connection is closed (end of the block: SQLite has written everything and removed its journal) and the finished file is swapped in with `MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`, as for CSV; if that fails the temporary file is removed and the error is `cannot replace <path> with <path>.tmp`.

`save` is a `const` function; it only writes `error_`, which is `mutable`.

**Used by.** `tools/gen_features.cpp` (the format follows the file name), `tools/db_convert.cpp` (the output format), `tests/test_match.cpp`. Not by the store itself any more: `persist` writes CSV with `write_csv_file` and SQLite with `SqliteDb`. The pipeline does not call it.

### FeatureStore::open

```cpp
bool FeatureStore::open(const std::string& path, DatabaseFormat format)
```

The parameter `format` defaults to `DatabaseFormat::Auto` in the header.

**What it does.** Opens the database file `path` as a managed database (see "Background: the managed database"), in the format the user chose (see "Background: choosing the format"): takes the lock, reads the file and binds the store to it. A file that does not exist yet is an empty database; it is **created at once** (CSV: a file with only the header line; SQLite: a database with an empty table `people`), so that a folder that cannot be written is noticed now and not at the first registration. The old content of the store is replaced by the content of the file. Returns `true` on success. Returns `false` and sets `last_error()` when: the path is empty (`no database path given`); the existing file is of the other format than the one asked for (see `resolve_format`); another store or program has the database open (`cannot open the database <path>: another program has it open (or <path>.lock cannot be created)`); the file is damaged (the message of `read_csv` with the line number, or the message of `read_sqlite`: not a SQLite database, a foreign or newer table layout, a damaged row); or the missing file cannot be created (`cannot write <path>.tmp` for CSV, `cannot open the SQLite database <path>: ...` for SQLite, for example because the folder does not exist). On every failure the store is exactly as before: the same content and, if it was open, still bound to its old database with its old lock and its old SQLite connection.

**How it works.**
1. An empty path: set the error and return `false`.
2. `resolve_format(path, format, resolved)`; a refusal returns `false` here, before any lock is taken.
3. `same_path` is `true` when `path` is the same text as `path_`, the path the store is already bound to. Opening that path again keeps the lock the store already holds and just reads the file again (so changes made to the file by hand become visible, and a damaged file is refused as usual).
4. For a new path: `take_lock(path + ".lock")`. If that returns null, set the error `cannot open the database ...` and return `false`.
5. Read the database. **SQLite:** `read_sqlite(path, !file_exists(path), loaded, &db)`: a missing file is created, empty, with the table; the open connection comes back in `db`. **CSV:** if `file_exists(path)`, `read_csv(path, loaded)`; otherwise `write_csv_file(path, loaded, error_)` with an empty list, which creates the empty database file.
6. If step 5 failed: `release_lock(new_lock)` (the lock file disappears again) and return `false`. Nothing else was changed: the store keeps its old binding, its old lock, its old connection and its old records.
7. Success: for a new path, release the old lock (`release_lock(lock_)`), keep the new one in `lock_` and set `path_`. Then `sqlite_` takes the new connection (`std::move(db)`: null for CSV; the old connection, if there was one, is closed by this assignment), `format_ = resolved`, the loaded list is swapped into `records_` and the matcher flag is set. Return `true`.

Because the new lock is taken before the old one is let go, and nothing is committed before the file has been read, a refused `open` cannot leave the store half-moved (`test_database` checks that a refused open keeps the old binding and the old records).

Things to know. (a) The path is compared as text. Another spelling of the same file (`build\db.csv` and `build/db.csv`) is treated as a new path; taking its lock then fails, because the store itself holds the lock file under the first spelling. This follows from the code; no test covers it. (b) The lock is the empty file `<path>.lock` next to the database; do not delete it by hand while a program runs. (c) Opening a second path while the store is open moves the store to the other database: the first database file is not touched and its lock is released. (d) A zero-byte file named `*.db` is taken for SQLite by `Auto` (an empty file has no content to look at), is opened without the permission to create the table, and is refused as `not a face_auth database`; a zero-byte file with any other name is an empty CSV database. This follows from the code; no test covers it.

**Used by.** `AuthPipeline::init` (with the user's `db_format`), `tests/test_match.cpp` (`test_database`, `test_format_choice`).

### FeatureStore::close

```cpp
void FeatureStore::close()
```

**What it does.** Forgets the binding and releases the lock and the SQLite connection (if there was one). The records stay in memory; from now on `add`, `remove` and `update` change only the memory, not the file. Calling it on a store that is not open does nothing. The database file is not touched.

**How it works.** `sqlite_.reset()` (this closes the SQLite connection), `release_lock(lock_)`, `lock_ = nullptr`, `path_.clear()`, `format_ = DatabaseFormat::Csv`. Another store (or program) can open the database from this moment on.

**Used by.** `FeatureStore::~FeatureStore`, `AuthPipeline::init` (when no path is given, and when the database's feature length does not fit the profile's extractor: it releases the lock again), `tests/test_match.cpp` (`test_database`: after `close()` the records are still there and the lock file is gone).

### FeatureStore::persist

```cpp
bool FeatureStore::persist(Change what, const Record& record)
```

**What it does.** Private. Writes one change to the database file after the change was made in memory. `what` is `Change::Add`, `Change::Remove` or `Change::Update` and `record` is the person concerned (for a removal the record that was taken out). When the store is not open there is nothing to write and it returns `true`. When it is open it returns `true` if the file now holds the change. If the write failed it returns `false` and sets `last_error()` to `the change was not saved and has been undone: <reason>` (the reason is the message of `write_csv_file` or of `SqliteDb`, for example `cannot write <path>.tmp`). `persist` itself changes nothing in memory: the caller (`add`, `remove`, `update` or `set_card_id`) undoes its change after `persist` returned `false`; the text is written here because the callers share it.

**How it works.** `if (path_.empty()) return true;` Then by format. **SQLite:** one row: `Add` calls `sqlite_->insert(record)`, `Remove` calls `sqlite_->erase(record.id)`, `Update` calls `sqlite_->update(record)` (the row with that id gets the new name, card and feature). **CSV:** the whole file: `write_csv_file(path_, records_, error_)` (`records_` already holds the change, so `what` and `record` are not needed). On failure the error text is prefixed.

**Used by.** `FeatureStore::add` (`Add`), `FeatureStore::remove` (`Remove`), `FeatureStore::update` and `FeatureStore::set_card_id` (`Update`).

### FeatureStore::find

```cpp
const Record* FeatureStore::find(const std::string& id) const
```

**What it does.** Returns a pointer to the record with this id, or `nullptr` if there is none.

**How it works.** A plain linear search through `records_` comparing ids, so the time grows with the number of registered people. The pointer points inside the internal vector and becomes invalid after `add`, `remove` or `load`. It does not set `last_error()`.

**Used by.** `FeatureStore::add` (duplicate check), `tests/test_match.cpp`, `tests/test_pipeline.cpp`.

### FeatureStore::add

```cpp
bool FeatureStore::add(const Record& record)
```

**What it does.** Registers a new person. Returns `true` on success. Returns `false` and sets `last_error()` when: the id is empty or the feature has no values (`record needs an id and a feature`); the feature is not a finite vector of length 1 (`the feature must be a finite vector of length 1`); the id, the name or the card number contains a carriage return or a line feed (`id, name and card_id must not contain line breaks`); the id is already registered (`id already registered: <id>`); the card number is not empty and belongs to another person (`card already belongs to <id of the owner>: <card>`); the feature length differs from that of the first registered record (`feature length differs from the registered ones`); or, when the store is open, the database file cannot be written (`the change was not saved and has been undone: <reason>`, see `persist`). The store is unchanged on failure, and so is the file.

**How it works.**
1. Check id and feature are present.
2. `is_usable_feature(record.feature)`: refuse a feature with a NaN or infinite number, or whose length is not within 0.95 to 1.05.
3. Refuse an id, a name or a card number that contains `\r` or `\n`. The CSV has one line per person and is read line by line, so a line break inside a text field could not be read back; the rule is the same for the SQLite format, so that both formats hold exactly the same.
4. `find(record.id)`: refuse a duplicate id.
5. If `record.card_id` is not empty, `find_by_card(record.card_id)`: refuse a card that another person owns (the error names the owner). Any number of people may have no card.
6. If the store is not empty, compare the feature length with `records_[0]`'s: all registered features must have the same length (the matcher needs that). An empty store accepts any length, and so does a store that has just been emptied.
7. Copy the record to the end of `records_` and set `matcher_dirty_`.
8. `persist(Change::Add, record)`: when the store is open, the change is written now (CSV: the whole file; SQLite: one row). If that fails, the new person is taken out again (`records_.pop_back()`) and the function returns `false`. The matcher flag stays set, so the next `matcher()` rebuilds the table from the restored list.

When the store is not open, nothing is written: the person lives in memory only. When it is open, the file holds the new person when `add` returns `true`; the price is one write of the whole file with CSV and one row with SQLite (see "Background: two formats, and why CSV is the default"). The pipeline therefore no longer saves after `add`. `add` does not scale a feature to length 1; it only refuses one that is not (the extractors do the scaling, see `feature.md`).

**Used by.** `AuthPipeline::enroll_track`, `AuthPipeline::enroll_image`, `tools/gen_features.cpp`, `tools/bench_match_part.h` (`bench_matching`, run by `bench` and `bench_match`), `tests/test_match.cpp` (`test_matching`, `test_m_to_n_shapes`, `test_store`, `test_card_first`, `test_database` (which also tests the undo, for both formats), `test_format_choice` and `test_database_speed`).

### FeatureStore::remove

```cpp
bool FeatureStore::remove(const std::string& id)
```

**What it does.** Deletes the person with this id. Returns `true` if found and deleted; otherwise returns `false` and sets `last_error()` to `unknown id: <id>`, or, when the store is open and the database file cannot be written, to `the change was not saved and has been undone: <reason>`. On `false` the person is still registered, in the same position, and the file is as it was.

**How it works.** Linear search; on a hit a copy of the record is kept, `records_.erase(...)` removes it (the records behind it move up by one position, so matcher indexes change, which is why the matcher is rebuilt) and `matcher_dirty_` is set. Then `persist(Change::Remove, removed)` writes the change (CSV: the file without him; SQLite: the row is deleted) when the store is open; if that fails, the copy is inserted again at the same position, so the list (and with it every matcher index) is as before, and the function returns `false`.

**Used by.** `AuthPipeline::remove_person`, `tests/test_match.cpp` (`test_store`, `test_database`).

### FeatureStore::update

```cpp
bool FeatureStore::update(const std::string& id, const std::string& new_name, const Feature& new_feature)
```

**What it does.** Modifies an existing person. An empty `new_name` keeps the old name; an empty `new_feature` (no values) keeps the old feature. Returns `true` on success. Returns `false` and sets `last_error()` when: the new name contains a carriage return or a line feed (`id and name must not contain line breaks`); a non-empty new feature is not a finite vector of length 1 (`the feature must be a finite vector of length 1`); the id is unknown (`unknown id: <id>`); the new feature's length differs from the stored one (`feature length differs from the registered one`); or, when the store is open, the database file cannot be written (`the change was not saved and has been undone: <reason>`). In every failure case nothing is changed, not even the name, and the file is as it was.

**How it works.**
1. Refuse a `new_name` that contains `\r` or `\n` (the same rule as in `add`: the CSV is read line by line). This check runs first, even for an unknown id.
2. If a new feature is given (it has values) and `is_usable_feature` refuses it, fail. An empty feature is not checked, because it means "keep the old one". This check also runs before the id is looked up, so an unknown id together with a bad feature reports the feature error.
3. Find the record with this id (linear search); if there is none, fail with `unknown id`.
4. If a new feature is given and its length differs from the stored one: fail.
5. Keep a copy of the record as it is now. If `new_name` is not empty, replace the name.
6. If a new feature is given, replace the feature and set `matcher_dirty_`.
7. `persist(Change::Update, r)`: when the store is open, the change is written now (CSV: the whole file; SQLite: the row of this id gets its name, card and feature), also for a change of the name alone. If that fails, the record is set back to the copy (old name, old feature) and the function returns `false`; if the feature had been replaced, the matcher flag stays set, so the next `matcher()` rebuilds the table from the restored feature.

A name-only change does not set the flag, because names are not stored in the matcher. Note that "empty" means "keep", so a name cannot be changed to an empty text with this function. `update` never changes the RF card; use `set_card_id` for that.

**Used by.** `AuthPipeline::rename_or_refresh`, `tests/test_match.cpp` (`test_store`, `test_database`).

### FeatureStore::set_card_id

```cpp
bool FeatureStore::set_card_id(const std::string& id, const std::string& card_id)
```

**What it does.** Gives the person `id` the RF card `card_id`, or takes the card away when `card_id` is empty. Returns `true` on success. Returns `false` and sets `last_error()` when: the card number contains a carriage return or a line feed (`id, name and card_id must not contain line breaks`); the id is unknown (`unknown id: <id>`); the card belongs to another person (`card already belongs to <id of the owner>: <card>`); or, when the store is open, the database file cannot be written (`the change was not saved and has been undone: <reason>`; the old card is restored). Setting the card that the person already has is fine and returns `true`. On every failure the card is as it was and so is the file.

**How it works.**
1. Refuse a card number with `\r` or `\n` (this check runs first, even for an unknown id).
2. Find the record with this id (linear search); if there is none, fail with `unknown id`.
3. Unless the new card is empty, `find_by_card` it: if another record owns it, fail.
4. Keep the old card, set the new one, `persist(Change::Update, r)` (writes the change when the store is open: CSV the whole file, SQLite the row; an empty card is written as `NULL` in SQLite). If that fails, put the old card back and return `false`.

The matcher is not affected (it holds features, not cards), so `matcher_dirty_` is not set. Like `add`, the call writes to the database file when the store is open.

**Used by.** `tests/test_match.cpp` (`test_card_first`, `test_database`: a change that cannot be written is undone, for both formats) and `tests/test_pipeline.cpp` (`main`, video 6: gives two registered people a card). `AuthPipeline` does not call it: a card is given at registration (`enroll_track`, `enroll_image`), or later through `AuthPipeline::store()`.

### FeatureStore::find_by_card

```cpp
const Record* FeatureStore::find_by_card(const std::string& card_id) const
```

**What it does.** Returns a pointer to the record of the person who owns the RF card, or `nullptr` if nobody does. It also returns `nullptr` for an empty `card_id`: "no card" is not a card that somebody owns (many records may have an empty one). It does not set `last_error()`.

**How it works.** An empty card number returns `nullptr` at once; otherwise a linear search through `records_` comparing the card numbers, so the time grows with the number of registered people. The pointer points inside the internal vector and becomes invalid after `add`, `remove`, `load` or `open`, like the one from `find`.

**Used by.** `FeatureStore::add` (duplicate card), `FeatureStore::set_card_id`, `FeatureStore::index_of_card`, `AuthPipeline::fill_card_status` (looks the owner of each card of the check up again in every frame), `tests/test_match.cpp` (`test_card_first`, `test_format_choice`), `tests/test_pipeline.cpp` (video 6).

### FeatureStore::index_of_card

```cpp
int FeatureStore::index_of_card(const std::string& card_id) const
```

**What it does.** Returns the position of the card's owner in `records()`, which is also the position (the `index`) in the matcher's table, or -1 if nobody owns the card (also for an empty `card_id`).

**How it works.** `find_by_card`, then the pointer difference to the start of `records_`.

**Used by.** `FeatureStore::match_cards` (the look-up of every card), `tests/test_match.cpp` (`test_card_first`).

### FeatureStore::match_cards

```cpp
bool FeatureStore::match_cards(const std::vector<Feature>& queries, const std::vector<std::string>& card_ids,
                               CardMatch& result) const
```

**What it does.** The **card-first** comparison. The card information is identified FIRST: every card of `card_ids` (N of them) is looked up in the database and the feature stored with it is retrieved. Then the M faces (`queries`, the features of the currently detected faces) are compared with exactly those retrieved features and with nothing else. M and N are independent: fewer faces than cards, as many, or more (the tests play M < N, M = N and M > N). `result` (a `CardMatch`) tells both ways round: for every card whether it was found and which face is most like its owner, and for every face which card's owner it is most like.

Returns `true` only if `card_ids` is not empty and **every** card is in the database. A card that is **not** in the database is a **failed comparison**: it is marked `found = false`, nothing is compared for it, and the call returns `false` (`last_error()` says `a card is not in the database`, or `no card given` for an empty list). The cards that were found are still compared, so the caller can use their results or reject the whole attempt, but **there is never a fallback to the whole database**: a face is not searched among the registered people because a card was unknown. An empty card number is not a card (it is never found). The function does not decide: it is up to the caller to accept a face when its score reaches the threshold.

**How it works.**
1. `result` is reset: no cards, and one default `Query` (`best_card = -1`, `score = kNoScore`) for each of the M faces.
2. **The cards first.** For every card, in order: `index_of_card` gives the position of its owner in `records()`; the card is `found` if that is not -1. A found card adds its position to a list `positions` (N_s entries) and remembers which card it belongs to; every card, found or not, adds its `Card` entry to `result.cards` (so N entries, in the order given).
3. `all_found` is "the list is not empty and every card was found"; if it is not, the error text is set (`no card given` or `a card is not in the database`).
4. If no card was found or there are no faces, return `all_found` (nothing to compare).
5. **The comparison:** `matcher().score_matrix(queries, positions, scores)` computes all M x N_s similarities in one call (the matcher is rebuilt first if the store changed; see `FeatureMatcher::score_matrix`). Then for every face `q` and every found card `k`: an entry that is `kNoScore` (a face of the wrong length) is skipped; if the similarity is higher than the face's best so far, the face's `best_card` and `score` are updated; if it is higher than the card's best so far, the card's `best_query` and `best_score` are updated. A tie goes to the first face or card. The same card given twice is two entries with the same results.
6. Return `all_found`.

An example: five people P0..P5 are registered, P0 to P4 with the cards `CARD-0` to `CARD-4`. `match_cards` with the faces of P2, P0 and P4 (M = 3) and the cards `CARD-0`, `CARD-2`, `CARD-4` (N = 3) gives `queries[0].best_card = 1` (P2's face is most like the owner of the second card), `queries[1].best_card = 0`, `queries[2].best_card = 2`, and `cards[0].best_query = 1`, `cards[1].best_query = 0`, `cards[2].best_query = 2`. With only `CARD-4` and the face of P2 (a registered person with somebody else's card) the call returns `true` and the only entry has a low `score`: the face was compared with P4 only. With `CARD-NOBODY` the call returns `false`, `cards[0].found` is `false`, and `queries[0].best_card` is `-1` and `score` is `-2` although P2 is registered.

**Used by.** `AuthPipeline::match_results` (during a card check, once per frame with new features: the M faces against the cards of the check), `AuthPipeline::begin_card_check` (with no faces: only the look-up of the cards), `tests/test_match.cpp` (`test_card_first`).

### FeatureStore::matcher

```cpp
const FeatureMatcher& FeatureStore::matcher() const
```

**What it does.** Returns the matcher that holds all registered features. Its index `i` refers to `records()[i]`. If the store changed since the last call, the matcher is rebuilt first.

**How it works.**
1. If `matcher_dirty_` is set: build a list of pointers to every record's feature and pass it to `FeatureMatcher::set_gallery`, which copies them into the matcher's padded table; then clear the flag.
2. Return the matcher (a reference to the member).

Things to know. (a) The result of `set_gallery` is ignored. A comment in the code explains why: `set_gallery` can only fail for features of different lengths, which `add()`, `load()` and `open()` already refuse (and `update` refuses a feature of another length too), and on failure the matcher is left empty and matches nothing. (One more failure is possible in `set_gallery`, a failed memory allocation, which that comment does not mention; the effect is the same: an empty matcher, the flag cleared, and every query gets "no match" without any error message.) (b) Always call `matcher()` again after changing the store. A reference kept from before the change refers to the same matcher object, but that object is brought up to date only inside `matcher()`, so matching through an old reference without calling `matcher()` again uses the old table. (c) An empty store gives an empty matcher. Every registered feature is a finite vector of length 1 (within the tolerance of `is_usable_feature`), because `open`, `load`, `add` and `update` all apply that check; so the matcher's dot product is a cosine similarity for the registered side. The matcher never checks the query features. (d) The function is `const` (it only touches `mutable` members) but is not safe to call from two threads at once while a rebuild is needed.

**Used by.** `AuthPipeline::match_results` (`store_.matcher().match(...)`), `FeatureStore::match_cards` (`matcher().score_matrix(...)`), `tools/eval_footage.cpp`, `tools/bench_match_part.h` (`bench_matching`, run by `bench` and `bench_match`), `tests/test_match.cpp` (also after an undone change: `test_database` checks that the matcher knows the restored person again).
## src/store/sqlite_db.h

The SQLite form of the database of registered people: a thin wrapper around the SQLite C library (`third_party/sqlite`, public domain) that stores the same things as the CSV file in the table `people` (see "Background: the SQLite format"). `FeatureStore` is the only user of this class; it adds the lock file, the checks of the features and the undo of failed changes, and the class itself does no checking of the features. File names are passed to SQLite as they are: use ASCII (or UTF-8) names. Macro: `FA_SQLITE_DB_H` is the include guard. The header includes `record.h` and forward-declares `struct sqlite3;`, so it does not include the SQLite header.

| Function | Purpose |
|---|---|
| `SqliteDb` (class) | One open SQLite database file of registered people. |
| `SqliteDb()`, `~SqliteDb()` | Create an object that holds no file; close the file when the object is destroyed. |
| `SqliteDb(const SqliteDb&)` and `operator=` (deleted) | Forbid copying. |
| `SqliteDb::is_open` | Is a file open? |
| `is_sqlite_file` | Does a file start with the SQLite file header? |

Declared here and documented under `sqlite_db.cpp`, where they are defined: `SqliteDb::open`, `close`, `load_all`, `insert`, `erase`, `update`, `replace_all` and the private `exec`.

### SqliteDb (class)

```cpp
class SqliteDb {
public:
    SqliteDb() {}
    ~SqliteDb() { close(); }
    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;

    bool open(const std::string& path, bool create, std::string& error);
    void close();
    bool is_open() const { return db_ != nullptr; }
    bool load_all(std::vector<Record>& records, std::string& error);
    bool insert(const Record& record, std::string& error);
    bool erase(const std::string& id, std::string& error);
    bool update(const Record& record, std::string& error);
    bool replace_all(const std::vector<Record>& records, std::string& error);
private:
    bool exec(const char* sql, std::string& error);
    sqlite3* db_ = nullptr;
};
```

**What it does.** One connection to a SQLite database file that holds the table `people`. The functions return `true` on success; on failure they return `false` and put the reason into the `error` parameter (a text that names the file, the row or the SQLite message). Every function that changes the file is one atomic SQLite transaction: it is done completely or not at all.

**How it works.** The only member is `db_`, the connection handle of the SQLite library (`sqlite3*`, null when no file is open). The destructor calls `close()`. Copying is deleted: two objects would close the same connection. The class is not thread safe (like the store).

**Used by.** `FeatureStore` (member `sqlite_`, and a temporary one in `read_sqlite` and `save`), `tests/test_match.cpp` (`test_format_choice` uses the SQLite C library directly to make damaged and foreign files; `test_database` does so to hold the write lock of the file).

### SqliteDb::is_open

```cpp
bool is_open() const { return db_ != nullptr; }
```

**What it does.** Returns `true` while a file is open (after a successful `open`, until `close`).

**How it works.** A one-line test of `db_`.

**Used by.** No caller in `src/`, `tools/` or `tests/` (the store knows whether its connection exists from `sqlite_` itself).

### is_sqlite_file

```cpp
bool is_sqlite_file(const std::string& path);
```

**What it does.** Returns `true` if the file at `path` starts with the SQLite file header, the 16 bytes `SQLite format 3` followed by a zero byte. Returns `false` for any other content, for a file shorter than 16 bytes and for a file that cannot be opened. It only looks at the first bytes; it does not check that the file is a usable database.

**How it works.** The file is opened in binary mode, 16 bytes are read, and they are compared with `"SQLite format 3"` including its terminating zero (15 characters and the zero).

**Used by.** `FeatureStore::resolve_format` (to recognise an existing file by its content), `tools/db_convert.cpp` (`main`: says whether the input and the output are SQLite), `tests/test_match.cpp` (`test_format_choice`).

## src/store/sqlite_db.cpp

The implementation. It includes `sqlite3.h` (from `third_party/sqlite`), `<cstring>` and `<fstream>`. In an anonymous namespace (private to this file) it has two constants, a small class and three helper functions.

| Function | Purpose |
|---|---|
| `kLayoutVersion`, `kBusyTimeoutMs`, `kCreateTable` (constants) | The layout version of the table, how long a change waits for another connection, the `CREATE TABLE` text. |
| `Statement` (class) | A prepared SQL statement that is finalized when it goes out of scope. |
| `failure` | Build an error text from what was being done and the SQLite message. |
| `text_column` | Read a text column of the current row as a `std::string` (NULL becomes empty). |
| `bind_record` | Bind the five fields of a record to the parameters of an `INSERT` or `UPDATE`. |
| `is_sqlite_file` | Does a file start with the SQLite file header? (documented above) |
| `SqliteDb::exec` | Run one or more SQL statements without a result (private). |
| `SqliteDb::open` | Open (or create) the database file and check its table layout. |
| `SqliteDb::close` | Close the file. |
| `SqliteDb::load_all` | Read every row, in insertion order. |
| `SqliteDb::insert` | Add one row. |
| `SqliteDb::erase` | Delete one row. |
| `SqliteDb::update` | Change one row. |
| `SqliteDb::replace_all` | Make the table hold exactly a given list of records (one transaction). |

### kLayoutVersion, kBusyTimeoutMs, kCreateTable

```cpp
const int kLayoutVersion = 1;
const int kBusyTimeoutMs = 500;
const char* const kCreateTable = "CREATE TABLE people ( id TEXT NOT NULL PRIMARY KEY, name TEXT NOT NULL, card_id TEXT UNIQUE, dim INTEGER NOT NULL CHECK (dim > 0), feature BLOB NOT NULL CHECK (length(feature) = dim * 4));";
```

**What it does.** `kLayoutVersion` is the version of the table layout, stored in the file as `PRAGMA user_version`: a file with a higher number was written by a newer program and is refused. `kBusyTimeoutMs` is how long (500 ms) a change waits for another connection that holds the lock of the file before it fails (`sqlite3_busy_timeout`). `kCreateTable` is the table, as described under "Background: the SQLite format".

**How it works.** Constants in the anonymous namespace.

**Used by.** `SqliteDb::open` (all three).

### Statement (class)

```cpp
class Statement {
public:
    Statement(sqlite3* db, const char* sql);
    ~Statement();
    sqlite3_stmt* get() const;
};
```

**What it does.** A prepared SQL statement (the SQLite library parses the text once; parameters `?1`, `?2` ... are filled in with `bind_record` or `sqlite3_bind_text`). It cleans up after itself: when the object goes out of scope the statement is finalized, so every way out of a function releases it. `get()` returns the statement handle, or null if the SQL text could not be prepared (the callers test for that and report the SQLite message). Copying is deleted.

**How it works.** The constructor calls `sqlite3_prepare_v2` and keeps the handle (or null when that did not return `SQLITE_OK`); the destructor calls `sqlite3_finalize`.

**Used by.** `SqliteDb::open` (the two look-ups of the layout), `load_all`, `insert`, `erase`, `update` and `replace_all`.

### failure

```cpp
std::string failure(sqlite3* db, const std::string& what)
```

**What it does.** Returns `<what>: <the SQLite message of db>`, for example `cannot insert 'E001': UNIQUE constraint failed: people.id`. When `db` is null the message is `no database`.

**How it works.** `what + ": " + sqlite3_errmsg(db)`.

**Used by.** `SqliteDb::open`, `load_all`, `insert`, `erase`, `update` and `replace_all` (every error text that comes from the SQLite library).

### text_column

```cpp
std::string text_column(sqlite3_stmt* s, int column)
```

**What it does.** Returns the text of column `column` of the current result row. A `NULL` value (a person with no card) gives an empty string.

**How it works.** `sqlite3_column_text` returns a null pointer for `NULL`; otherwise the text is copied with its length (`sqlite3_column_bytes`), so a text with a zero byte inside would not be cut short.

**Used by.** `SqliteDb::load_all` (id, name and card).

### bind_record

```cpp
bool bind_record(sqlite3_stmt* s, const Record& r)
```

**What it does.** Fills the five parameters of an `INSERT` or `UPDATE` statement from a record: `?1` the id, `?2` the name, `?3` the card, `?4` the length of the feature, `?5` the feature as a blob. An empty card is bound as `NULL` (several people may have no card; a real card number is unique). Returns `true` if all five were bound.

**How it works.** `sqlite3_bind_text` for id and name (copied by SQLite: `SQLITE_TRANSIENT`, so the record may go away afterwards), `sqlite3_bind_null` or `sqlite3_bind_text` for the card, `sqlite3_bind_int` for `dim` and `sqlite3_bind_blob` for the numbers (`dim * 4` bytes, the floats as they are in memory). The bindings stop at the first failure.

**Used by.** `SqliteDb::insert`, `update` and `replace_all`.

### SqliteDb::exec

```cpp
bool SqliteDb::exec(const char* sql, std::string& error)
```

**What it does.** Private. Runs one or more SQL statements that return no rows (`BEGIN`, `COMMIT`, `ROLLBACK`, `DELETE FROM people`, the table creation). Returns `true` on success; on failure `false` with the SQLite message in `error`.

**How it works.** `sqlite3_exec` without a callback; the message that SQLite allocates is copied into `error` and released with `sqlite3_free`.

**Used by.** `SqliteDb::open` (create the table) and `replace_all`.

### SqliteDb::open

```cpp
bool SqliteDb::open(const std::string& path, bool create, std::string& error)
```

**What it does.** Opens the database file `path`. With `create` a missing file is created and, if it is a new (empty) database, given the table `people` and the layout version; without it the file must exist and must already hold this table. Returns `true` on success. Returns `false` and puts the reason into `error` when: the file cannot be opened (`cannot open the SQLite database <path>: ...`, for example a missing folder, or a missing file without `create`); the file is not a SQLite database at all (`cannot read <path> as a SQLite database: ...`); the layout version is newer than `kLayoutVersion` (`<path> has table layout N, newer than this program understands (1)`); the table cannot be created (`cannot create the table in <path>: ...`); or the file is a SQLite database but not one of ours (`<path> is not a face_auth database (no table 'people' of layout 1)`). A previously open file is closed first, and on every failure no file is left open.

**How it works.**
1. `close()`, then `sqlite3_open_v2(path, READWRITE | CREATE if asked)`. On failure the handle is closed and the error is returned. Then `sqlite3_busy_timeout(kBusyTimeoutMs)`.
2. `PRAGMA user_version` is read. This is the first real access to the file, so a file that is not a SQLite database fails here.
3. `SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'people'` says whether the table exists.
4. A version above `kLayoutVersion`: refuse.
5. **A new empty database** (version 0, no table, and `create` asked for): one script, `BEGIN; CREATE TABLE ...; PRAGMA user_version = 1; COMMIT;`. If it fails the transaction is rolled back and the file is closed. Return `true` otherwise.
6. Any other file must have exactly version 1 and the table `people`, otherwise it is refused as `not a face_auth database` (this also refuses an existing empty database when `create` is false, and a file of another application). Return `true`.

**Used by.** `FeatureStore::read_sqlite` (to read a database) and `FeatureStore::save` (to make the temporary file: `create = true`).

### SqliteDb::close

```cpp
void SqliteDb::close()
```

**What it does.** Closes the file. Calling it when no file is open does nothing.

**How it works.** `sqlite3_close(db_)` and `db_ = nullptr`. SQLite writes nothing more at this point (every change was committed when it was made) and removes its journal file.

**Used by.** `~SqliteDb`, `SqliteDb::open` (a previous file, and every failed opening), `FeatureStore::save` (explicitly, before a failed temporary file is removed).

### SqliteDb::load_all

```cpp
bool SqliteDb::load_all(std::vector<Record>& records, std::string& error)
```

**What it does.** Reads every row of the table, in the order in which the rows were inserted, into `records` (which is emptied first). Returns `true` on success. Returns `false` and puts the reason into `error` when no file is open (`the SQLite database is not open`), the table cannot be read (`cannot read the table people: ...`), or a row is damaged: its `dim` is not positive, it has no blob, or the blob is not `dim * 4` bytes long (`row '<id>': the feature does not have <dim> numbers`); `records` is then empty. It does not check that a feature has length 1 or that all features have the same length (`FeatureStore` does, with `check_loaded_records`).

**How it works.** `SELECT id, name, card_id, dim, feature FROM people ORDER BY rowid`, stepped row by row: `text_column` for the three texts, `sqlite3_column_int` for `dim`, `sqlite3_column_blob` and `sqlite3_column_bytes` for the numbers, which are copied into a `Feature` with `memcpy`. The order of the rows is the order of `FeatureStore::records()`.

**Used by.** `FeatureStore::read_sqlite`.

### SqliteDb::insert

```cpp
bool SqliteDb::insert(const Record& record, std::string& error)
```

**What it does.** Adds one person as a new row. Returns `true` on success. Returns `false` and puts the reason into `error` when no file is open or the database refuses the row (`cannot insert '<id>': ...`: an id or a card number that exists already, or a file that another connection holds locked for longer than 500 ms).

**How it works.** `INSERT INTO people (id, name, card_id, dim, feature) VALUES (?1, ?2, ?3, ?4, ?5)` with `bind_record`. SQLite runs it as its own transaction (automatic commit), so the row is in the file, crash-safe, when the call returns.

**Used by.** `FeatureStore::persist` (`Change::Add`).

### SqliteDb::erase

```cpp
bool SqliteDb::erase(const std::string& id, std::string& error)
```

**What it does.** Deletes the row of the person with this id. Returns `true` on success. Returns `false` and puts the reason into `error` when no file is open, the statement fails (`cannot delete '<id>': ...`) or there is no such row (`unknown id in the database file: <id>`).

**How it works.** `DELETE FROM people WHERE id = ?1`; afterwards `sqlite3_changes` must be 1. The other rows keep their order.

**Used by.** `FeatureStore::persist` (`Change::Remove`).

### SqliteDb::update

```cpp
bool SqliteDb::update(const Record& record, std::string& error)
```

**What it does.** Changes the row of the person with `record.id`: the name, the card, the length and the feature become those of `record` (the id stays). Returns `true` on success. Returns `false` and puts the reason into `error` when no file is open, the statement fails (`cannot change '<id>': ...`, for example a card that another row owns) or there is no such row (`unknown id in the database file: <id>`).

**How it works.** `UPDATE people SET name = ?2, card_id = ?3, dim = ?4, feature = ?5 WHERE id = ?1` with `bind_record` (all five parameters are bound; `?1` is used in the `WHERE`); afterwards `sqlite3_changes` must be 1. The row keeps its place in the order of the rows.

**Used by.** `FeatureStore::persist` (`Change::Update`: a new name, a new feature, a new or removed card).

### SqliteDb::replace_all

```cpp
bool SqliteDb::replace_all(const std::vector<Record>& records, std::string& error)
```

**What it does.** Makes the table hold exactly `records`, in this order, in **one transaction: all or nothing**. It is used to write a new file. Returns `true` on success. Returns `false` and puts the reason into `error` when no file is open or any step fails (`cannot insert '<id>': ...`, `cannot prepare the insert`, or the message of a `BEGIN` or `COMMIT` that failed); the transaction is rolled back then, so the table is as it was.

**How it works.** `BEGIN IMMEDIATE` (takes the write lock at once), `DELETE FROM people`, then one prepared `INSERT` that is reset (`sqlite3_reset`) and filled with `bind_record` for every record, then `COMMIT`. If anything failed, `ROLLBACK`. One transaction for all rows is much faster than one per row (writing 10,000 people took 97 ms in `test_database_speed`).

**Used by.** `FeatureStore::save` (format SQLite).

## third_party/sqlite

Not our code: the **SQLite amalgamation**, version 3.54.1 (`sqlite3.c` and `sqlite3.h`), public domain. It was downloaded from sqlite.org (`sqlite-amalgamation-3540100.zip`) and checked against the SHA3-256 value published on sqlite.org/download.html (`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`). It is compiled as plain C (not as C++) by `build.bat` (once, into `build\obj\sqlite3.obj`; delete that file to compile it again) and by the Visual Studio library project `fa_sqlite`, with `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_THREADSAFE=1`, `SQLITE_DEFAULT_MEMSTATUS=0` and `SQLITE_OMIT_SHARED_CACHE`. It compiles with the real Visual Studio 2015 C compiler. Only `sqlite_db.cpp` (and the tests, which use the C library directly to make damaged and foreign files) include `sqlite3.h`. The licence and source are listed in `MODELS.md`.

CSV numeric fields must parse completely: dimensions must be positive integers within the C++ int range; feature values must be finite and representable as floats. Empty numbers, suffixes, overflow and underflow to zero are rejected. IDs must be nonempty, and IDs, names and cards cannot contain line breaks. Rejected loads/opens preserve the existing records, binding and revision. Legacy CSV, quoted fields, CRLF and whitespace around numeric fields remain supported.

CSV quotes must enclose a whole field; doubled quotes inside quoted fields remain supported. Text following a closing quote and quotes inside unquoted fields are rejected. A current-format header must match all five fields (`id,name,card_id,dim,values`). CRLF record endings, valid quoted fields and headerless legacy records remain supported; malformed input does not replace the current records or binding.
