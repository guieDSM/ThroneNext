# Local patches to the vendored fkYAML

`node.hpp` is the upstream [fkYAML](https://github.com/fktn-k/fkYAML) v0.4.2
single-header amalgamation **plus** the local fixes listed below. Every one of
them is marked in the source with a `THRONE-PATCH` comment.

**Re-apply these when bumping fkYAML.** Upstream had not fixed any of them as of
v0.4.3 — see [fktn-k/fkYAML#536](https://github.com/fktn-k/fkYAML/issues/536).
Grep for `THRONE-PATCH` in the new amalgamation's neighbourhood and re-check each
site before dropping a patch as "fixed upstream".

## Why they matter here

Clash subscription bodies are attacker-controlled: `RawUpdater::updateClash`
([src/configs/sub/GroupUpdater.cpp](../../src/configs/sub/GroupUpdater.cpp))
hands whatever a subscription URL returned straight to
`fkyaml::node::deserialize`. Each bug below is an out-of-bounds access, so
`catch (const fkyaml::exception&)` does not contain it — the process just dies.
Reported as [Throne#1746](https://github.com/throneproj/Throne/issues/1746).

## The patches

| # | Site | Bug | Fix |
|---|------|-----|-----|
| 1 | `basic_deserializer::m_context_stack` | ~40 call sites reach `back()`/`pop_back()` without testing `empty()`; malformed input empties the stack (SEGV) | declared as a `context_stack_type` wrapper whose `back()`/`pop_back()` throw `parse_error` when empty |
| 2 | `iterator_input_adapter<char>::get_buffer_view_utf8` | trailing bytes of a multibyte sequence read via `*++current` with no `m_end` test → OOB read on a truncated tail | bounds-checked `next_byte()`, throws `invalid_encoding` |
| 3 | `iterator_input_adapter<char>::get_buffer_view_utf16` | low byte read via `*++current` with no `m_end` test → OOB read when the length is odd, then a past-the-end `++` | end test before the second byte |
| 4 | `iterator_input_adapter<char>::get_buffer_view_utf32` | four bytes consumed per one `m_end` test → OOB read when the length is not a multiple of 4; also shifted `char` (signed) left, UB for bytes ≥ 0x80 | per-byte end test, and shift as `uint8_t`→`uint32_t` |
| 5 | `iterator_input_adapter<char8_t>::get_buffer_view` | same as #2 | same as #2 |
| 6 | `file_input_adapter` / `stream_input_adapter` `::get_buffer_view_utf8` | same as #2, via `*current++` | same as #2 |

Patches 5 and 6 cover code paths Throne never reaches today (it only ever
deserializes a `std::string`); they are fixed so the defect does not come back
if a call site changes.

## Verifying

The out-of-bounds reads are silent in an ordinary build. To observe them, place
the payload flush against an unmapped guard page (`VirtualAlloc` two pages,
commit only the first) and call `fkyaml::node::deserialize(begin, end)`. Known
crashing inputs, all of which now raise a catchable exception:

| Payload (hex / text) | Original v0.4.2 |
|---|---|
| `01 00 0a` | SEGV in `get_buffer_view_utf16` |
| `01 00` + `proxies: []` | SEGV in `get_buffer_view_utf16` |
| `a: ` + `e3 81` | SEGV in `get_buffer_view_utf8` |
| `01 00 00 00 41` | SEGV in `get_buffer_view_utf32` |
| `a\n: b` | SEGV, `deque::back()` on an empty container |
| `[a]\nb: 1` | SEGV, `deque::back()` on an empty container |

The last two are plain ASCII and need no guard page. A complete subscription
body that crashes an unpatched client is 14 bytes — the comment is only there so
Throne's format detector routes it to the Clash parser:

```yaml
# proxies:
a
: b
```
