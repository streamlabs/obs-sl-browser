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

// How long a misrouted copy is given to show up after the addressee's report. Both pages
// report over their own HTTP request to the same local server, so the skew being covered is
// scheduling, not the network.
const SETTLE_MS = 2000;

const settle = (ms) => new Promise((r) => setTimeout(r, ms));

const received = (events, who) =>
	events.filter((e) => e.who === who && e.event === "RECEIVED");

export default {
	name: "source-message",
	description: "browsersource_sendMessage reaches the right browser source, and only it",
	collection: "SourceMessage.json",
	timeoutMs: 300000,

	async run({ cdp, observer, say }) {
		const r = results();

		// nonce -> the source the payload was addressed to. Routing is judged against this
		// rather than against the target field that came back, so a delivery cannot clear the
		// check by arriving with a damaged or missing target - and the error-path probes,
		// whose "{}" carries no nonce, are correctly not treated as misrouted.
		const sent = new Map();

		const strays = (events) =>
			events.filter((e) => {
				if (e.event !== "RECEIVED") return false;
				const addressee = sent.get(e.data?.nonce);
				return addressee !== undefined && addressee !== e.who;
			});

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

			sent.set(nonce, target);

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

			// The undecoded string, not the fields parsed out of it. The third argument is
			// opaque to the plugin, so anything that changes its bytes is a regression even
			// when it happens to parse the same: an escaped non-ASCII character in place of the
			// literal, a reserialization, a change in spacing. Comparing fields passes all three.
			await r.step(`${target}: the payload survives byte for byte`, async () => {
				const hit = received(observer.events, target).find((e) => e.data?.nonce === nonce);
				if (!hit) return "nothing arrived to compare";
				if (hit.data.raw !== payload) {
					return `mangled - got ${JSON.stringify(hit.data.raw)}, sent ${JSON.stringify(payload)}`;
				}
			});

			// The exact key set, not just the presence of "message": an extra field in the
			// wrapper is a change to the contract a Desktop overlay is written against.
			await r.step(`${target}: the envelope is {"message": "..."} `, async () => {
				const hit = received(observer.events, target).find((e) => e.data?.nonce === nonce);
				if (!hit) return "nothing arrived to inspect";
				const keys = [...(hit.data.detailKeys || [])].sort();
				if (keys.length !== 1 || keys[0] !== "message") {
					return `detail keys were ${JSON.stringify(keys)}, expected exactly ["message"]`;
				}
			});

			// The routing assertion, and the reason each send carries its own nonce: a copy
			// landing on another source is identified by which send it belongs to, not by a
			// count taken around this send. Each page reports over its own HTTP request, so a
			// misrouted copy can arrive after the addressee did - hence the settle before the
			// absence is believed, and the sweep after the loop for anything later still.
			await settle(SETTLE_MS);

			await r.step(`${target}: no other source received it`, async () => {
				const noisy = strays(observer.events)
					.filter((e) => e.data?.nonce === nonce)
					.map((e) => e.who);
				if (noisy.length) return `also delivered to ${[...new Set(noisy)].join(", ")}`;
			});
		}

		// The loop's per-send check can only see as far as its own settle window. This catches
		// a copy that arrived during a later send, or after the last one.
		await settle(SETTLE_MS);

		await r.step("no payload ever reached a source it was not addressed to", async () => {
			const bad = strays(observer.events);
			if (bad.length) {
				return bad.map((e) => `${sent.get(e.data.nonce)} -> ${e.who}`).join(", ");
			}
		});

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

		// Opaque does not mean untyped. json11's string_value() answers "" for a missing or
		// non-string param3, so without a type check these two would deliver an empty message
		// and report success - on a one-way api, where the return value is the only signal
		// the caller ever gets.
		await probe("an omitted message is an error", ["MsgSourceA"],
			(res) => (res.error ? undefined : `expected an error, got ${JSON.stringify(res)}`));

		await probe("a message that is not a string is an error", ["MsgSourceA", 42],
			(res) => (res.error ? undefined : `expected an error, got ${JSON.stringify(res)}`));

		// The other side of that check: "" is a string, and sending it is a caller's business.
		// Addressed to the source with no live page, so asserting this delivers nothing.
		await probe("an explicitly empty message is accepted", ["MsgSourceHidden", ""],
			(res) => (res.error ? `expected success, got error: ${res.error}` : undefined));

		// There is deliberately no malformed-payload probe beyond that: past the type check the
		// third argument is opaque, and the plugin builds the JSON envelope around it, so there
		// is nothing further a caller can pass that is invalid.

		return r.list;
	},
};
