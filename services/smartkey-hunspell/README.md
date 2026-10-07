smartkey-hunspell
=================

`com.palm.smartKey`, answered from **libhunspell** — the spelling and
word-completion service behind the keyboard's candidate bar and the browser's
text fields.

This is **ours, not HP's**. HP's smartKey shipped only on the device and was
never released as source; nothing in the CE drop provides the name. hunspell is
the open-source checker HP itself used, taken here from the distribution
(`libhunspell-dev`) with its dictionaries read from the host — nothing
proprietary ships. Ticket #34.

Who is listening
----------------

| Caller | Method | Request | Reply it reads |
|---|---|---|---|
| keyboard-efigs `CandidateBarRemote.cpp` | `processTaps` | `{taps:[x,y,key,shift,…]}` or `{trace:[…],first,last,shift}` | `{traceEntry, spelledCorrectly, guesses:[{str, sp, "auto-accept"}]}` |
| BrowserServer `BrowserPage.cpp` | `search` | `{query}` | `{returnValue, match}` |
| BrowserServer `BrowserPage.cpp` | `learn` | `{word}` | fire-and-forget |

These three are the only methods any client in the tree calls — measured across
both the active tree and HP's reference (`reference/luna-sysmgr-ce`), not
assumed. There is no caller for user-word-list management, locale changes over
the bus, or completion subscriptions, so none is implemented.

The reply the keyboard reads
---------------------------

`CandidateBarRemote` reads the reply field by field and **discards the whole
thing if `traceEntry` is missing**, so every reply carries it. The guesses are
ordered the way the candidate bar expects:

- `guesses[0]` is the word as typed;
- `guesses[1…]` are hunspell's corrections, best first;
- a misspelling marks its top correction `"auto-accept":true` (literal hyphen),
  so the composing line shows the fix; a correctly spelled word is never
  autocorrected.

That ordering is why a misspelling must produce at least two guesses: the
keyboard falls back to `guesses[1]` when nothing is auto-accepted and the typed
word is wrong.

Dictionaries and locale
-----------------------

The `.dic`/`.aff` pairs are read from the host (`/usr/share/hunspell`), chosen
by locale:

- the locale comes from `SMARTKEY_LOCALE`, else `LANG`, normalised to a stem
  (`es-MX.UTF-8` → `es_MX`);
- an unknown or missing dictionary falls back to `en_US`, which is always
  installed (`hunspell-en-us`) — the same rule the date/time pickers follow
  (#19): an unknown locale shows en_US, not a broken feature;
- Spanish needs `hunspell-es` on the host (its regional aliases `es_MX`,
  `es_AR`, … all resolve to `es_ES`).

A per-user word list, taught through `learn`, is kept at
`/var/luna/preferences/com.palm.smartKey.userwords` and loaded as an extra
dictionary at startup, so a word the user taught survives a restart.

Runtime dependencies (host): `libhunspell`, and the dictionaries
`hunspell-en-us` (required) and `hunspell-es` (for Spanish).

Layout
------

- `src/smartkey_contract.h` — the request decoding and reply shaping, free of
  the bus and of hunspell, so `tests/smartkey-contract.cpp` pins the exact
  payloads without ls-hubd and without a dictionary. The dictionary is behind
  `SmartKey::Engine`, which a test fills with a fixed word list.
- `src/hunspell_engine.{h,cpp}` — `SmartKey::Engine` on libhunspell, using only
  the 1.7 API (`spell`, `suggest`, `add`), no deprecated `char**` overloads.
- `src/main.cpp` — the bus registration, the JSON parsing of each request, and
  the method table.

Testing it
----------

```sh
ctest --test-dir build/tests -R smartkey-contract --output-on-failure
```

Verified by mutation: the tests break the fix (e.g. dropping `traceEntry`,
autocorrecting a correctly spelled word, or ordering the corrected word before
the typed one) and confirm each makes the suite fail.
