reference
=========

HP's code that is read and never built.

`luna-sysmgr-ce/` is the **TouchPad's** LunaSysMgr, CE 3.0.5 — 21 MB, 605 files.
The system this tree builds is Open webOS's `luna-sysmgr` (in `components/`,
where everything that is built lives); this one is what actually shipped on the
device, and it is the answer to "how did HP do it" for anything the Open webOS
sources do not say. It is not in `MANIFEST.tsv` because the manifest is the
build order, and nothing here is built.

`build-support-ce/` is HP's too: 76 MB of staged ARM headers from the CE drop,
with **zero references anywhere in the tree** -- it was for cross-compiling to
the TouchPad, which this build does not do. It is here rather than under
`adapters/` for the same reason as the above: nothing in `adapters/` is HP's.

Keeping the two out of `components/` is what lets
`git diff --stat hp-original -- components/` mean exactly one thing: what we
changed in HP's code. See `tests/repo-layout.sh`.
