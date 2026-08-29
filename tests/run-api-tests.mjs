#!/usr/bin/env node
/*
 * Console runner for the dual output API tests.
 *
 * The api only exists on window.slabsGlobal inside the Streamlabs browser
 * process, so this does not reimplement it: CEF already listens for DevTools
 * on 9123 (SlBrowser.cpp sets remote_debugging_port), so we attach there,
 * inject tests/dual-output-tests.js into the live page and read the results
 * back. Same suite the HTML page runs, same context.
 *
 * If nothing is listening it starts OBS itself, waits for the port, and closes
 * it again afterwards. An OBS that was already running is attached to and left
 * alone.
 *
 * Zero dependencies - needs Node 22+ for the built-in WebSocket.
 *
 *   node tests/run-api-tests.mjs
 *   node tests/run-api-tests.mjs --keep-open
 *   node tests/run-api-tests.mjs --no-launch --json
 *
 * Exit code is 0 when every test passed, 1 otherwise.
 */

import { readFile } from "node:fs/promises";
import { existsSync, readFileSync } from "node:fs";
import { spawn, execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");
const SUITE = join(HERE, "dual-output-tests.js");

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const opt = (f, d) => {
  const i = argv.indexOf(f);
  return i !== -1 && argv[i + 1] ? argv[i + 1] : d;
};

if (has("--help") || has("-h")) {
  console.log(`Dual Output API tests

  node tests/run-api-tests.mjs [options]

Starts OBS if it is not already up, runs the suite against the live plugin,
then closes the OBS it started. An OBS that was already running is reused and
left open.

  --no-launch          Never start OBS; fail if nothing is listening
  --keep-open          Leave OBS running afterwards even if we started it
  --obs <path>         Path to obs64.exe (default: the dev_build.ps1 rundir)
  --config <name>      Build config for that default path (default RelWithDebInfo)
  --collection <name>  Load a specific scene collection - worth using, see below
  --launch-timeout <s> How long to wait for OBS to come up (default 90)

  --port <n>           DevTools port (default 9123)
  --target <text>      Pick the page whose url or title contains <text>
  --cleanup-only       Only remove leftover __slt_ scenes, run no tests
  --json               Emit machine-readable JSON instead of a report
  --list               List attachable pages and exit
  -h, --help           This

The suite creates and deletes scenes in whatever collection is loaded, and one
test borrows the name of your first horizontal scene. Point --collection at a
scratch collection if that matters.`);
  process.exit(0);
}

const PORT = Number(opt("--port", "9123"));
const TARGET = opt("--target", null);
const JSON_OUT = has("--json");
const LAUNCH_TIMEOUT_MS = Number(opt("--launch-timeout", "90")) * 1000;

const useColor = process.stdout.isTTY && !process.env.NO_COLOR;
const paint = (code, s) => (useColor ? `\x1b[${code}m${s}\x1b[0m` : s);
const green = (s) => paint("32", s);
const red = (s) => paint("31", s);
const grey = (s) => paint("90", s);
const bold = (s) => paint("1", s);

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// stopObs runs from sync contexts (die, signal handlers) and cannot await.
const sleepSync = (ms) => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);

let launched = null; // pid of an OBS we started

function say(msg) {
  if (!JSON_OUT) console.log(grey("  " + msg));
}

function die(msg, hint) {
  stopObs();
  if (JSON_OUT) {
    console.log(JSON.stringify({ ok: false, error: msg, hint: hint || null }));
  } else {
    console.error(`\n${red("Cannot run tests")}  ${msg}`);
    if (hint) console.error(grey(hint) + "\n");
  }
  process.exit(1);
}

/* ------------------------------------------------------------------- obs --- */

function defaultObsExe() {
  const config = opt("--config", "RelWithDebInfo");
  let version;
  try {
    version = readFileSync(join(REPO, "obs.ver"), "utf8").trim();
  } catch {
    return null;
  }
  // Mirrors CI\dev_build.ps1: builds\obs-studio-<ver>\build_x64\rundir\<config>\bin\64bit
  return join(REPO, "builds", `obs-studio-${version}`, "build_x64", "rundir", config, "bin", "64bit", "obs64.exe");
}

async function portIsUp() {
  try {
    const r = await fetch(`http://127.0.0.1:${PORT}/json/version`, { signal: AbortSignal.timeout(1200) });
    return r.ok;
  } catch {
    return false;
  }
}

