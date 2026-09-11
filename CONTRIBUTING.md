# Contributing

Thank you for considering a contribution to Der Tondehr Crunchy.

## Before starting

1. Search for an existing issue related to the change.
2. If the change affects sound, parameters, saved state, or identifiers, open a proposal first and describe the expected behavior.
3. Create a branch from the current main branch.
4. Keep each change focused on one concrete problem.

## Building and validating a change

Follow [BUILDING.md](BUILDING.md), build Release, and run:

```powershell
ctest --test-dir build\windows -C Release --output-on-failure
```

State which tests you ran in the pull request. For audio changes, include sample rate, block size, channel, oversampling mode, and the relevant control settings.

## Code rules

- Keep the project compatible with C++17.
- Do not allocate memory, lock, access files, or open UI dialogs on the audio thread.
- Do not change existing parameter identifiers; saved sessions depend on them.
- Keep DSP state and output finite when input or host data is invalid.
- Document perceptual constants that are not directly derived from the implemented circuit.
- Keep plugin processing separate from standalone device management.

## External material

Do not add manuals, circuit drawings, logos, recordings, impulse responses, commercial presets, or other files that you are not permitted to redistribute. An issue may link to a legitimately public source when necessary, but the source file must not be copied into this repository.

Do not add a third-party product name to the plugin name, repository name, executable name, topic list, or visual branding. Any descriptive reference must be limited, accurate, and accompanied by a clear non-affiliation statement.

## Pull requests

Include:

- The concrete problem being solved.
- Previous and new behavior.
- Main files changed.
- Validation performed.
- Known sound, compatibility, performance, or migration risks.

By contributing, you agree that your contribution may be distributed under the project's MIT License.

