/*
 * Does the plugin come up at all?
 *
 * This suite asserts nothing about any particular feature. It exists so that the harness
 * itself is under test: OBS starts, the DevTools port opens, our page loads, slabsGlobal is
 * injected, and a call through it reaches the plugin and comes back. When a feature suite
 * goes red and this one is green, the feature is broken. When both are red, the plumbing is.
 *
 * Everything here has been true since long before the test system existed, so this is also
 * the suite that has to stay green on main.
 */

import { results, until } from "../../harness/suite.mjs";

export default {
	name: "smoke",
	description: "the plugin's browser comes up and answers",
	timeoutMs: 90000,
	// Called deliberately below to prove an absent function is reported as absent.
	expectMissing: ["definitely_not_a_real_api_function"],

	async run({ cdp, observer }) {
		const r = results();

		await r.step("the page was served by the harness", async () => {
			const served = observer.events.filter((e) => e.event === "served").map((e) => e.data);
			if (!served.some((p) => String(p).includes("page.html"))) return `served: ${served.join(", ") || "nothing"}`;
		});

		await r.step("slabsGlobal is injected", async () => {
			if ((await cdp.evaluate("typeof window.slabsGlobal")) !== "object") return "not an object";
		});

		await r.step("the api surface is populated", async () => {
			const names = await cdp.evaluate("__slt.names()");
			// A number rather than a list: the exact surface is the feature suites' business,
			// and pinning it here would make every new api call fail this suite.
			if (!Array.isArray(names) || names.length < 50) return `only ${names?.length} names exposed`;
		});

		await r.step("pluginVersion is set", async () => {
			const v = await cdp.evaluate("window.slabsGlobal.pluginVersion");
			if (!v) return "empty";
			r.info("pluginVersion", String(v));
		});

		// The first call that proves the whole path, not just V8: the callback comes back
		// through the grpc bridge from the plugin process.
		await r.step("sl_getVersionInfo round-trips", async () => {
			const v = await cdp.call("sl_getVersionInfo");
			if (v.__missing) return "sl_getVersionInfo is not exposed";
			if (v.__timeout) return "the callback never fired";
			if (!v.rev && !v.branch && !v.git_sha) return `unexpected shape: ${JSON.stringify(v).slice(0, 200)}`;
			r.info("version", JSON.stringify(v));
		});

		await r.step("obs_enum_scenes answers with the seeded collection", async () => {
			// Scene collections load asynchronously, so this is the one call worth waiting on.
			const scenes = await until(async () => {
				const res = await cdp.call("obs_enum_scenes");
				return Array.isArray(res) ? res : null;
			}, { timeoutMs: 20000 });

			if (!scenes) return "obs_enum_scenes never returned an array";
			r.info("scenes", scenes.map((s) => s?.name).filter(Boolean).join(", ") || "(none)");
		});

		await r.step("an unknown api function is not silently accepted", async () => {
			const res = await cdp.call("definitely_not_a_real_api_function");
			if (!res.__missing) return `expected it to be absent, got ${JSON.stringify(res).slice(0, 120)}`;
		});

		return r.list;
	},
};