async function startObs() {
  const exe = opt("--obs", null) || defaultObsExe();

  if (!exe) die("could not work out where obs64.exe is.", "Pass --obs <path>.");
  if (!existsSync(exe)) {
    die(
      `obs64.exe not found at\n  ${exe}`,
      "Build it first with  .\\CI\\dev_build.ps1  (add -Run once so the rundir is populated),\n" +
      "or point at another build with --obs <path>. Use --no-launch to skip launching entirely."
    );
  }

  const args = [
    // We terminate OBS at the end; without this the next start shows the
    // safe-mode prompt and never reaches the plugin.
    "--disable-shutdown-check",
    // Don't argue with an unrelated OBS the user already has open.
    "--multi",
  ];
  const collection = opt("--collection", null);
  if (collection) args.push("--collection", collection);

  say(`starting ${exe}`);
  const child = spawn(exe, args, {
    cwd: dirname(exe), // OBS resolves its data relative to cwd
    detached: true,
    stdio: "ignore",
  });
  child.on("error", (e) => die(`failed to start OBS: ${e.message}`));
  child.unref();
  launched = child.pid;

  const deadline = Date.now() + LAUNCH_TIMEOUT_MS;
  while (Date.now() < deadline) {
    if (await portIsUp()) {
      say(`devtools up on ${PORT}`);
      return;
    }
    if (!alive(launched)) {
      launched = null;
      die(
        "OBS exited before opening the DevTools port.",
        "Start it by hand to see why - a crash-recovery or safe-mode dialog is the usual cause."
      );
    }
    await sleep(500);
  }
  die(
    `OBS did not open port ${PORT} within ${LAUNCH_TIMEOUT_MS / 1000}s.`,
    "It may be showing a dialog on first run. Start it by hand once, then retry."
  );
}

function alive(pid) {
  if (!pid) return false;
  try { process.kill(pid, 0); return true; } catch { return false; }
}

function stopObs() {
  if (!launched || has("--keep-open")) return;
  const pid = launched;
  launched = null;
  try {
    // No /F first: that sends WM_CLOSE so OBS saves and shuts down properly.
    execFileSync("taskkill", ["/PID", String(pid), "/T"], { stdio: "ignore" });
  } catch {
    /* already gone, or refused - the force pass below deals with it */
  }
  for (let i = 0; i < 30 && alive(pid); i++) sleepSync(200);
  if (alive(pid)) {
    try { execFileSync("taskkill", ["/PID", String(pid), "/T", "/F"], { stdio: "ignore" }); } catch { /* gone */ }
  }
}

// Don't strand an OBS we started if the run is interrupted.
process.on("SIGINT", () => { stopObs(); process.exit(130); });
process.on("SIGTERM", () => { stopObs(); process.exit(143); });

/* -------------------------------------------------------------- devtools --- */

async function listTargets() {
  const res = await fetch(`http://127.0.0.1:${PORT}/json/list`, { signal: AbortSignal.timeout(3000) });
  if (!res.ok) die(`DevTools returned HTTP ${res.status}`);
  return res.json();
}

async function waitForPage() {
  const deadline = Date.now() + 30000;
  let last = [];
  while (Date.now() < deadline) {
    try {
      last = await listTargets();
      const pages = last.filter((t) => t.type === "page" && t.webSocketDebuggerUrl);
      if (pages.length) return pages;
    } catch { /* the port answers before the page exists; keep waiting */ }
    await sleep(500);
  }
  die(
    "the browser is up but never exposed an attachable page.",
    "Open the Streamlabs window once from the OBS menu bar, then retry."
  );
}

function pickPage(pages) {
  if (!TARGET) return pages[0];
  const needle = TARGET.toLowerCase();
  const hit = pages.find(
    (p) => (p.url || "").toLowerCase().includes(needle) || (p.title || "").toLowerCase().includes(needle)
  );
  if (!hit) die(`no page matches --target "${TARGET}". Use --list to see what is attachable.`);
  return hit;
}

/* A minimal CDP client. Only Runtime.evaluate is needed. */
class Cdp {
  #ws;
  #id = 0;
  #pending = new Map();

  static connect(url) {
    return new Promise((resolve_, reject) => {
      const c = new Cdp();
      const ws = new WebSocket(url);
      c.#ws = ws;

      const onFail = (e) => reject(new Error(`DevTools websocket failed: ${e?.message || "closed"}`));
      ws.addEventListener("error", onFail, { once: true });
      ws.addEventListener("close", onFail, { once: true });

      ws.addEventListener("open", () => {
        ws.removeEventListener("error", onFail);
        ws.removeEventListener("close", onFail);
        ws.addEventListener("message", (ev) => {
          let msg;
          try { msg = JSON.parse(ev.data); } catch { return; }
          const p = c.#pending.get(msg.id);
          if (!p) return;
          c.#pending.delete(msg.id);
          msg.error ? p.reject(new Error(msg.error.message)) : p.resolve(msg.result);
        });
        resolve_(c);
      }, { once: true });
    });
  }

