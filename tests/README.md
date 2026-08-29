# Dual Output API tests

`dual-output-api.html` exercises the vertical-canvas JS API against a running
plugin. It is an integration test, not a unit test: the API only exists inside
the Streamlabs browser process, and the behaviour worth checking (canvas
namespacing, persistence, output arbitration) only exists with libobs up.

## Running from a console

The suite lives in `dual-output-tests.js`; the console runner injects it into the
live page over CEF's DevTools port (`SlBrowser.cpp` already sets
`remote_debugging_port = 9123`) and reads the results back. Zero dependencies,
needs Node 22+ for the built-in WebSocket.

```
node tests/run-api-tests.mjs                # start OBS, run, close it; exit 1 on failure
node tests/run-api-tests.mjs --keep-open    # leave OBS running afterwards
node tests/run-api-tests.mjs --json         # machine-readable
node tests/run-api-tests.mjs --cleanup-only # drop leftover __slt_ scenes
node tests/run-api-tests.mjs --no-launch    # require an OBS that is already up
```

It starts OBS itself if nothing is on the port, waits for DevTools, and closes
it again when done. An OBS that is already running is attached to and left
alone, so this is safe to run beside a session you are working in.

The OBS it starts comes from the `CI\dev_build.ps1` rundir, resolved through
`obs.ver`; override with `--obs <path>` or `--config <name>`. It is launched
with `--disable-shutdown-check` (the run ends by terminating OBS, and without
this the next start stops at the safe-mode prompt) and `--multi`.

The suite creates and deletes scenes in whatever collection is loaded, and one
test borrows the name of your first horizontal scene, so `--collection <name>`
is worth pointing at a scratch collection.

## Running in the browser

The plugin injects the api onto `window.slabsGlobal` in its own CEF process, so
the page has to be loaded by *that* browser — not an OBS browser dock, and not
an ordinary browser. `SlBrowser::getDefaultUrl()` reads `SL_PLUGIN_DEFAULT_URL`,
so point it at this file and start OBS:

```powershell
$env:SL_PLUGIN_DEFAULT_URL = "file:///C:/work/repos/obs-sl-browser/tests/dual-output-api.html"
& "C:\work\repos\obs-studio-31.1.2\build_x64\rundir\RelWithDebInfo\bin\64bit\obs64.exe"
```

Then open the Streamlabs window from the menu bar and press **Run tests**.

If `file://` is blocked by the CEF build, the plugin registers a scheme handler
for local paths — use `http://absolute/C:/work/repos/obs-sl-browser/tests/dual-output-api.html`
instead.

## What it covers

The isolation invariants are the point — they are what the canvas argument
design rests on, and what silently breaks if scene resolution regresses.

- every `dualoutput_*` function is exposed, and `getState` has its documented shape
- `obs_enum_scenes` with no canvas argument returns exactly what it did before
  a vertical scene existed (backward compatibility)
- a vertical scene never appears in the main scene list
- the same scene name can exist on both canvases
- an unrecognised canvas name errors **and** creates nothing on either canvas
- a source added to a vertical scene round-trips its position, and does not
  appear in a main scene
- vertical canvas dimensions are not the main canvas dimensions
- stream settings round-trip, and a partial update keeps the fields it omits
- `enhanced_broadcasting` mode refuses `dualoutput_startStream`
- an invalid output mode is rejected and does not change stored state
- `setCanvasSize` reports the aligned size actually applied

## Safety

Scenes are created with a `__slt_` prefix and removed afterwards. Canvas size,
output mode and stream settings are snapshotted before being changed and
restored during cleanup. No stream is ever started against a real endpoint —
the settings test writes `rtmp://127.0.0.1`, and the only `startStream` call is
the one asserted to be refused.

Cleanup runs even when tests fail. **Clean up only** re-runs it on its own if a
run is interrupted.

One caveat: the "same scene name on both canvases" test borrows the name of
your first horizontal scene, so cleanup removes a vertical scene with that
name. If you happen to have a real vertical scene named after a horizontal one,
run this against a scratch collection.
