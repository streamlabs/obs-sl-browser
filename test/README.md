# Tests

End-to-end suites that run against a real OBS with this plugin loaded.

```
node test/run.mjs                 # every suite
node test/run.mjs smoke           # one
node test/check.mjs               # the static checks, no OBS needed, about a second
```

Exit code is 0 when everything passed. Node 22+, no dependencies.

## Why there are no unit tests here

The api under test is injected onto `window.slabsGlobal` by a CEF process that only exists
inside a running OBS, and most of what is worth asserting — which canvas owns a scene, which
browser source a message reached, whether two outputs fight over the same encoder — is only
true with libobs up. A mock of that surface would only ever agree with itself.

So the harness does the expensive thing properly: it starts OBS, drives the real api, and
reads the real answers. `test/check.mjs` covers the cheap mistakes separately.

## Running

The default OBS is the rundir `CI\dev_build.ps1` produces, resolved through `obs.ver`:
`builds\obs-studio-<version>\build_x64\rundir\RelWithDebInfo`. Build it once with
`.\CI\dev_build.ps1 -Run` so the rundir is populated, then:

```powershell
node test/run.mjs
node test/run.mjs --obs "D:\some\other\rundir\bin\64bit\obs64.exe"
node test/run.mjs --no-launch --keep-open      # against an OBS you already have open
node test/run.mjs --json --junit results.xml   # what CI runs
```

`--help` lists the rest.

### It uses its own OBS profile, not yours

OBS is started with `--portable`, so it reads `<rundir>\config\obs-studio` instead of your
real profile under `%APPDATA%`. The harness rewrites that directory before every suite:
a scene collection, a profile, and the four settings that let OBS start unattended —
`FirstRun` (the auto-configuration wizard is a modal, and startup never completes behind
it), `ConfirmOnExit`, `EnableAutoUpdates`, and `BrowserHWAccel` (CEF's shared-texture path,
which CI has no hardware for).

It marks the directory as its own. If it finds a portable config there that it did not
write, it stops rather than overwrite your work — pass `--force` if you meant it.

Your everyday OBS is untouched: nothing writes to `%APPDATA%\obs-studio`, and no
`portable_mode.txt` marker is left behind, so launching that rundir yourself still behaves
normally.

### One OBS at a time

Only one process can hold the CEF DevTools port (9123, hardcoded in `SlBrowser.cpp`, which is
why there is no flag for it). If something is already on it the harness refuses to start a
second OBS, because it would have no way to tell which one it had attached to. Close it, or
use `--no-launch` to run against it.

That is also why `--keep-open` applies to the last suite of a run: an OBS left open by an
earlier suite would block every later one.

## Adding a suite

A suite is a directory under `test/suites/` with a `suite.mjs`:

```js
import { results, until } from "../../harness/suite.mjs";

export default {
  name: "my-feature",                  // must match the directory name
  description: "what it proves",
  page: "page.html",                   // served by the harness, loaded by the Streamlabs window
  collection: "collection.json",       // optional scene collection
  timeoutMs: 180000,

  async run({ cdp, observer, obs, dir, workDir, say }) {
    const r = results();
    await r.step("the thing works", async () => {
      const res = await cdp.call("obs_enum_scenes");
      if (!Array.isArray(res)) return `got ${JSON.stringify(res)}`;   // returning a string fails it
    });
    return r.list;
  },
};
```

`run.mjs` does the rest, the same way for every suite: serve the suite directory, seed a
fresh profile, install the collection, start OBS pointed at the page, attach over CDP,
inject the shared in-page helpers, prime the callback path, run `run()` under its timeout,
then tear everything down and dump the observer's events to `test/.work/<suite>/events.json`.

### What `ctx` gives you

| | |
| --- | --- |
| `cdp.call(fn, ...args)` | call a plugin api function, get its reply parsed. `{__missing}`, `{__timeout}` and `{__raw}` are the diagnostic shapes; a setter answering with an empty string parses to `{}` |
| `cdp.evaluate(expr, {awaitPromise})` | run anything in the page |
| `cdp.inject(file)` | load a classic script into the page — how a large in-page suite gets there |
| `observer.events` | everything any page has reported, newest last |
| `observer.waitFor(pred, {timeoutMs})` | resolves true when `pred(events)` holds, false on timeout |
| `observer.origin` | the harness server's url |
| `say(msg)` | progress line, suppressed under `--json` |

### Reporting from a page

Some things cannot be asserted from the page that caused them. A message sent to a browser
source lands in a *different* CEF process with no reply path, so the receiving page reports
over HTTP to the harness instead — a channel that is deliberately not the one under test, so
a total failure shows up as silence rather than as a missing observer.

```js
fetch(location.origin + '/report', {
  method: 'POST',
  headers: {'content-type': 'application/json'},
  body: JSON.stringify({who: 'MySource', event: 'RECEIVED', data: {...}})
});
```

Pages are served by the harness, so `location.origin` is always right and nothing hardcodes
a port. A scene collection can reach it too: `{{BASE_URL}}` anywhere in the collection json
is replaced with that origin when the collection is installed.

### Two gotchas worth knowing before you lose an hour to them

**Prime before you listen.** `GrpcBrowser::com_grpc_run_javascriptOnBrowser` pushes to
`BrowserClient::GetMostRecentRenderKnown()`, which — despite the name — is only ever assigned
in `RegisterCallback`. Until the page calls *some* `slabsGlobal` function, that target is null
and pushes are dropped to a `printf`. A page that only listens never hears anything. The
harness makes a priming call after attaching, so suites inherit this already done; a page
that runs standalone in a browser has to do it itself.

**Browser sources load late.** A browser source can take anywhere from ten seconds to over a
minute to load its page after OBS starts, and nothing announces when it has. Repeat and wait
(`until`, `observer.waitFor`) rather than sending once and hoping.

## `check.mjs`

Three things, in about a second, with no build:

1. every test file parses — modules and standalone scripts through `node --check`, the
   inline `<script>` blocks in suite pages compiled without being run, and scene collections
   as JSON
2. every `suite.mjs` satisfies the contract, and the files it names are real files inside
   the suite directory
3. every api function a suite calls exists in `JavascriptApi.h`

(3) is the one that earns its place. A renamed or misspelled api call otherwise costs a
fifteen-minute build to discover, and it surfaces as "the callback never fired", which reads
like a runtime bug rather than a typo. If a suite calls something absent on purpose, list it
in the suite's `expectMissing`.

## CI

`.github/workflows/tests.yml` runs `check.mjs` on every pull request in under a minute, and
the suites against a freshly built OBS after it. See that file for what the e2e job does.

## Layout

```
test/
  run.mjs          the CLI
  check.mjs        the static checks
  harness/
    obs.mjs        find, start and stop OBS
    profile.mjs    the throwaway portable profile
    cdp.mjs        DevTools client, page attachment, api calls
    inpage.js      the shared in-page helpers (window.__slt)
    observer.mjs   the suite web server and its out-of-band report log
    suite.mjs      the suite contract, results(), until()
    report.mjs     console, json and JUnit output
  suites/
    smoke/         the plugin comes up and answers
    dual-output-api/ the vertical canvas api and its isolation
    source-message/ browsersource_sendMessage reaches the right browser source
```
