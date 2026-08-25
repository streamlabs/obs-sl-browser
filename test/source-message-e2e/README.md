# Browser-source message end-to-end harness

Drives `browsersource_sendMessage` against a real OBS build, with no GUI interaction.
`observer.js` is an independent recorder on port 28951 — it serves the harness pages and
logs what they report, so nothing self-reports through the channel under test.

```
SlBrowser page --browsersource_sendMessage--> plugin --javascript_event--> browser source page
both pages     --HTTP :28951--> observer.js            (out-of-band observation)
```

The channel is one way. There is no reply path, so the observer is how a source page says
anything at all.

## The contract being tested

`source.html` uses no plugin-specific helper. It listens exactly the way a Streamlabs
Desktop overlay does, because the bytes are the same:

```js
window.addEventListener('messageFromApp', e => JSON.parse(e.detail.message));
```

Desktop's patched `BrowserSource::MessageToBrowser` emits
`DispatchJSEvent("messageFromApp", {"message": "<string>"})`, and the plugin reproduces
that envelope through stock obs-browser's `javascript_event` proc handler. A page written
against Desktop therefore runs here unchanged — which is the property this harness exists
to protect. Every round carries quotes, backslashes, newlines and non-ASCII so that a
regression in the envelope's JSON escaping fails as `UNPARSEABLE` rather than passing
quietly.

The scene collection defines three browser sources, which is the point of the test:

| source | visible | `shutdown` | exercises |
| --- | --- | --- | --- |
| `MsgSourceA` | yes | false | delivery, and that it never sees B's payloads |
| `MsgSourceB` | yes | false | delivery, and that it never sees A's payloads |
| `MsgSourceHidden` | no | true | the `warning` field for a source with no live page |

## Prerequisites

A built and installed rundir, per the build steps in the repo root:

```powershell
cmake --build build_x64 --target sl-browser-plugin --config RelWithDebInfo
cmake --install build_x64 --prefix "<obs>\build_x64\rundir\RelWithDebInfo" --config RelWithDebInfo
```

## One-time: portable throwaway config

Keeps the test off any real OBS profile. `BASE_PATH` is `../..` from `bin\64bit`, so the
marker goes in the rundir root.

```powershell
$RD  = "<obs>\build_x64\rundir\RelWithDebInfo"
$CFG = "$RD\config\obs-studio"
New-Item -ItemType Directory -Force "$CFG\basic\scenes","$CFG\basic\profiles\Untitled" | Out-Null
Set-Content "$RD\portable_mode.txt" -Value "" -NoNewline

@"
[General]
FirstRun=true
ConfirmOnExit=false
EnableAutoUpdates=false

[Basic]
Profile=Untitled
ProfileDir=Untitled
SceneCollection=SourceMessageE2E
SceneCollectionFile=SourceMessageE2E
"@ | Set-Content "$CFG\user.ini" -Encoding utf8

"[General]`nName=Untitled" | Set-Content "$CFG\basic\profiles\Untitled\basic.ini" -Encoding utf8
Copy-Item .\SourceMessageE2E.json "$CFG\basic\scenes\SourceMessageE2E.json" -Force
```

`FirstRun=true` suppresses the auto-configuration wizard, which would otherwise block
startup with a modal.

## Run

```powershell
node observer.js                      # terminal 1, holds :28951

$env:SL_PLUGIN_DEFAULT_URL = "http://127.0.0.1:28951/slbrowser.html"
& "$RD\bin\64bit\obs64.exe" --collection SourceMessageE2E --disable-updater --disable-shutdown-check
```

`--disable-shutdown-check` is not optional here. Killing OBS at the end of a run is an
unclean shutdown, so the next launch opens a safe-mode modal and sits there — the log
stops after `[Safe Mode] Unclean shutdown detected!` and the harness looks hung.

`SL_PLUGIN_DEFAULT_URL` (`SlBrowser.cpp:103`) repoints the Streamlabs window at the
harness page instead of `https://obs-plugin.streamlabs.com`. That is what makes the test
scriptable — both ends are pages we control.

The sender runs 20 rounds at 2s intervals, so give it about a minute. Then:

```powershell
curl http://127.0.0.1:28951/verdict
curl http://127.0.0.1:28951/events
```

`/verdict` is the pass condition: `received > 0` and `misrouted == 0`, with a count for
each of `MsgSourceA` and `MsgSourceB`. Repeating rather than sending once is deliberate —
a browser source can take anywhere from ~10s to ~70s to load its page after OBS starts,
and nothing acknowledges, so early rounds are expected to land nowhere.

`/events` additionally carries the error-path probes:

| probe | expected callback |
| --- | --- |
| `ERRPATH-unknown-source` | `{"error": "Did not find a source with name NoSuchSource"}` |
| `ERRPATH-not-browser-source` | `{"error": "Scene is not a browser source"}` |
| `ERRPATH-empty-name` | `{"error": "param2 (sourceName) is required"}` |
| `WARNPATH-hidden-source` | `{"success": true, "warning": "..."}` |

There is no malformed-payload probe: the third argument is an opaque string and the plugin
builds the JSON envelope around it, so there is nothing the caller can pass that is invalid.

## Gotcha that will cost you an hour

`GrpcBrowser::com_grpc_run_javascriptOnBrowser` targets
`BrowserClient::GetMostRecentRenderKnown()`, which despite the name is only ever assigned
in `RegisterCallback` — i.e. it stays null until the SlBrowser page calls some
`slabsGlobal.*` function. A page that only listens never receives its callbacks; the push
is dropped to a `printf`. `slbrowser.html` therefore makes a priming `sl_getVersionInfo()`
call before anything else. The production Streamlabs page primes itself naturally via its
startup calls.
