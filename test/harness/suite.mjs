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

export function validate(suite, dirName) {
	const problems = [];
	for (const k of REQUIRED) if (!suite?.[k]) problems.push(`missing "${k}"`);
	if (suite?.name && suite.name !== dirName) problems.push(`name "${suite.name}" does not match directory "${dirName}"`);
	if (suite?.run && typeof suite.run !== "function") problems.push(`"run" is not a function`);
	if (suite?.timeoutMs && typeof suite.timeoutMs !== "number") problems.push(`"timeoutMs" is not a number`);
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
