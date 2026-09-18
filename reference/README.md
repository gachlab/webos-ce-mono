reference
=========

HP's code that is read and never built.

`luna-sysmgr-ce/` is the **TouchPad's** LunaSysMgr, CE 3.0.5 — 21 MB, 605 files.
The system this tree builds is Open webOS's `luna-sysmgr` (in `components/`,
where everything that is built lives); this one is what actually shipped on the
device, and it is the answer to "how did HP do it" for anything the Open webOS
sources do not say. It is not in `MANIFEST.tsv` because the manifest is the
build order, and nothing here is built.

Keeping it out of `components/` is what lets
`git diff --stat hp-original -- components/` mean exactly one thing: what we
changed in HP's code. See `tests/repo-layout.sh`.
