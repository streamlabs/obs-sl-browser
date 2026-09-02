#!/usr/bin/env node
/*
 * The checks that do not need OBS.
 *
 * Everything else here takes a fifteen-minute build and a running OBS before it can tell you
 * anything. These three run in about a second, so they gate every pull request:
 *
 *   1. every test file parses - modules, inline page scripts and collection json alike
 *   2. every suite.mjs satisfies the contract, and the files it names exist
 *   3. every plugin api function a suite calls actually exists in JavascriptApi.h
 *
 * (3) is the one that earns its place: a renamed or misspelled api call is otherwise a
 * fifteen-minute round trip to discover, and it fails as "the callback never fired", which
 * looks like a runtime bug rather than a typo.
 *
 *   node test/check.mjs        exit 1 if anything is wrong
 */

import { readdirSync, readFileSync, existsSync, statSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { Script } from "node:vm";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, join, relative, resolve, extname, sep } from "node:path";

import { DEFAULTS, validate } from "./harness/suite.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");
const SUITES_DIR = join(HERE, "suites");
const API_HEADER = join(REPO, "JavascriptApi.h");

const problems = [];
const note = (where, what) => problems.push(`${where}: ${what}`);
const rel = (p) => relative(REPO, p).replaceAll("\\", "/");

function walk(dir, out = []) {
	if (!existsSync(dir)) return out;
	for (const e of readdirSync(dir, { withFileTypes: true })) {
		const p = join(dir, e.name);
		if (e.isDirectory()) {
			if (e.name !== ".work" && e.name !== "node_modules") walk(p, out);
		} else out.push(p);
	}
	return out;
}

const files = walk(HERE);

/*
 * A suite's page and collection must each be a regular file inside the suite directory.
 * existsSync alone lets a directory through - which then reaches readFileSync in the server -
 * and lets a ../ path through here only to be refused later by the server's root check.
 */
function suiteFile(dir, name, label) {
	const full = resolve(join(dir, name));
	if (!full.startsWith(resolve(dir) + sep)) return `${label} "${name}" is outside the suite directory`;
	if (!existsSync(full)) return `${label} "${name}" does not exist`;
	if (!statSync(full).isFile()) return `${label} "${name}" is not a file`;
	return null;
}

/* ------------------------------------------------------- 1. does it parse --- */

/*
 * Not only .mjs and .js. A suite page carries its logic in inline <script>, and a scene
 * collection is json; a syntax error in either used to reach the expensive e2e job
 * untouched. Reporting every walked file as "parsed" while checking two extensions was
 * also simply untrue, so this counts what it actually checked.
 */
let checked = 0;

for (const f of files.filter((f) => [".mjs", ".js"].includes(extname(f)))) {
	checked++;
	try {
		execFileSync(process.execPath, ["--check", f], { stdio: "pipe" });
	} catch (e) {
		note(rel(f), `does not parse\n    ${String(e.stderr || e.message).trim().split("\n").slice(0, 3).join("\n    ")}`);
	}
}

// new Script() compiles without running, so a page that touches `document` is safe to check.
const INLINE_SCRIPT = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;

for (const f of files.filter((f) => extname(f) === ".html")) {
	checked++;
	const src = readFileSync(f, "utf8");
	for (const m of src.matchAll(INLINE_SCRIPT)) {
		if (/\bsrc\s*=/i.test(m[1])) continue; // an external script, checked as its own file
		const line = src.slice(0, m.index).split("\n").length;
		try {
			new Script(m[2], { filename: `${rel(f)}:${line}` });
		} catch (e) {
			note(`${rel(f)}:${line}`, `inline script does not parse: ${e.message}`);
		}
	}
}

for (const f of files.filter((f) => extname(f) === ".json")) {
	checked++;
	try {
		// {{BASE_URL}} and friends are substituted at install time, not here.
		JSON.parse(readFileSync(f, "utf8").replace(/\{\{[A-Z_]+\}\}/g, "placeholder"));
	} catch (e) {
		note(rel(f), `is not valid JSON: ${e.message}`);
	}
}

/* ------------------------------------------------ 2. are the suites valid --- */

