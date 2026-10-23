/*
 * Does obs tell the main page what happens in it, and exactly once?
 *
 *   suite.mjs --cdp--> Streamlabs page --obs_*--> plugin --signal--> ObsEvents queue
 *        ^                                                              |
 *        '-- inbox <-- events_registerReceiver <-- grpc <-- sender thread
 *
 * Every assertion is a count. "An event arrived" would stay green while a source made one event
 * and a scene made two, which is exactly the storm this channel exists to stop. So each step
 * clears the inbox, does one thing, waits out the coalescing windows, and compares the whole
 * tally of event names with what that thing should have produced - anything not listed must be
 * zero.
 *
 * Creating a source through obs_source_create also puts it in the current scene, so that one
 * call is a sourceCreated plus a sceneItemAdded.
 */

import { results, until } from "../../harness/suite.mjs";

// Longer than the 250 ms source update and 100 ms transform windows, so a held-back trailing event has arrived
const SETTLE_MS = 800;

const COLOR = JSON.stringify({ color: 4278190335, width: 100, height: 100 });

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export default {
	name: "obs-events",
	description: "obs source, scene and scene item events reach events_registerReceiver, each exactly once",
	collection: "collection.json",
	timeoutMs: 240000,

	async run({ cdp, observer, say }) {
		const r = results();

		const clear = () => cdp.evaluate("window.__sltInbox.length = 0");
		const events = async () => (await cdp.inbox()).map((args) => JSON.parse(args[0]));
		const tally = (evs) => evs.reduce((t, e) => ({ ...t, [e.event]: (t[e.event] || 0) + 1 }), {});

		// The events are what has to match, in any order; a count of zero is the same as absent
		const exactly = (evs, expected) => {
			const got = tally(evs);
			const names = new Set([...Object.keys(got), ...Object.keys(expected)]);
			const diff = [...names].filter((n) => (got[n] || 0) !== (expected[n] || 0)).map((n) => `${n}: ${got[n] || 0}, wanted ${expected[n] || 0}`);
			return diff.length ? `${diff.join("; ")} (got ${JSON.stringify(evs).slice(0, 600)})` : undefined;
		};

		// Clears, runs the action, waits, and compares
		const happens = async (action, expected) => {
			await clear();
			await action();
			await sleep(SETTLE_MS);
			return exactly(await events(), expected);
		};

		const items = async (scene) => (await cdp.call("obs_scene_get_items", scene)).items;
		const sources = async () => cdp.call("obs_query_all_sources");

		await r.step("the main page can register for events, and a subframe cannot", async () => {
			const names = await cdp.evaluate("__slt.names()");
			if (!names.includes("events_registerReceiver")) return "events_registerReceiver is not exposed";

			await cdp.evaluate(`(() => { const f = document.createElement("iframe"); f.src = ${JSON.stringify(`${observer.origin}/frame.html`)}; document.body.appendChild(f); return true; })()`);

			if (!(await observer.waitFor((evs) => evs.some((e) => e.who === "events-frame"), { timeoutMs: 30000 }))) return "the iframe never reported";

			const frame = observer.events.find((e) => e.who === "events-frame").data;

			if (frame.isTop) return "the report came from the top frame";
			if (!frame.keys) return "the iframe has no slabsGlobal at all";
			if (frame.keys.includes("events_registerReceiver")) return "the iframe can register for events";
			if (!frame.keys.includes("sl_getVersionInfo")) return "the iframe lost the rest of slabsGlobal";
		});

		const registered = await cdp.listen("slabsGlobal", "events_registerReceiver");

		if (registered.__missing)
		{
			r.fail("register the receiver", "events_registerReceiver is not exposed");
			return r.list;
		}

		// Events are held until obs has finished loading, which this page has no way to hear about,
		// so make things until one is heard
		await r.step("events start flowing once obs has finished loading", async () => {
			let n = 0;

			const heard = await until(
				async () => {
					const name = `EvProbe${n++}`;
					await clear();
					await cdp.call("obs_source_create", "color_source_v3", name, COLOR, "{}");
					await sleep(1000);
					const got = (await events()).some((e) => e.event === "sourceCreated" && e.name === name);
					await cdp.call("obs_source_destroy", name);
					return got;
				},
				{ timeoutMs: 90000, everyMs: 500 },
			);

			if (!heard) return "no event arrived in 90 s";
			await sleep(SETTLE_MS);
		});

		let color;

		await r.step("obs_source_create: one sourceCreated, one sceneItemAdded, no scene", async () => {
			const why = await happens(() => cdp.call("obs_source_create", "color_source_v3", "EvColor1", COLOR, "{}"), { sourceCreated: 1, sceneItemAdded: 1 });
			color = (await events()).find((e) => e.event === "sourceCreated");

			if (why) return why;
			if (color.name !== "EvColor1" || color.id !== "color_source_v3" || color.isGroup !== false) return `payload ${JSON.stringify(color)}`;
			if (!color.uuid) return "no uuid";
		});

		await r.step("obs_query_all_sources carries the same uuid", async () => {
			const entry = (await sources()).find((s) => s.name === "EvColor1");

			if (!entry) return "EvColor1 is not listed";
			if (entry.uuid !== color?.uuid) return `listed uuid ${entry.uuid}, event uuid ${color?.uuid}`;
		});

		await r.step("obs_create_scene: one sceneCreated, no source", async () => {
			const why = await happens(() => cdp.call("obs_create_scene", "EvScene2"), { sceneCreated: 1 });
			if (why) return why;

			const scene = (await events()).find((e) => e.event === "sceneCreated");

			if (scene.name !== "EvScene2" || !scene.uuid) return `payload ${JSON.stringify(scene)}`;
		});

		await r.step("obs_add_transition: a private source makes no event", async () => {
			let reply;
			const why = await happens(async () => (reply = await cdp.call("obs_add_transition", "fade_transition", "EvTransition")), {});

			if (reply?.error) return `obs_add_transition: ${reply.error}`;
			return why;
		});

		let itemId;

		await r.step("obs_scene_add: one sceneItemAdded, its itemId is the one obs_scene_get_items lists", async () => {
			const why = await happens(() => cdp.call("obs_scene_add", "EvScene2", "EvColor1"), { sceneItemAdded: 1 });
			if (why) return why;

			const added = (await events()).find((e) => e.event === "sceneItemAdded");
			const listed = await items("EvScene2");

			if (typeof added.itemId !== "number") return `itemId ${JSON.stringify(added.itemId)}`;
			if (added.scene !== "EvScene2" || added.source !== "EvColor1" || added.sourceUuid !== color.uuid || !added.sceneUuid) return `payload ${JSON.stringify(added)}`;
			if (listed.length !== 1 || listed[0].id !== added.itemId) return `items ${JSON.stringify(listed)}`;

			itemId = added.itemId;
		});

		await r.step("obs_scene_get_items describes the item", async () => {
			const [item] = await items("EvScene2");
			const want = { sourceName: "EvColor1", sourceUuid: color.uuid, sourceId: "color_source_v3", visible: true, locked: false, rot: 0, isGroup: false };
			const wrong = Object.keys(want).filter((k) => item[k] !== want[k]);

			if (wrong.length) return `${wrong.join(", ")} in ${JSON.stringify(item)}`;
			if (!item.pos || !item.scale || !item.crop || !Array.isArray(item.children)) return `missing keys in ${JSON.stringify(item)}`;
		});

		await r.step("obs_sceneitem_set_visibility: one sceneItemUpdated with reason visible", async () => {
			const why = await happens(() => cdp.call("obs_sceneitem_set_visibility", "EvScene2", "EvColor1", false), { sceneItemUpdated: 1 });
			if (why) return why;

			const e = (await events())[0];

			if (e.reason !== "visible" || e.itemId !== itemId || e.scene !== "EvScene2") return `payload ${JSON.stringify(e)}`;
		});

		await r.step("50 rapid obs_sceneitem_set_pos: transform events are coalesced, and the last one is the final position", async () => {
			await clear();

			const started = Date.now();
			await cdp.evaluate("Promise.all(Array.from({length: 50}, (_, i) => __slt.call('obs_sceneitem_set_pos', 'EvScene2', 'EvColor1', i, i))).then(() => true)", { awaitPromise: true });
			const elapsedMs = Date.now() - started;

			await sleep(SETTLE_MS);

			const evs = await events();
			const transforms = evs.filter((e) => e.event === "sceneItemUpdated" && e.reason === "transform");

			say(`50 set_pos took ${elapsedMs} ms and made ${transforms.length} transform events`);

			if (evs.length !== transforms.length) return `other events: ${JSON.stringify(evs.filter((e) => !transforms.includes(e))).slice(0, 300)}`;
			if (transforms.length === 0) return "no transform event at all";

			// 10 per second, plus the one that opens the window
			const allowed = Math.ceil(elapsedMs / 100) + 1;

			if (transforms.length > Math.min(allowed, 10 * Math.max(1, Math.ceil(elapsedMs / 1000)))) return `${transforms.length} transform events in ${elapsedMs} ms`;
			if (transforms.length >= 50) return "no coalescing";

			const last = transforms.at(-1);

			if (last.pos.x !== 49 || last.pos.y !== 49) return `last event ${JSON.stringify(last)}`;
		});

		await r.step("obs_scene_get_items shows the final position", async () => {
			const [item] = await items("EvScene2");

			if (item.pos.x !== 49 || item.pos.y !== 49 || item.visible !== false) return JSON.stringify(item);
		});

		await r.step("obs_sceneitem_update applies the whole patch, visibility and locking included", async () => {
			const patch = { visible: true, locked: true, transform: { position: { x: 10, y: 20 }, scale: { x: 2, y: 3 }, rotation: 90, crop: { left: 1, top: 2, right: 3, bottom: 4 } } };

			await clear();
			const reply = await cdp.call("obs_sceneitem_update", "EvScene2", itemId, JSON.stringify(patch));

			if (reply.error) return reply.error;

			await sleep(SETTLE_MS);

			const [item] = await items("EvScene2");
			const mismatch = [
				[item.visible, true],
				[item.locked, true],
				[item.pos.x, 10],
				[item.pos.y, 20],
				[item.scale.x, 2],
				[item.scale.y, 3],
				[item.rot, 90],
				[item.crop.left, 1],
				[item.crop.top, 2],
				[item.crop.right, 3],
				[item.crop.bottom, 4],
			].filter(([got, want]) => got !== want);

			if (mismatch.length) return `${JSON.stringify(mismatch)} in ${JSON.stringify(item)}`;

			const evs = await events();
			const reasons = evs.filter((e) => e.event === "sceneItemUpdated").map((e) => e.reason);

			if (reasons.filter((x) => x === "visible").length !== 1 || reasons.filter((x) => x === "locked").length !== 1) return `reasons ${reasons.join(", ")}`;
			if (evs.length !== reasons.length) return `other events ${JSON.stringify(evs).slice(0, 300)}`;

			const last = evs.filter((e) => e.reason === "transform").at(-1);

			if (!last || last.pos.x !== 10 || last.pos.y !== 20 || last.scale.x !== 2 || last.scale.y !== 3 || last.rot !== 90) return `last transform ${JSON.stringify(last)}`;
		});

		await r.step("obs_sceneitem_update takes keys one at a time, and the flat names", async () => {
			const reply = await cdp.call("obs_sceneitem_update", "EvScene2", itemId, JSON.stringify({ locked: false, pos: { x: 5 }, rot: 0 }));

			if (reply.error) return reply.error;

			const [item] = await items("EvScene2");

			if (item.locked !== false || item.pos.x !== 5 || item.pos.y !== 20 || item.rot !== 0 || item.visible !== true || item.scale.x !== 2) return JSON.stringify(item);
		});

		await r.step("obs_sceneitem_update and obs_sceneitem_remove refuse what they cannot find", async () => {
			const bad = [
				["unknown item", await cdp.call("obs_sceneitem_update", "EvScene2", 99999, "{}")],
				["unknown scene", await cdp.call("obs_sceneitem_update", "NoSuchScene", itemId, "{}")],
				["a source, not a scene", await cdp.call("obs_sceneitem_update", "EvColor1", itemId, "{}")],
				["a patch that is not json", await cdp.call("obs_sceneitem_update", "EvScene2", itemId, "{")],
				["remove, unknown item", await cdp.call("obs_sceneitem_remove", "EvScene2", 99999)],
				["remove, unknown scene", await cdp.call("obs_sceneitem_remove", "NoSuchScene", itemId)],
			].filter(([, reply]) => typeof reply.error !== "string");

			if (bad.length) return `no error for ${bad.map(([what, reply]) => `${what} (${JSON.stringify(reply)})`).join(", ")}`;
			if ((await items("EvScene2")).length !== 1) return "an item went missing";
		});

		await r.step("obs_set_current_scene: one sceneSwitched", async () => {
			const why = await happens(() => cdp.call("obs_set_current_scene", "EvScene2"), { sceneSwitched: 1 });
			if (why) return why;

			const e = (await events())[0];

			if (e.name !== "EvScene2" || !e.uuid) return `payload ${JSON.stringify(e)}`;
		});

		r.skip("obs_source_update rename: one sourceRenamed, no created or removed", "obs_source_update is not in this build");

		await r.step("obs_source_set_settings_json: one sourceUpdated", async () => {
			const why = await happens(() => cdp.call("obs_source_set_settings_json", "EvColor1", JSON.stringify({ width: 120 })), { sourceUpdated: 1 });
			if (why) return why;

			const e = (await events())[0];

			if (e.name !== "EvColor1" || e.uuid !== color.uuid) return `payload ${JSON.stringify(e)}`;
		});

		await r.step("obs_sceneitem_remove: one sceneItemRemoved, and the source survives", async () => {
			const why = await happens(() => cdp.call("obs_sceneitem_remove", "EvScene2", itemId), { sceneItemRemoved: 1 });
			if (why) return why;

			const e = (await events())[0];

			if (e.itemId !== itemId || e.scene !== "EvScene2" || e.source !== "EvColor1") return `payload ${JSON.stringify(e)}`;
			if ((await items("EvScene2")).length !== 0) return "the item is still listed";
			if (!(await sources()).some((s) => s.name === "EvColor1")) return "the source is gone";
		});

		await r.step("re-registering replaces the receiver: the next event arrives once", async () => {
			await clear();
			await cdp.listen("slabsGlobal", "events_registerReceiver");
			await cdp.call("obs_create_scene", "EvScene3");
			await sleep(SETTLE_MS);

			const why = exactly(await events(), { sceneCreated: 1 });
			if (why) return why;
		});

		// "Scene" still holds an item of it, but obs prunes the items of a removed source when their scene is next drawn, and that scene is not the current one
		await r.step("obs_source_destroy: one sourceRemoved", async () => {
			const why = await happens(() => cdp.call("obs_source_destroy", "EvColor1"), { sourceRemoved: 1 });
			if (why) return why;

			const e = (await events()).find((x) => x.event === "sourceRemoved");

			if (e.name !== "EvColor1" || e.uuid !== color.uuid || e.id !== "color_source_v3") return `payload ${JSON.stringify(e)}`;
		});

		await r.step("obs_source_destroy of a scene: one sceneRemoved", async () => {
			const why = await happens(() => cdp.call("obs_source_destroy", "EvScene3"), { sceneRemoved: 1 });
			if (why) return why;
		});

		await r.step("a scene collection switch is one sceneCollectionChanged and nothing per source", async () => {
			const original = (await cdp.call("obs_get_current_scene_collection")).name;

			if (!original) return "no current scene collection";

			const first = await happens(() => cdp.call("obs_add_scene_collection", "EvOther"), { sceneCollectionChanged: 1 });
			if (first) return `new collection: ${first}`;

			if ((await events())[0].name !== "EvOther") return `payload ${JSON.stringify((await events())[0])}`;

			const back = await happens(() => cdp.call("obs_set_current_scene_collection", original), { sceneCollectionChanged: 1 });
			if (back) return `switch back: ${back}`;

			if ((await events())[0].name !== original) return `payload ${JSON.stringify((await events())[0])}`;
		});

		await r.step("events flow again after the switch", async () => {
			const why = await happens(() => cdp.call("obs_create_scene", "EvScene4"), { sceneCreated: 1 });
			if (why) return why;
		});

		return r.list;
	},
};
