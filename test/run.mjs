#!/usr/bin/env node
/*
 * Runs the end-to-end suites against a real OBS with the plugin loaded.
 *
 *   node test/run.mjs                 every suite
 *   node test/run.mjs smoke           just this one
 *   node test/run.mjs --help
 *
 * Exit code is 0 when every test passed, 1 otherwise.
 *
 * There is no unit-test tier here on purpose. The api under test is injected into a CEF
 * process by a plugin loaded into OBS, and most of what is worth asserting - scene
 * ownership, message routing, output arbitration - is only true with libobs up. A mock of
 * that would only ever agree with itself.
 *
 * Zero dependencies. Node 22+ for the built-in WebSocket.
 */

import { readdirSync, existsSync, mkdirSync, rmSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, join, resolve } from "node:path";

import { resolveObsExe, rundirOf, launchObs, assertObsExe, devtoolsUp, sleep } from "./harness/obs.mjs";
import { seedProfile, latestLog } from "./harness/profile.mjs";
import { Cdp, waitForPage } from "./harness/cdp.mjs";
import { startServer } from "./harness/observer.mjs";
import { DEFAULTS, validate, validateResults } from "./harness/suite.mjs";
import { renderConsole, toJson, writeJUnit, failed, grey, red, bold } from "./harness/report.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");
const SUITES_DIR = join(HERE, "suites");
const WORK = join(HERE, ".work");

const TAKES_VALUE = new Set(["--obs", "--config", "--observer-port", "--junit"]);

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const opt = (f, d) => { const i = argv.indexOf(f); return i !== -1 && argv[i + 1] ? argv[i + 1] : d; };
// Positional arguments are suite names: anything that is neither a flag nor a flag's value.
const wanted = argv.filter((a, i) => !a.startsWith("-") && !(i > 0 && TAKES_VALUE.has(argv[i - 1])));

if (has("--help") || has("-h")) {
	console.log(`End-to-end suites for the sl-browser plugin

  node test/run.mjs [suite ...] [options]

Starts OBS with a throwaway portable profile, runs each suite against the live plugin,
then closes it again.

  --obs <path>          obs64.exe to use (default: the CI\\dev_build.ps1 rundir, or $OBS_EXE)
  --config <name>       build config for that default path (default RelWithDebInfo)
  --observer-port <n>   pin the harness web server's port (default: an ephemeral one)
  --no-launch           use an OBS that is already running instead of starting one
  --keep-open           leave OBS running afterwards (the last suite's, if several run)
  --force               overwrite a portable config in the rundir that we did not write
  --json                machine-readable output
  --junit <path>        also write a JUnit xml report
  --list                list the suites and exit
  -h, --help            this

The suites run against <rundir>\\config\\obs-studio, which the harness owns and rewrites -
not your real OBS profile.`);
	process.exit(0);
}

// Not configurable: SlBrowser.cpp:227 hardcodes remote_debugging_port, so a flag here could
// only ever point the poller at a port nothing was going to open.
const PORT = 9123;
const JSON_OUT = has("--json");
const say = (m) => { if (!JSON_OUT) console.log(grey("  " + m)); };

/* ---------------------------------------------------------------- suites --- */

function discover() {
	if (!existsSync(SUITES_DIR)) return [];
	return readdirSync(SUITES_DIR, { withFileTypes: true })
		.filter((d) => d.isDirectory() && existsSync(join(SUITES_DIR, d.name, "suite.mjs")))
		.map((d) => d.name)
		.sort();
}

async function load(name) {
	const dir = join(SUITES_DIR, name);
	const mod = await import(pathToFileURL(join(dir, "suite.mjs")));
	const suite = { ...DEFAULTS, ...mod.default, dir };
	const problems = validate(suite, name);
	if (problems.length) throw new Error(`suites/${name}/suite.mjs: ${problems.join("; ")}`);
	return suite;
}

const available = discover();
const unknown = wanted.filter((w) => !available.includes(w));
if (unknown.length) {
	console.error(`${red("Unknown suite:")} ${unknown.join(", ")}\nAvailable: ${available.join(", ") || "(none)"}`);
	process.exit(1);
}
const selected = wanted.length ? wanted : available;

if (has("--list")) {
	for (const name of available) {
		const s = await load(name).catch((e) => ({ description: red(e.message) }));
		console.log(`  ${bold(name.padEnd(20))} ${s.description || ""}`);
	}
	process.exit(0);
}
if (!selected.length) {
	console.error("No suites found under test/suites/.");
	process.exit(1);
}

/* ------------------------------------------------------------------- run --- */

let current = null;          // the OBS this process started, so a signal can still kill it
let keepCurrentOpen = false; // --keep-open, but only for the suite it can apply to

const shutdown = (code) => () => { if (!keepCurrentOpen) current?.stop(); process.exit(code); };
process.on("SIGINT", shutdown(130));
process.on("SIGTERM", shutdown(143));

