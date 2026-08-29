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
 * Zero dependencies - needs Node 22+ for the built-in WebSocket.
 *
 *   node tests/run-api-tests.mjs
 *   node tests/run-api-tests.mjs --json
 *   node tests/run-api-tests.mjs --cleanup-only
 *
 * Exit code is 0 when every test passed, 1 otherwise.
 */

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const HERE = dirname(fileURLToPath(import.meta.url));
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

  --port <n>        DevTools port (default 9123)
  --target <text>   Pick the page whose url or title contains <text>
  --cleanup-only    Only remove leftover __slt_ scenes, run no tests
  --json            Emit machine-readable JSON instead of a report
  --list            List attachable pages and exit
  -h, --help        This

OBS with the plugin must already be running. The Streamlabs browser window
does not need to be visible, only started.`);
  process.exit(0);
}

const PORT = Number(opt("--port", "9123"));
const TARGET = opt("--target", null);
const JSON_OUT = has("--json");

const useColor = process.stdout.isTTY && !process.env.NO_COLOR;
const paint = (code, s) => (useColor ? `\x1b[${code}m${s}\x1b[0m` : s);
const green = (s) => paint("32", s);
const red = (s) => paint("31", s);
const grey = (s) => paint("90", s);
const bold = (s) => paint("1", s);

function die(msg, hint) {
  if (JSON_OUT) {
    console.log(JSON.stringify({ ok: false, error: msg, hint: hint || null }));
  } else {
    console.error(`\n${red("Cannot run tests")}  ${msg}`);
    if (hint) console.error(grey(hint) + "\n");
  }
  process.exit(1);
}

/* ------------------------------------------------------------- devtools --- */

async function listTargets() {
  let res;
  try {
    res = await fetch(`http://127.0.0.1:${PORT}/json/list`, { signal: AbortSignal.timeout(3000) });
  } catch {
    die(
      `nothing is listening on DevTools port ${PORT}.`,
      "Start OBS with the plugin first. The Streamlabs browser process opens the port on launch;\n" +
      "it is set in SlBrowser.cpp (settings.remote_debugging_port). Pass --port if you changed it."
    );
  }
  if (!res.ok) die(`DevTools returned HTTP ${res.status}`);
  return res.json();
}

function pickPage(targets) {
  const pages = targets.filter((t) => t.type === "page" && t.webSocketDebuggerUrl);
  if (!pages.length) {
    die(
      "the browser is up but exposes no attachable page.",
      "Open the Streamlabs window once from the OBS menu bar so a page exists, then retry."
    );
  }
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
    return new Promise((resolve, reject) => {
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
        resolve(c);
      }, { once: true });
    });
  }

  send(method, params = {}) {
    const id = ++this.#id;
    return new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve, reject });
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
  const width = Math.max(...summary.results.map((r) => r.name.length));
  console.log("");
  for (const r of summary.results) {
    if (r.status === "pass") {
      console.log(`  ${green("PASS")}  ${r.name}`);
    } else if (r.status === "fail") {
      console.log(`  ${red("FAIL")}  ${bold(r.name)}`);
      if (r.why) console.log(`        ${red(r.why)}`);
    } else {
      console.log(`  ${grey("····")}  ${grey(r.name + (r.why ? " — " + r.why : ""))}`);
    }
  }
  const line = `${summary.pass} passed, ${summary.fail} failed`;
  console.log("\n  " + (summary.fail ? red(bold(line)) : green(bold(line))) + "\n");
  void width;
}

/* ------------------------------------------------------------------ main --- */

const targets = await listTargets();

if (has("--list")) {
  for (const t of targets.filter((x) => x.type === "page")) {
    console.log(`${t.title || "(untitled)"}\n  ${t.url}\n`);
  }
  process.exit(0);
}

const page = pickPage(targets);
const suiteSource = await readFile(SUITE, "utf8");

let cdp;
try {
  cdp = await Cdp.connect(page.webSocketDebuggerUrl);
} catch (e) {
  die(e.message, "The page was listed but would not accept a DevTools connection.");
}

try {
  const injected = await cdp.evaluate(`${suiteSource}\n;typeof globalThis.__slDualTests === "object"`);
  if (injected !== true) die("the suite did not install itself in the page.");

  const ready = await cdp.evaluate("__slDualTests.available()");
  if (!ready) {
    die(
      "window.slabsGlobal is missing on that page.",
      `Attached to: ${page.url}\nThat is not the Streamlabs browser page - use --list, then --target <text>.`
    );
  }

  if (has("--cleanup-only")) {
    const removed = await cdp.evaluate("__slDualTests.cleanup()", { awaitPromise: true });
    const msg = removed?.length ? `removed: ${removed.join(", ")}` : "nothing to remove";
    console.log(JSON_OUT ? JSON.stringify({ ok: true, removed: removed || [] }) : `\n  ${grey(msg)}\n`);
    process.exit(0);
  }

  if (!JSON_OUT) {
    console.log(`\n  ${bold("Dual Output API tests")}`);
    console.log(grey(`  ${page.url}`));
  }

  const summary = await cdp.evaluate("__slDualTests.run()", { awaitPromise: true });

  if (JSON_OUT) console.log(JSON.stringify({ ok: summary.fail === 0, ...summary }));
  else report(summary);

  process.exit(summary.fail === 0 ? 0 : 1);
} catch (e) {
  die(e.message);
} finally {
  cdp?.close();
}