const suiteNames = existsSync(SUITES_DIR)
	? readdirSync(SUITES_DIR, { withFileTypes: true }).filter((d) => d.isDirectory()).map((d) => d.name).sort()
	: [];

if (!suiteNames.length) note("test/suites", "no suites found");

const suites = [];
for (const name of suiteNames) {
	const dir = join(SUITES_DIR, name);
	const entry = join(dir, "suite.mjs");
	if (!existsSync(entry)) { note(`test/suites/${name}`, "has no suite.mjs"); continue; }

	let suite;
	try {
		suite = { ...DEFAULTS, ...(await import(pathToFileURL(entry))).default, dir };
	} catch (e) {
		note(rel(entry), `could not be imported: ${e.message}`);
		continue;
	}

	const problems = validate(suite, name);
	for (const p of problems) note(rel(entry), p);
	// Only look for the files once the fields naming them are known to be strings.
	if (!problems.length) {
		for (const bad of [suiteFile(dir, suite.page, "page"),
			suite.collection ? suiteFile(dir, suite.collection, "collection") : null]) {
			if (bad) note(rel(entry), bad);
		}
	}
	suites.push(suite);
}

/* ---------------------------------------- 3. do the api calls name real api --- */

// getPluginFunctionNames() and getBrowserFunctionNames() are both {"name", JS_ENUM} tables.
function apiNames() {
	if (!existsSync(API_HEADER)) { note(rel(API_HEADER), "not found - cannot check api names"); return null; }
	const src = readFileSync(API_HEADER, "utf8");
	const names = new Set([...src.matchAll(/\{\s*"([A-Za-z0-9_]+)"\s*,\s*JS_[A-Z0-9_]+\s*\}/g)].map((m) => m[1]));
	if (names.size < 20) { note(rel(API_HEADER), `only found ${names.size} api names - has the table format changed?`); return null; }
	return names;
}

// How a suite reaches the api: cdp.call("x"), __slt.call('x'), or slabsGlobal.x(...) from a page.
const CALL_PATTERNS = [
	/\b(?:cdp|ctx\.cdp)\.call\(\s*["'`]([A-Za-z0-9_]+)["'`]/g,
	/\b__slt\.call\(\s*["'`]([A-Za-z0-9_]+)["'`]/g,
	/\bslabsGlobal\s*\.\s*([A-Za-z0-9_]+)\s*\(/g,
	/\bslabsGlobal\s*\[\s*["'`]([A-Za-z0-9_]+)["'`]\s*\]/g,
];

// Not api calls: properties the plugin puts on slabsGlobal, and the helpers' own surface.
const NOT_API = new Set(["pluginVersion"]);

const known = apiNames();
if (known) {
	// Suites only. The harness deals in api names it is handed, not ones it writes down.
	for (const suite of suites) {
		// Resolved per suite, not pooled: expectMissing is a suite-local statement about that
		// suite's own calls, and pooling it would let one suite's deliberate exemption hide the
		// same name misspelled in another.
		const exempt = new Set([...(Array.isArray(suite.expectMissing) ? suite.expectMissing : []), ...NOT_API]);

		for (const f of walk(suite.dir).filter((f) => [".mjs", ".js", ".html"].includes(extname(f)))) {
			const src = readFileSync(f, "utf8");
			const found = new Set();
			for (const re of CALL_PATTERNS) for (const m of src.matchAll(re)) found.add(m[1]);
			for (const name of found) {
				if (known.has(name) || exempt.has(name)) continue;
				note(rel(f), `calls "${name}", which is not in JavascriptApi.h`);
			}
		}
	}
}

/* ------------------------------------------------------------------ done --- */

if (problems.length) {
	console.error(`\n${problems.length} problem${problems.length > 1 ? "s" : ""}:\n`);
	for (const p of problems) console.error(`  ${p}`);
	console.error("");
	process.exit(1);
}

console.log(
	`  ok  ${checked} files parse, ${suites.length} suite${suites.length === 1 ? "" : "s"} valid` +
	(known ? `, api calls check out against ${known.size} names in JavascriptApi.h` : ""));
