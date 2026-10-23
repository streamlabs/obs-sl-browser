/*
 * The vertical-canvas JS API.
 *
 * An integration suite, not a unit one: the api only exists inside the Streamlabs browser
 * process, and the behaviour worth checking - canvas namespacing, scene ownership, output
 * arbitration - is only true with libobs up.
 *
 * The assertions live in tests.js, which runs in the page. Two things drive that same file:
 * this driver injects it over CDP, and page.html loads it with a script tag so the suite can
 * also be run by hand in the browser. Keeping the assertions in one classic script is what
 * makes both work.
 *
 * The isolation invariants are the point - they are what the trailing `canvas` argument rests
 * on, and what silently breaks if scene resolution regresses:
 *
 *   - obs_enum_scenes with no canvas argument returns what it did before a vertical scene existed
 *   - a vertical scene never appears in the main scene list
 *   - the same scene name can exist on both canvases
 *   - an unrecognised canvas name errors and creates nothing on either canvas
 *
 * Plus scene-item position round-trip, canvas dimensions, stream-settings round-trip and
 * partial update, enhanced_broadcasting refusing startStream, and size alignment.
 *
 * Safety: scenes are created with a __slt_ prefix and removed afterwards; canvas size, output
 * mode and stream settings are snapshotted and restored, empty originals included.
 *
 * Two startStream calls, neither of which reaches a real endpoint. One is asserted to be refused
 * in enhanced_broadcasting mode. The other genuinely starts an output, against
 * rtmp://127.0.0.1:1/none where nothing is listening, to check a resize is refused while it runs -
 * so a manual run does briefly bring an output up and stop it again. The settings tests write
 * rtmp://127.0.0.1 and never start it.
 */

import { join } from "node:path";
import { results } from "../../harness/suite.mjs";

export default {
	name: "dual-output-api",
	description: "the vertical canvas api, and the isolation it rests on",
	collection: "DualOutput.json",
	timeoutMs: 240000,

	async run({ cdp, dir, say }) {
		const r = results();

		await cdp.inject(join(dir, "tests.js"));
		if ((await cdp.evaluate("typeof globalThis.__slDualTests")) !== "object") {
			r.fail("the suite installed itself in the page", "__slDualTests is not defined after injection");
			return r.list;
		}

		say(`running ${await cdp.evaluate("__slDualTests.count")} in-page tests`);

		// tests.js reports {pass, fail, total, results:[{status, name, why}]} - the same shape
		// this harness uses, so the results pass straight through. Its cleanup runs even when
		// tests fail, and reports what it removed as an info row.
		const summary = await cdp.evaluate("__slDualTests.run()", { awaitPromise: true });
		if (!summary || !Array.isArray(summary.results)) {
			r.fail("the in-page suite returned results", `got ${JSON.stringify(summary).slice(0, 200)}`);
			return r.list;
		}

		return summary.results;
	},
};
