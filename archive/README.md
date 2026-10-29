# The archive

Everything that was cut from LLocr before the release, kept whole.

## Why

The release ships four tasks and eight models. Nine models and two of the six
tasks did not earn their place: the decision models that worked markedly worse
and slower than MiniCPM-OCR Jev, and the OCR models whose quality, speed,
licence or language support did not hold up.

The material is kept here, not deleted, because the work is worth reusing: a
model that was wrong for this release is not wrong forever.

## What is here

- `models/profiles/` — the model profiles, verbatim
- `models/docs/` — the manuals of the removed models
- `parsers/` — the parsers that existed only for the removed models
- `single-pass/` — the single-pass page recognition controller
- `decision-text/` — the text-decision prompt profile and the calibration harness

## How to bring a model back

1. Copy its profile from `models/profiles/` back to `resources/profiles/models/`.
   The loader globs that directory: a new model is a new file, no list to maintain.
2. Copy its parser from `parsers/` back to `src/parsers/` and add the two lines
   to `src/CMakeLists.txt`.
3. Register the parser id in `src/parsers/ParserFactory.cpp`.
4. Add the profile's file to `LLOCR_MODEL_PROFILE_FILES` in `tests/CMakeLists.txt`.
5. Give the profile a role. The roles are `layout`, `blockRecognition`,
   `decision` and `fix` — see `src/core/ConnectionRole.h`.
6. Restore the task's settings row in `roleSettings()` in
   `src/config/SettingsStore.cpp` and its case in
   `connectionRoleName()`.

## The code that was interleaved with the shared code

The removal was one commit, and the tree before it is the tag
`archive/pre-cleanup`:

    git show archive/pre-cleanup:src/app/RecognitionController.cpp

Every file that the removal touched is reachable there in full — the removed
hunks included.
