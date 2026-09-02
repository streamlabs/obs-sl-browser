/*
 * Turning results into something a person or a CI job can read.
 *
 * A run is { suite, results[], error?, ms }. Results are { name, status, why }.
 */

import { writeFileSync } from "node:fs";

const useColor = process.stdout.isTTY && !process.env.NO_COLOR;
const paint = (code, s) => (useColor ? `\x1b[${code}m${s}\x1b[0m` : s);
export const green = (s) => paint("32", s);
export const red = (s) => paint("31", s);
export const grey = (s) => paint("90", s);
export const bold = (s) => paint("1", s);

export const counts = (runs) => ({
	pass: runs.flatMap((r) => r.results).filter((r) => r.status === "pass").length,
	fail: runs.flatMap((r) => r.results).filter((r) => r.status === "fail").length,
	skip: runs.flatMap((r) => r.results).filter((r) => r.status === "skip").length,
});

export const failed = (runs) => runs.some((r) => r.error || r.results.some((x) => x.status === "fail"));

export function renderConsole(runs) {
	for (const run of runs) {
		console.log(`\n  ${bold(run.suite)}  ${grey(run.description || "")}`);
		if (run.error) console.log(`  ${red("ERROR")} ${run.error}`);
		for (const r of run.results) {
			if (r.status === "pass") console.log(`  ${green("PASS")}  ${r.name}`);
			else if (r.status === "fail") {
				console.log(`  ${red("FAIL")}  ${bold(r.name)}`);
				if (r.why) console.log(`        ${red(r.why)}`);
			} else console.log(`  ${grey("····")}  ${grey(r.name + (r.why ? " — " + r.why : ""))}`);
		}
	}
	const c = counts(runs);
	const errored = runs.filter((r) => r.error).length;
	// A suite that never started has no failing result of its own, so say so explicitly -
	// otherwise the tally reads green while the exit code says otherwise.
	const line = `${c.pass} passed, ${c.fail} failed`
		+ (c.skip ? `, ${c.skip} skipped` : "")
		+ (errored ? `, ${errored} suite${errored > 1 ? "s" : ""} did not run` : "");
	console.log("\n  " + (failed(runs) ? red(bold(line)) : green(bold(line))) + "\n");
}

export const toJson = (runs) => JSON.stringify({ ok: !failed(runs), ...counts(runs), runs }, null, 2);

const xml = (s) =>
	String(s).replace(/[<>&"']/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", '"': "&quot;", "'": "&apos;" }[c]));

export function writeJUnit(path, runs) {
	const suites = runs.map((run) => {
		// "info" results are diagnostics a suite recorded, not assertions. Counting them as
		// passing tests inflates the total and claims coverage that was never asserted, so
		// they go to system-out instead.
		const notes = run.results.filter((r) => r.status === "info");
		const cases = run.results.filter((r) => r.status !== "info").map((r) => {
			const head = `<testcase classname="${xml(run.suite)}" name="${xml(r.name)}">`;
			if (r.status === "fail") return `${head}<failure message="${xml(r.why || "failed")}"/></testcase>`;
			if (r.status === "skip") return `${head}<skipped/></testcase>`;
			return `${head}</testcase>`;
		});
		// A suite that never got to run has no failing case of its own, so give it one -
		// otherwise a harness-level error reports as a green build with nothing in it.
		if (run.error) {
			cases.unshift(
				`<testcase classname="${xml(run.suite)}" name="suite did not run">` +
				`<failure message="${xml(run.error)}"/></testcase>`);
		}
		const total = cases.length;
		if (notes.length) {
			cases.push(`<system-out>${xml(notes.map((n) => `${n.name}: ${n.why}`).join("\n"))}</system-out>`);
		}
		const c = counts([run]);
		return `  <testsuite name="${xml(run.suite)}" tests="${total}" ` +
			`failures="${c.fail + (run.error ? 1 : 0)}" skipped="${c.skip}" time="${(run.ms || 0) / 1000}">\n` +
			cases.map((c2) => "    " + c2).join("\n") + "\n  </testsuite>";
	});
	writeFileSync(path, `<?xml version="1.0" encoding="UTF-8"?>\n<testsuites>\n${suites.join("\n")}\n</testsuites>\n`);
}
