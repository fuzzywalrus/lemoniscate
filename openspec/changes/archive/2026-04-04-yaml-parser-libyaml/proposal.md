## Why

The `tn_load()` function in `threaded_news_yaml.c` used a libyaml event-based parser that was deployed but had two classes of bugs:

### 1. Invalid UTF-8 caused total data loss (root cause of empty bodies)

libyaml rejects the **entire** YAML file on any invalid UTF-8 byte, returning a parse error at the very first event. `tn_load()` silently returned success with zero categories — no articles loaded at all.

The VPS ThreadedNews.yaml (written by Mobius Go) contained smart quotes (`'`, U+2019) in article 3. If these bytes were mojibake or otherwise invalid UTF-8, libyaml would reject the entire file, and Navigator would show articles with empty bodies (the server had an empty default "General" category in memory).

**Root cause confirmed** by test: feeding invalid UTF-8 bytes to the old parser → `category_count=0`, all articles lost.

### 2. Save/load round-trip corruption

Three bugs in `tn_save()` / `tn_load()` prevented articles from surviving a save→reload cycle:

1. **`\r` not escaped** — `yaml_write_escaped()` wrote raw `\r` bytes (Hotline wire format newlines) to YAML. A bare `\r` is a YAML line break, corrupting the file on re-read.
2. **`Date` not written** — timestamps were lost after save→reload.
3. **`ParentID` not parsed** — `tn_save()` wrote `ParentID: N` as a scalar, but `tn_load()` only handled `ParentArt` as a sequence. Threading was lost after save→reload.

## What Changed

- `tn_load()` now reads the file into memory and sanitizes invalid UTF-8 bytes (replacing with `?`) before feeding to libyaml. Invalid characters degrade gracefully instead of causing total data loss.
- `yaml_write_escaped()` now converts `\r` to `\n` when writing YAML.
- `tn_save()` now writes `Date` as a flow sequence and `ParentArt` as a sequence (matching Mobius format).
- `tn_load()` now handles `ParentID` as a scalar value (Lemoniscate's own save format).
- Duplicate comment block removed.

## Testing

16-test suite in `test/test_threaded_news.c` covering:
- Exact Mobius Go VPS format (4-space indent, flow sequences, block scalars with blank lines, quoted strings with `\n`, UTF-8 smart quotes)
- Older Mobius format with DataFlav byte sequence
- Invalid UTF-8 resilience (bad bytes → `?`, articles still load)
- Lemoniscate save format (`Body` key, `ParentID` scalar)
- Full save/load round-trip with `\r` newline preservation

## Impact

- **File**: `src/mobius/threaded_news_yaml.c` — `tn_load()`, `tn_save()`, `yaml_write_escaped()` modified
- **New file**: `test/test_threaded_news.c` — 16 tests
- **Makefile**: `test-threaded-news` target added
- **Dependencies**: No new dependencies (libyaml already linked)
- **Risk**: Low — changes are additive (UTF-8 sanitizer) or fix incorrect behavior (save format)
- **Deployed**: 2026-04-04, verified on VPS — article bodies now display correctly in Navigator

## Capabilities

### Modified Capabilities
- `threaded-news`: YAML loading is now resilient to encoding issues. Save format preserves timestamps and threading across restarts.

### New Capabilities
- None
