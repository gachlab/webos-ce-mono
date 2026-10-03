audiod-pipewire
===============

`com.palm.audio` for a machine whose audio daemon is PipeWire, reached through
the PulseAudio client API.

Ours, not HP's. On a device this was audiod, and nothing in the CE drop provides
it: `com.palm.audio` appears zero times in any session log, so every audio
control in the shell is inert while the apps' own sound works fine. Mute Sound
round-trips through db8 and nobody consumes it; the volume HUD never draws; the
volume keys reach the shell and nothing acts on them; the 12 files in
`/usr/palm/sounds` never play.

PipeWire, through the PulseAudio API, and why that is not a contradiction
------------------------------------------------------------------------

The PulseAudio *daemon* is dead and not running here; the daemon is PipeWire.
The PulseAudio *client API* is one of the APIs PipeWire implements, and the one
its own documentation points ordinary applications at. The measurement agrees:
the session's own stream already arrives tagged `client.api = "pipewire-pulse"`.
So talking Pulse here is talking to PipeWire, through the interface it wants apps
to use.

**webOS is its own application.** Its volume applies to its own streams, not the
machine's. The session already appears as its own node
(`application.name = "WebAppManager"`, `media.class = Stream/Output/Audio`), with
a volume of its own that QtWebEngine created. `set_sink_input_volume` on that
node is "webOS's volume", scoped to webOS and nothing else on the host. No
virtual sink or loopback is needed.

What it answers
---------------

| Direction | Category / method | Who |
|---|---|---|
| in  | `com.palm.keys/audio/status` (subscribe) | the shell's `InputManager` publishes `{"key":"volume_up"\|"volume_down","state":"up"}` |
| in  | `com.palm.systemservice/getPreferences` (subscribe) | `muteSound`, and the tone names |
| out | `com.palm.audio/{system,media,ringtone,phone}/status` | `NativeAlertManager` (the HUD) and `DisplayManager` subscribe |
| out | `com.palm.audio/systemsounds/playFeedback` | `SoundPlayerPool` |

The status payload is the superset of what both consumers read, sent every time,
because schema validation is global-off and each consumer drops a message that is
missing a field it wants. The HUD pops only when the payload's `changed` array
contains `"volume"` and not `"scenario"`; see `src/audio_contract.h`, which holds
the exact shapes verified against the consumers.

The output chooser is deliberately half-built
----------------------------------------------

Volume and mute are **proven** and implemented. Moving webOS's audio to a chosen
output is **not settled**, and it is exactly the PipeWire-vs-WirePlumber
question: two attempts on the running session failed, one where the write did
nothing and one where it was reverted within three seconds -- plausibly by a
custom WirePlumber Lua component that rewrites preferred devices.

`Backend::moveOutputToTarget` is therefore unimplemented and returns false, with
the settling test written out in `src/pulse_backend.cpp`: write the route, read
it back at 0s/1s/3s. Appears-then-reverts is policy (WirePlumber); never-appears
is the command (PipeWire). Only after that observation is the path chosen
(`move_sink_input`, metadata, or asking the session manager). The chooser's UI,
the bus contract and the volume half do not depend on it.

System sounds
-------------

`playFeedback {name}` plays the matching file from `/usr/palm/sounds` as webOS's
own PipeWire stream, by spawning a PipeWire player rather than decoding in
process. Only names with a file that actually ships play; the UI-click set HP
never released in the CE drop plays nothing, rather than inventing a sound. See
`src/system_sounds.h`.

Why C++
-------

The shell's consumers `addmatch`/subscribe to the bus and the service drives a
native audio client; both are C++ territory, as with `sysfs-powerd`.

Built by `tools/build.sh audiod`; started with the other static services in the
launcher's `services` stage.
