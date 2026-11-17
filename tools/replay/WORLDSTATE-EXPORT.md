# RA1 replay world-state export

The exporter is called from the native replay frame hook only while a native replay is playing. It writes one JSON object per line to `worldstate-export.jsonl` in the game working directory, once every 60 simulation frames.

## Current snapshot fields

Each line contains:

- `frame`: the RA1 simulation frame.
- `objects`: active entries read from the five RA1 object heaps (aircraft, buildings, infantry, units, vessels).
- `houses`: active house records with identity, color, credits, power, defeat/spectator flags, and current object counts.

Object records currently include the pool name, RTTI, heap ID, X/Y lepton coordinates, current strength, and limbo state. The exporter deliberately leaves owner, type ID, maximum strength, facings, and mission as null until their exact RA1 runtime offsets are verified. `objectsComplete` is false to prevent consumers from treating this as a complete object schema.

House fields use the offsets already maintained in `inc/houseclass.h`. Object-pool traversal uses the `FixedHeapClass` layout from EA's `CODE/HEAP.H`: size, total count, active count, and backing buffer. The object common prefix follows `CODE/ABSTRACT.H` and `CODE/OBJECT.H`.

## Build and test

1. Build the patch using the repository's existing Win32 toolchain.
2. Put the resulting patched game binary in a test RA1 directory.
3. Launch a native `.rarp` replay through the existing playback path.
4. After frame 60, check for `worldstate-export.jsonl` beside the game executable.
5. Parse each line as independent JSON and confirm frame values increase and object coordinates change as the replay runs.

This is a first engine-side export layer, not yet the complete Milestone 5 exporter. It does not modify the `.rarp` format, recorded commands, or replay verification stream. It does not yet export projectile paths, death effects, production queues, or complete type/owner/facing metadata.
