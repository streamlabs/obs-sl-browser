/*
 * browsersource_sendMessage: does a payload from the Streamlabs window reach the page running
 * in a named OBS browser source, and only that one?
 *
 *   suite.mjs  --cdp--> Streamlabs page --browsersource_sendMessage--> plugin
 *                              --javascript_event--> source.html in a browser source
 *   source.html --http /report--> the harness observer
 *
 * The channel is one way. obs-browser's javascript_event proc handler has no reply path, so
 * the receiving page cannot answer through the mechanism under test - it reports out of band
 * instead, which is also what makes a total failure show up as silence rather than as a
 * missing observer.
 *
 * What is actually being protected: source.html uses no plugin-specific helper. It listens
 * exactly the way a Streamlabs Desktop overlay does -
 *
 *     window.addEventListener('messageFromApp', e => JSON.parse(e.detail.message));
 *
 * because the bytes are the same. Desktop's patched BrowserSource::MessageToBrowser emits
 * DispatchJSEvent("messageFromApp", {"message": "<string>"}), and the plugin reproduces that
 * envelope through stock obs-browser. A page written against Desktop therefore runs here
 * unchanged, and every payload carries quotes, backslashes, newlines and non-ASCII so that a
 * regression in the envelope's escaping fails as UNPARSEABLE rather than passing quietly.
 *
 * The collection defines three sources, which is the point:
 *   MsgSourceA        visible, shutdown false  - delivery, and never seeing B's payloads
 *   MsgSourceB        visible, shutdown false  - delivery, and never seeing A's payloads
 *   MsgSourceHidden   hidden,  shutdown true   - the warning path, no live page to deliver to
 */

import { results } from "../../harness/suite.mjs";

const TARGETS = ["MsgSourceA", "MsgSourceB"];

// The apostrophe matters on its own: obs-browser splices the envelope into a
// CustomEvent('name', <json>) expression it hands to context->Eval.
const TRICKY = "quote \" apostrophe ' backslash \\ newline \n tab \t unicode ✓";

const received = (events, who) =>
	events.filter((e) => e.who === who && e.event === "RECEIVED");

export default {
	name: "source-message",
	description: "browsersource_sendMessage reaches the right browser source, and only it",
	collection: "SourceMessage.json",
	timeoutMs: 300000,

	async run({ cdp, observer, say }) {
		const r = results();

		// A browser source can take anywhere from ten seconds to over a minute to load its
		// page after OBS starts, and nothing announces when it has.
		say("waiting for the browser source pages to load");
		const loaded = await observer.waitFor(
			(evs) => TARGETS.every((t) => evs.some((e) => e.who === t && e.event === "LOADED")),
			{ timeoutMs: 180000 });

		const who = [...new Set(observer.events.map((e) => e.who))].join(", ");
		r.check("both browser source pages loaded", loaded, `only heard from: ${who || "nobody"}`);
		if (!loaded) {
			r.info("nothing downstream could run", "every later check needs a live page to deliver to");
			return r.list;
		}

		// One send per target, each payload naming its own target and carrying a nonce, so a
		// page receiving one addressed elsewhere proves javascript_event routed to the wrong
		// browser - and so a stale delivery from an earlier send cannot be mistaken for this one.
		for (const target of TARGETS) {
			const nonce = `${Date.now()}-${target}`;
			const payload = JSON.stringify({ target, nonce, tricky: TRICKY });

			const before = Object.fromEntries(TARGETS.map((t) => [t, received(observer.events, t).length]));

			await r.step(`${target}: the send is accepted`, async () => {
				const res = await cdp.call("browsersource_sendMessage", target, payload);
				if (res.__missing) return "browsersource_sendMessage is not exposed";
				if (res.__timeout) return "the callback never fired";
				if (res.error) return res.error;
				if (res.success !== true) return `expected success, got ${JSON.stringify(res)}`;
			});

			const arrived = await observer.waitFor(
				(evs) => received(evs, target).some((e) => e.data?.nonce === nonce),
				{ timeoutMs: 30000 });

			await r.step(`${target}: the payload arrives`, async () => {
				if (!arrived) return `no RECEIVED carrying nonce ${nonce} within 30s`;
			});

			await r.step(`${target}: the payload survives JSON escaping byte for byte`, async () => {
				const hit = received(observer.events, target).find((e) => e.data?.nonce === nonce);
				if (!hit) return "nothing arrived to compare";
				if (hit.data.tricky !== TRICKY) {
					return `mangled: ${JSON.stringify(hit.data.tricky)} != ${JSON.stringify(TRICKY)}`;
				}
			});

			await r.step(`${target}: the envelope is {"message": "..."} `, async () => {
				const hit = received(observer.events, target).find((e) => e.data?.nonce === nonce);
				if (!hit) return "nothing arrived to inspect";
				const keys = hit.data.detailKeys || [];
				if (!keys.includes("message")) return `detail keys were ${JSON.stringify(keys)}`;
			});

			// The routing assertion, and the reason each source gets its own send: nobody else
			// may have received anything while this one did.
			await r.step(`${target}: no other source received it`, async () => {
				const others = TARGETS.filter((t) => t !== target);
				const noisy = others.filter((t) => received(observer.events, t).length > before[t]);
				if (noisy.length) return `also delivered to ${noisy.join(", ")}`;
			});
		}

		await r.step("nothing anywhere failed to parse", async () => {
			const bad = observer.events.filter((e) => e.event === "UNPARSEABLE");
			if (bad.length) return `${bad.length} unparseable: ${JSON.stringify(bad[0].data).slice(0, 200)}`;
		});

		/* --------------------------------------------------------- error paths --- */

		const probe = async (label, args, expect) =>
			r.step(label, async () => {
				const res = await cdp.call("browsersource_sendMessage", ...args);
				if (res.__timeout) return "the callback never fired";
				return expect(res);
			});

		await probe("an unknown source name is an error", ["NoSuchSource", "{}"],
			(res) => (res.error ? undefined : `expected an error, got ${JSON.stringify(res)}`));

		await probe("a source that is not a browser source is an error", ["Scene", "{}"],
			(res) => (res.error ? undefined : `expected an error, got ${JSON.stringify(res)}`));

		await probe("an empty source name is an error", ["", "{}"],
			(res) => (res.error ? undefined : `expected an error, got ${JSON.stringify(res)}`));

		// Delivery is best effort: obs-browser drops the event when the source has no live
		// page, so this reports success with a warning rather than failing.
		await probe("a source with no live page warns rather than fails", ["MsgSourceHidden", "{}"],
			(res) => {
				if (res.error) return `expected a warning, got error: ${res.error}`;
				if (res.success !== true) return `expected success, got ${JSON.stringify(res)}`;
				if (!res.warning) return "expected a warning field explaining nothing was delivered";
			});

		// There is deliberately no malformed-payload probe: the third argument is an opaque
		// string and the plugin builds the JSON envelope around it, so there is nothing a
		// caller can pass that is invalid.

		return r.list;
	},
};