  send(method, params = {}) {
    const id = ++this.#id;
    return new Promise((res, rej) => {
      this.#pending.set(id, { resolve: res, reject: rej });
      this.#ws.send(JSON.stringify({ id, method, params }));
    });
  }

  // Returns the value, or throws with whatever the page threw.
  async evaluate(expression, { awaitPromise = false } = {}) {
    const r = await this.send("Runtime.evaluate", {
      expression,
      awaitPromise,
      returnByValue: true,
      allowUnsafeEvalBlockedByCSP: true,
    });
    if (r.exceptionDetails) {
      const d = r.exceptionDetails;
      throw new Error(d.exception?.description || d.text || "evaluation threw");
    }
    return r.result?.value;
  }

  close() { try { this.#ws.close(); } catch { /* already gone */ } }
}

/* ------------------------------------------------------------- reporting --- */

function report(summary) {
  console.log("");
  for (const r of summary.results) {
    if (r.status === "pass") console.log(`  ${green("PASS")}  ${r.name}`);
    else if (r.status === "fail") {
      console.log(`  ${red("FAIL")}  ${bold(r.name)}`);
      if (r.why) console.log(`        ${red(r.why)}`);
    } else console.log(`  ${grey("····")}  ${grey(r.name + (r.why ? " — " + r.why : ""))}`);
  }
  const line = `${summary.pass} passed, ${summary.fail} failed`;
  console.log("\n  " + (summary.fail ? red(bold(line)) : green(bold(line))) + "\n");
}

/* ------------------------------------------------------------------ main --- */

let cdp;
try {
  if (!(await portIsUp())) {
    if (has("--no-launch")) {
      die(
        `nothing is listening on DevTools port ${PORT}.`,
        "Drop --no-launch to have this start OBS for you, or start it yourself first."
      );
    }
    await startObs();
  } else {
    say(`attaching to the OBS already on ${PORT}`);
  }

  const pages = await waitForPage();

  if (has("--list")) {
    for (const t of pages) console.log(`${t.title || "(untitled)"}\n  ${t.url}\n`);
    stopObs();
    process.exit(0);
  }

  const page = pickPage(pages);
  const suiteSource = await readFile(SUITE, "utf8");

  try {
    cdp = await Cdp.connect(page.webSocketDebuggerUrl);
  } catch (e) {
    die(e.message, "The page was listed but would not accept a DevTools connection.");
  }

  const injected = await cdp.evaluate(`${suiteSource}\n;typeof globalThis.__slDualTests === "object"`);
  if (injected !== true) die("the suite did not install itself in the page.");

  // slabsGlobal is injected on context creation, which can trail the page by a moment.
  let ready = false;
  for (let i = 0; i < 20 && !ready; i++) {
    ready = await cdp.evaluate("__slDualTests.available()");
    if (!ready) await sleep(500);
  }
  if (!ready) {
    die(
      "window.slabsGlobal never appeared on that page.",
      `Attached to: ${page.url}\nIf that is not the Streamlabs browser page, use --list then --target <text>.`
    );
  }

  if (has("--cleanup-only")) {
    const removed = await cdp.evaluate("__slDualTests.cleanup()", { awaitPromise: true });
    const msg = removed?.length ? `removed: ${removed.join(", ")}` : "nothing to remove";
    console.log(JSON_OUT ? JSON.stringify({ ok: true, removed: removed || [] }) : `\n  ${grey(msg)}\n`);
    cdp.close();
    stopObs();
    process.exit(0);
  }

  if (!JSON_OUT) {
    console.log(`\n  ${bold("Dual Output API tests")}`);
    console.log(grey(`  ${page.url}`));
  }

  const summary = await cdp.evaluate("__slDualTests.run()", { awaitPromise: true });

  if (JSON_OUT) console.log(JSON.stringify({ ok: summary.fail === 0, ...summary }));
  else report(summary);

  cdp.close();
  stopObs();
  process.exit(summary.fail === 0 ? 0 : 1);
} catch (e) {
  cdp?.close();
  die(e.message);
}
