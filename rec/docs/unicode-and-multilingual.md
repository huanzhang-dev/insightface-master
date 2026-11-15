# Unicode text and multilingual OCR

The application stores recognized text as UTF-8. The display uses UTF-16 Windows GDI text APIs with installed-font fallback. `Characters` means all non-number, non-whitespace code points, including scripts beyond English. `Numbers` uses Unicode 17.0.0 General_Category N*; values are preserved rather than converted to ASCII. Unicode data is generated into `include/UnicodeNumbers.inc` from the recorded `third_party/UnicodeData.txt`; see `third_party/unicode_LICENSE`.

## OCR selection

`--ocr-languages eng+chi_sim+kor+jpn` is the default. Installed models are `eng`, `chi_sim`, `kor`, `jpn`, `ara`, and `rus`. For a known script prefer its specific model: `--ocr-languages chi_sim`, `--ocr-languages kor`, or `--ocr-languages rus`. The Chinese/Korean/Japanese models also include Latin examples. The order and combination of models affect accuracy; enabling every language does not guarantee better results. To add a language, install the corresponding official Tesseract 4/5 LSTM `.traineddata` under `models/tessdata` and select its language code. No download happens during recognition.

`--ocr-languages latin` explicitly selects the original CRNN plus synthetic glyph classifier. That backend alone remains A-Z/0-9. `--numbers-only` rejects whole mixed readings. `Recognizer` documents this optional Latin backend; normal `Pipeline::detect` uses `MultilingualOcr` unless `latin` was selected. Vehicle/plate detection still uses the existing ONNX networks. Tesseract runs on CPU, including when detector inference uses DirectML.

Unicode storage does not mean every script can be recognized without a matching model, suitable installed fonts and clear plate images. Arbitrary-script, handwritten, tiny, blurred, or multiline plate accuracy is not established. The four-to-twelve-code-point length gate, at-least-one-number gate, confidence filtering, and temporal consensus still apply. The Chinese synthetic fixture scores 0.53; three independent matching, region-supported observations can now establish consensus. This is not proof of real-camera accuracy.

## Exports

`tracks.csv`: `frame,time_s,track_id,vehicle_type,plate,characters,numbers,plate_confidence,vehicle_confidence,stable,plate_status,plate_source,observed_time_s,last_read_text,vehicle_observed_time_s`. The `characters` column replaces the earlier `letters` column. Plate/character/number fields are quoted UTF-8 CSV. `recognition.jsonl` has `plate`, `character_text`, `numbers`, `confidence`, `symbol_confidence_source`, and a `characters` array of `{value,kind,confidence}`. Each array value is a complete UTF-8 code point. `kind` is `number` or `character`. Tesseract repeats line confidence for symbols and identifies the source as `line_mean`; the Latin backend uses `glyph_or_ctc`. Combining marks are separate array entries, not grapheme clusters.

`letters(text)` remains a source-compatibility alias for `characters(text)` and is no longer English-only. Existing code using that helper receives the new Unicode semantics.

## Function reference

### `car::wideText`

```cpp
std::wstring wideText(const std::string& text)
```

Source: [src/Text.cpp](../src/Text.cpp). Validates UTF-8 and converts it to Windows UTF-16. Input is UTF-8, output owns its storage. Empty input returns empty; malformed input throws runtime_error. Uses MultiByteToWideChar with MB_ERR_INVALID_CHARS.

### `car::numericCodepoint`

```cpp
static bool numericCodepoint(unsigned cp)
```

Source: [src/Text.cpp](../src/Text.cpp). Binary-searches generated Unicode 17.0.0 General_Category N* ranges. Input is a Unicode scalar; output identifies decimal, letter-number, or other-number characters. No allocation or state changes.

### `car::whitespaceCodepoint`

```cpp
static bool whitespaceCodepoint(unsigned cp)
```

Source: [src/Text.cpp](../src/Text.cpp). Recognizes Unicode White_Space code points. Input is a scalar; returns bool without modifying state. This is not a general control-character filter.

### `car::textSymbols`

```cpp
std::vector<TextSymbol> textSymbols(const std::string& text)
```

Source: [src/Text.cpp](../src/Text.cpp). Validates UTF-8, walks complete code points, omits whitespace, and emits owned UTF-8 values plus numeric-category flags in logical source order. Throws on malformed UTF-8. Combining marks remain separate code points; this is not grapheme segmentation or visual bidi reordering.

### `car::characters`

```cpp
std::string characters(const std::string& text)
```

Source: [src/Text.cpp](../src/Text.cpp). Returns all non-whitespace, non-numeric Unicode code points, preserving spelling and order. Includes non-English letters, combining marks, symbols and punctuation. Input is UTF-8; invalid input throws. No transliteration or case conversion.

### `car::numbers`

```cpp
std::string numbers(const std::string& text)
```

Source: [src/Text.cpp](../src/Text.cpp). Returns Unicode N* characters in source order. Preserves Arabic-Indic/fullwidth and supplementary-plane digits, rather than converting them to ASCII. Invalid UTF-8 throws. Chinese ideographs such as the word for three are letters by Unicode category, not parsed number words.

### `car::compactText`

```cpp
std::string compactText(const std::string& text)
```

Source: [src/Text.cpp](../src/Text.cpp). Removes Unicode whitespace from OCR output, preserving all other code points. Returns owned UTF-8 and throws on invalid input. Does not normalize Unicode or remove punctuation.

### `car::textLength`

```cpp
size_t textLength(const std::string& text)
```

Source: [src/Text.cpp](../src/Text.cpp). Returns non-whitespace Unicode scalar count via textSymbols. Input is UTF-8; malformed input throws. Used for plate length gates, which remain 4 to 12 code points.

