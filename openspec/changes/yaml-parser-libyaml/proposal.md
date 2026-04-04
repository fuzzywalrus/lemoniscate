## Why

The `tn_load()` function in `threaded_news_yaml.c` used a hand-rolled line-by-line YAML parser with `strtok()` that broke on Mobius-format YAML files. Specifically:

1. **Block scalars** (`Data: |-\n    multiline text`) — the `strtok` + `goto` approach corrupted parser state, causing articles to show blank or timeout in clients
2. **Quoted strings with escapes** (`Data: "text with \n"`) — `\n` wasn't unescaped
3. **4-space indentation** (Mobius format) vs 2-space (our format) — article detection failed
4. **Multiple articles per category** — parser got stuck on first article

A libyaml event-based parser was written and deployed to the VPS in this session. It builds and the server starts, but the article body rendering needs verification — the session ran out before confirming all 4 articles display correctly in Navigator.

**Current state (deployed to VPS):** The libyaml `tn_load()` is in place. It compiles and the server runs. The previous hand-rolled parser is gone. Needs testing to confirm:
- All 4 articles in "General Discussion" show title, poster, date, AND body text
- Block scalar articles ("This is Cool", "Test out markdown") render body correctly  
- Quoted string article ("Server + client?") renders with proper newlines
- Plain string article ("Success!") works as before

## What Changes

- `tn_load()` in `threaded_news_yaml.c` was replaced with a libyaml event-based parser that handles all YAML scalar types (plain, quoted, block `|-` and `|`)
- `\n` in parsed values is converted to `\r` for Hotline wire format
- Sequences (`Date: [...]`, `Type: [...]`, `ParentArt: [...]`) are parsed via libyaml sequence events
- `#include <yaml.h>` added (libyaml is already linked)

## Capabilities

### Modified Capabilities
- `threaded-news`: The YAML loading is now robust against all YAML scalar types. No behavior change for clients — same wire format output, just correct parsing of the input.

### New Capabilities

## Impact

- **File**: `src/mobius/threaded_news_yaml.c` — `tn_load()` rewritten
- **Dependencies**: Uses libyaml (already linked for config_loader.c)
- **Risk**: If the libyaml parser has issues, threaded news won't load. Rollback: revert to the previous commit.
- **Testing needed**: Connect with Hotline Navigator, verify all articles load with body text. If any show blank or timeout, the depth/key tracking in the parser needs adjustment.