const exe = resolveObsExe({ repoRoot: REPO, explicit: opt("--obs", null), config: opt("--config", "RelWithDebInfo") });

async function runSuite(name) {
	const started = Date.now();
	// Only one process can hold the DevTools port, so an OBS left open by an earlier suite
	// would block every later one. --keep-open therefore applies to the last suite that runs.
	const keepOpen = has("--keep-open") && name === selected.at(-1);
	keepCurrentOpen = keepOpen;

	const workDir = join(WORK, name);
	let server, cdp, obs;
	const run = { suite: name, results: [], ms: 0 };

	// Importing the suite and preparing its scratch directory are inside the boundary too:
	// outside it, a suite.mjs that will not import took down the whole loop, and the console
	// and JUnit reports for the suites that did run were never written.
	try {
		const suite = await load(name);
		run.description = suite.description;

		rmSync(workDir, { recursive: true, force: true });
		mkdirSync(workDir, { recursive: true });

		server = await startServer({ dir: suite.dir, port: Number(opt("--observer-port", "0")) });
		const pageUrl = server.pageUrl(suite.page);

		if (has("--no-launch")) {
			if (!(await devtoolsUp(PORT))) {
				throw new Error(`nothing is listening on DevTools port ${PORT}. Drop --no-launch to start OBS.`);
			}
			say(`attaching to the OBS already on ${PORT}`);
			// It is showing whatever page it was on, so send it to ours.
			const existing = await waitForPage(PORT);
			const nav = await Cdp.connect(existing.webSocketDebuggerUrl);
			await nav.evaluate(`location.href = ${JSON.stringify(pageUrl)}`).catch(() => {});
			nav.close();
			await sleep(1000);
		} else {
			// Before seedProfile, which derives a rundir from this path and writes into it.
			assertObsExe(exe);
			// --multi means a second OBS would start happily, but only one process can hold
			// the DevTools port - so we would attach to the wrong one and report its answers.
			if (await devtoolsUp(PORT)) {
				throw new Error(
					`something is already listening on DevTools port ${PORT}, so a new OBS could not be ` +
					`told apart from it. Close that OBS, or pass --no-launch to run against it.`);
			}
			const { collectionName } = seedProfile({
				rundir: rundirOf(exe),
				collection: suite.collection ? join(suite.dir, suite.collection) : null,
				replace: { BASE_URL: server.origin },
				force: has("--force"),
			});
			obs = await launchObs({
				exe, pageUrl, collection: collectionName, port: PORT, say,
				onSpawn: (handle) => { current = handle; },
			});
		}

		if (suite.cdp) {
			// Match the whole url, not just the file name: the ephemeral port makes it unique
			// to this run, so a page left over from a previous one cannot be picked up.
			const page = await waitForPage(PORT, pageUrl);
			cdp = await Cdp.connect(page.webSocketDebuggerUrl);
			await cdp.prepare({ expectUrl: pageUrl });
			say(`attached to ${page.url}`);
		}

		const ctx = { cdp, observer: server, obs, dir: suite.dir, workDir, say };
		const timeout = new Promise((_, rej) =>
			setTimeout(() => rej(new Error(`suite exceeded its ${suite.timeoutMs / 1000}s budget`)), suite.timeoutMs));

		// Enforced rather than coerced. `|| []` turned a suite that returned nothing into a
		// green run with no tests in it; a truthy non-array reached the reporting code and
		// threw from finally, losing the run instead of failing the suite; and a result with
		// an unrecognised status is emitted by the JUnit writer as a pass.
		const returned = await Promise.race([suite.run(ctx), timeout]);
		const problems = validateResults(returned);
		if (problems.length) throw new Error(problems.join("; "));
		run.results = returned;
	} catch (e) {
		run.error = String(e?.message || e);
	} finally {
		cdp?.close();
		try { server?.dump(join(workDir, "events.json")); } catch { /* nothing worth failing over */ }
		await server?.close();
		if (!keepOpen && obs) {
			obs.stop();
			// Wait for the port to actually free before the next suite's guard looks at it:
			// the CEF child processes can outlive the taskkill by a moment and keep 9123 bound.
			for (let i = 0; i < 40 && (await devtoolsUp(PORT)); i++) await sleep(250);
		}
		current = null;

		// The OBS log is the first thing anyone wants when a run goes wrong.
		if (exe && (run.error || run.results.some((r) => r.status === "fail"))) {
			const log = latestLog(join(rundirOf(exe), "config", "obs-studio", "logs"));
			if (log) run.obsLog = log;
		}
		run.ms = Date.now() - started;
	}
	return run;
}

const runs = [];
for (const name of selected) runs.push(await runSuite(name));

if (JSON_OUT) console.log(toJson(runs));
else {
	renderConsole(runs);
	for (const r of runs) if (r.obsLog) console.log(grey(`  OBS log: ${r.obsLog}`));
}

const junit = opt("--junit", null);
if (junit) writeJUnit(junit, runs);

process.exit(failed(runs) ? 1 : 0);
