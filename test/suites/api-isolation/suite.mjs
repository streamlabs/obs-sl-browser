/*
 * slabsGlobal belongs to the top-level page on the configured origin and nothing else.
 *
 * The harness server answers on both 127.0.0.1 and localhost, which are different origins,
 * so "same origin" and "other origin" can both be served from here. Frames report what they
 * see over HTTP: a frame has no slabsGlobal to ask through.
 */

import { results, until } from "../../harness/suite.mjs";

export default {
	name: "api-isolation",
	description: "slabsGlobal is bound only to the top-level page on the configured origin",
	timeoutMs: 120000,

	async run({ cdp, observer }) {
		const r = results();
		const port = new URL(observer.origin).port;
		const otherOrigin = `http://localhost:${port}`;

		const probe = (who) => observer.events.find((e) => e.who === who && e.event === "PROBE")?.data;

		const embed = (label, origin) =>
			cdp.evaluate(`(() => {
				const f = document.createElement("iframe");
				f.src = ${JSON.stringify(origin)} + "/frame.html?label=${label}";
				document.body.append(f);
			})()`);

		await r.step("the main frame has the full api", async () => {
			const v = await cdp.call("sl_getVersionInfo");
			if (v.__missing || v.__timeout || v.error) return `sl_getVersionInfo failed: ${JSON.stringify(v)}`;
			const names = await cdp.evaluate("__slt.names()");
			if (!Array.isArray(names) || names.length < 50) return `only ${names?.length} names exposed`;
		});

		await r.step("a same-origin iframe has no slabsGlobal", async () => {
			await embed("same-origin-frame", observer.origin);
			if (!(await observer.waitFor(() => probe("same-origin-frame"), { timeoutMs: 20000 }))) return "the frame never reported";
			const p = probe("same-origin-frame");
			if (p.top) return "the probe was not running in a frame";
			if (p.slabsGlobal !== "undefined") return `slabsGlobal is ${p.slabsGlobal}`;
		});

		await r.step("a cross-origin iframe has no slabsGlobal", async () => {
			await embed("cross-origin-frame", otherOrigin);
			if (!(await observer.waitFor(() => probe("cross-origin-frame"), { timeoutMs: 20000 }))) return "the frame never reported";
			const p = probe("cross-origin-frame");
			if (p.origin !== otherOrigin) return `frame ran on ${p.origin}`;
			if (p.slabsGlobal !== "undefined") return `slabsGlobal is ${p.slabsGlobal}`;
		});

		await r.step("the main frame still has the api next to the frames", async () => {
			const v = await cdp.call("sl_getVersionInfo");
			if (v.__missing || v.__timeout || v.error) return `sl_getVersionInfo failed: ${JSON.stringify(v)}`;
		});

		await r.step("after navigating to another origin slabsGlobal is absent", async () => {
			await cdp.evaluate(`location.href = ${JSON.stringify(otherOrigin + "/frame.html?label=navigated-main")}`);
			if (!(await observer.waitFor(() => probe("navigated-main"), { timeoutMs: 20000 }))) return "the page never reported";
			const p = probe("navigated-main");
			if (!p.top || p.origin !== otherOrigin) return `unexpected probe ${JSON.stringify(p)}`;
			if (p.slabsGlobal !== "undefined") return `slabsGlobal is ${p.slabsGlobal}`;
		});

		await r.step("returning to the configured origin binds the api again", async () => {
			const page = observer.pageUrl("page.html");
			await cdp.evaluate(`location.href = ${JSON.stringify(page)}`);
			await until(async () => (await cdp.evaluate("location.href").catch(() => null)) === page, { timeoutMs: 20000 });
			await cdp.prepare({ expectUrl: page });
			const v = await cdp.call("sl_getVersionInfo");
			if (v.__missing || v.__timeout || v.error) return `sl_getVersionInfo failed: ${JSON.stringify(v)}`;
		});

		return r.list;
	},
};
