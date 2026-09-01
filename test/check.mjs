#!/usr/bin/env node
/*
 * The checks that do not need OBS.
 *
 * Everything else here takes a fifteen-minute build and a running OBS before it can tell you
 * anything. These three run in about a second, so they gate every pull request:
 *
 *   1. every test file parses
 *   2. every suite.mjs satisfies the contract, and the files it names exist
 *   3. every plugin api function a suite calls actually exists in JavascriptApi.h
 *
 * (3) is the one that earns its place: a renamed or misspelled api call is otherwise a
 * fifteen-minute round trip to discover, and it fails as "the callback never fired", which
 * looks like a runtime bug rather than a typo.
 *
 *   node test/check.mjs        exit 1 if anything is wrong
 */

import { readdirSync, readFileSync, existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, join, relative, resolve, extname } from "node:path";

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

/* ------------------------------------------------------- 1. does it parse --- */

for (const f of files.filter((f) => [".mjs", ".js"].includes(extname(f)))) {
	try {
		execFileSync(process.execPath, ["--check", f], { stdio: "pipe" });
	} catch (e) {
		note(rel(f), `does not parse\n    ${String(e.stderr || e.message).trim().split("\n").slice(0, 3).join("\n    ")}`);
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

	for (const p of validate(suite, name)) note(rel(entry), p);
	if (!existsSync(join(dir, suite.page))) note(rel(entry), `page "${suite.page}" does not exist`);
	if (suite.collection && !existsSync(join(dir, suite.collection))) {
		note(rel(entry), `collection "${suite.collection}" does not exist`);
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
	const exempt = new Set([...suites.flatMap((s) => s.expectMissing || []), ...NOT_API]);

	// Suites only. The harness deals in api names it is handed, not ones it writes down.
	const suiteFiles = walk(SUITES_DIR).filter((f) => [".mjs", ".js", ".html"].includes(extname(f)));
	for (const f of suiteFiles) {
		const src = readFileSync(f, "utf8");
		const found = new Set();
		for (const re of CALL_PATTERNS) for (const m of src.matchAll(re)) found.add(m[1]);
		for (const name of found) {
			if (known.has(name) || exempt.has(name)) continue;
			note(rel(f), `calls "${name}", which is not in JavascriptApi.h`);
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
	`  ok  ${files.length} files parse, ${suites.length} suite${suites.length === 1 ? "" : "s"} valid` +
	(known ? `, api calls check out against ${known.size} names in JavascriptApi.h` : ""));