### `car::jsonText`

```cpp
std::string jsonText(const std::string& text)
```

Source: [src/Text.cpp](../src/Text.cpp). Returns a complete quoted JSON string: escapes quotes, backslashes and ASCII controls; preserves valid UTF-8. Invalid UTF-8 throws. Caller must not add another pair of quotes.

### `car::csvText`

```cpp
std::string csvText(const std::string& text)
```

Source: [src/Text.cpp](../src/Text.cpp). Returns a complete quoted CSV field, doubling embedded quotes. Handles commas and line breaks by quoting, preserves UTF-8, and rejects malformed input.

### `car::MultilingualOcr::Impl::Impl`

```cpp
Impl(const std::string& data,const std::string& languages,bool numeric)
```

Source: [src/Multilingual.cpp](../src/Multilingual.cpp). Loads native x64 Leptonica and Tesseract DLLs from the executable directory, resolves the pinned C ABI, creates an engine and initializes requested language models. Sets page segmentation mode 7 (one line). Arguments are tessdata directory, language expression and numeric-only flag. Missing DLL/API/model throws; partial initialization is cleaned up. No English-only whitelist is set.

### `car::MultilingualOcr::Impl::~Impl`

```cpp
~Impl()
```

Source: [src/Multilingual.cpp](../src/Multilingual.cpp). Deletes the owned Tesseract engine, then unloads Tesseract and Leptonica modules. No arguments or output; invoked during destruction. Must not race an active read.

### `car::MultilingualOcr::MultilingualOcr`

```cpp
MultilingualOcr(const std::string& data,const std::string& languages,bool numbersOnly)
```

Source: [src/Multilingual.cpp](../src/Multilingual.cpp). Constructs the owned native implementation from tessdata path, language expression and whole-reading numeric restriction. Delegates failures to Impl. One instance belongs to a pipeline and is used by one inference thread.

### `car::MultilingualOcr::~MultilingualOcr`

```cpp
~MultilingualOcr()
```

Source: [src/Multilingual.cpp](../src/Multilingual.cpp). Releases the owned implementation and its engine/modules. No input, result, or explicit inference work.

### `car::MultilingualOcr::read`

```cpp
Reading read(const cv::Mat& patch)
```

Source: [src/Multilingual.cpp](../src/Multilingual.cpp). Accepts a nonempty 8-bit BGR or grayscale plate crop. Converts to gray, enlarges to at least 64 pixels high, adds an eight-pixel white border, and runs line OCR. Copies UTF-8 output, frees Tesseract text, removes whitespace, and returns text/line confidence. Empty patches or numeric-only violations return empty Reading. Each emitted scalar receives the line mean confidence, explicitly marked line_mean in JSON; these are not individual symbol scores. Calls mutate the engine and must be serialized; OpenCV/UTF-8 failures propagate.

### `car::Pipeline::~Pipeline`

```cpp
~Pipeline()
```

Source: [src/Car.cpp](../src/Car.cpp). Destroys the optional multilingual engine and model members through their owners. Out-of-line definition permits the forward-declared OCR implementation in the public header. No arguments or result.

## External APIs added

- `MultiByteToWideChar`: strict UTF-8 validation/conversion, then Windows UTF-16 rendering.
- `CreateFontW`, `DrawTextW`: replace the ANSI variants; draw Unicode text into a GDI DIB consumed by OpenCV. Available glyphs depend on installed fonts and fallback.
- `GetModuleFileNameW`, `LoadLibraryW`, `GetProcAddress`, `FreeLibrary`: resolve the native OCR C entry points from application-local DLLs; module lifetimes are owned by Impl.
- `TessBaseAPICreate`, `TessBaseAPIInit3`, `TessBaseAPISetPageSegMode`, `TessBaseAPISetImage`, `TessBaseAPIGetUTF8Text`, `TessBaseAPIMeanTextConf`, `TessDeleteText`, `TessBaseAPIDelete`: respectively allocate, initialize models, select line segmentation, supply pixels, obtain allocated UTF-8, read line confidence, free returned text, and release the engine. Signatures are checked against the pinned upstream `third_party/tesseract_capi.h`. No managed/.NET wrapper is called.

References: [Tesseract language data](https://tesseract-ocr.github.io/tessdoc/Data-Files), [native package source](https://github.com/charlesw/tesseract), [C API](https://github.com/tesseract-ocr/tesseract/blob/5.2.0/include/tesseract/capi.h).

## Priority script verification

Chinese, Korean, Japanese and Latin are the priority scripts. `runTests` verifies exact OCR text for Chinese `京A12345`, Korean `12가3456`, Japanese `品川1234`, Latin `AB1234` and numeric-only `789012`. Korean uses the fast `kor` model; Japanese now uses `tessdata_best/jpn.traineddata`: the fast Japanese model misread the first two characters as `有川`, whereas the higher-accuracy model reads the same image correctly. This replacement can increase CPU inference time. Both model source and checksum are recorded in `models/manifest.json`. `jpn_fast.traineddata` preserves the earlier local model for comparison and is not selected by default.

Printed fixture confidence: Chinese 0.53, Korean 0.91, Japanese 0.83. These are model scores on clean, synthetic text, not measured real-camera accuracy. Existing Latin fixtures and the Cyrillic regression fixture remain in the suite. Fixture images are `testdata/plate_unicode.png`, `testdata/plate_kor.png`, and `testdata/plate_jpn.png`. Language-specific OCR remains preferable to a broad combination when the expected script is known.

Plate OCR output additionally includes evidence_source, evidence_image, observation_time_s and plate_box. Current/history semantics and saved raw crops are defined in [evidence reference](evidence-and-tracking.md).
