/*
 * What a suite is, and the two helpers every suite needs.
 *
 * A suite is a directory under test/suites/ with a suite.mjs that default-exports:
 *
 *   name         string, matches the directory name
 *   description  one line, shown in --list
 *   page         html file in the suite directory, served by the harness and pointed at by
 *                SL_PLUGIN_DEFAULT_URL (default "page.html")
 *   collection   optional scene collection .json, installed and selected. {{BASE_URL}} in it
 *                is replaced with the harness server's origin
 *   cdp          false if the suite does not need a DevTools attachment (default true)
 *   expectMissing api names the suite calls on purpose that do not exist - so test/check.mjs
 *                does not report them as typos
 *   timeoutMs    how long run() gets before it is abandoned (default 180000)
 *   run(ctx)     async, returns an array of results
 *
 * ctx is { cdp, observer, obs, dir, workDir, say }.
 */

export const DEFAULTS = { page: "page.html", cdp: true, timeoutMs: 180000, expectMissing: [] };

const REQUIRED = ["name", "run"];

/**
 * Every field is checked, not just the required ones. A suite that is wrong should say so
 * here, where the message names the field - not later, where a bad `page` surfaces as a
 * path-join TypeError from somewhere in the harness.
 */
export function validate(suite, dirName) {
	const problems = [];
	const bad = (k, want) => problems.push(`"${k}" must be ${want}`);

	for (const k of REQUIRED) if (!suite?.[k]) problems.push(`missing "${k}"`);
	if (!suite) return problems;

	if (suite.name && suite.name !== dirName) problems.push(`name "${suite.name}" does not match directory "${dirName}"`);
	if (suite.run && typeof suite.run !== "function") bad("run", "a function");
	if (suite.description !== undefined && typeof suite.description !== "string") bad("description", "a string");
	if (typeof suite.page !== "string" || !suite.page) bad("page", "a non-empty string");
	if (suite.collection !== undefined && suite.collection !== null && typeof suite.collection !== "string") {
		bad("collection", "a string, or absent");
	}
	if (typeof suite.cdp !== "boolean") bad("cdp", "a boolean");
	if (!Number.isFinite(suite.timeoutMs) || suite.timeoutMs <= 0) bad("timeoutMs", "a positive number of milliseconds");
	if (!Array.isArray(suite.expectMissing) || suite.expectMissing.some((n) => typeof n !== "string")) {
		bad("expectMissing", "an array of strings");
	}
	return problems;
}

export const STATUSES = ["pass", "fail", "skip", "info"];

/**
 * Check what a suite returned before any of it is counted or reported.
 *
 * An unrecognised status is the dangerous case, not a cosmetic one: counts() and failed()
 * both match on the exact strings, so a result carrying status "failed" is counted as
 * neither a pass nor a failure, and the JUnit writer emits it as a passing testcase. A suite
 * reporting a genuine failure that way would produce a green run.
 */
export function validateResults(list) {
	if (!Array.isArray(list)) {
		return [`run() must return an array of results, got ${list === undefined ? "undefined" : JSON.stringify(list)?.slice(0, 120)}`];
	}
	const problems = [];
	list.forEach((r, i) => {
		if (!r || typeof r !== "object" || Array.isArray(r)) {
			problems.push(`result ${i} is not an object: ${JSON.stringify(r)?.slice(0, 80)}`);
			return;
		}
		if (typeof r.name !== "string" || !r.name.trim()) problems.push(`result ${i} has no name`);
		if (!STATUSES.includes(r.status)) {
			problems.push(`result ${i} ("${r.name}") has status ${JSON.stringify(r.status)}, expected one of ${STATUSES.join(", ")}`);
		}
	});
	return problems;
}

/**
 * Collects results. A suite builds one and returns its .list.
 *
 *   const r = results();
 *   r.check("scene was created", scenes.includes("X"), `got ${scenes}`);
 *   return r.list;
 */
export function results() {
	const list = [];
	const add = (status) => (name, why) => { list.push({ name, status, why: why || "" }); return list.at(-1); };
	return {
		list,
		pass: add("pass"),
		fail: add("fail"),
		skip: add("skip"),
		info: add("info"),
		/** Records a pass or a fail. `why` is only shown when it fails, so say what went wrong. */
		check(name, ok, why) {
			return ok ? this.pass(name) : this.fail(name, why);
		},
		/** Wraps a step that may throw, so one broken assertion does not abandon the rest. */
		async step(name, fn) {
			try {
				const why = await fn();
				return typeof why === "string" ? this.fail(name, why) : this.pass(name);
			} catch (e) {
				return this.fail(name, String(e?.message || e).split("\n")[0]);
			}
		},
	};
}

/**
 * Poll until `fn` returns something truthy. e2e work is mostly waiting: a browser source can
 * take anywhere from ~10s to over a minute to load its page after OBS starts, and nothing
 * announces when it has.
 */
export async function until(fn, { timeoutMs = 30000, everyMs = 500 } = {}) {
	const deadline = Date.now() + timeoutMs;
	for (;;) {
		const v = await fn();
		if (v) return v;
		if (Date.now() >= deadline) return null;
		await new Promise((r) => setTimeout(r, everyMs));
	}
}
