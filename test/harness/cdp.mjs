/*
 * A minimal Chrome DevTools Protocol client - enough to run code inside the plugin's page.
 *
 * The api under test only exists on window.slabsGlobal in the Streamlabs browser process,
 * so tests do not reimplement it: SlBrowser.cpp already sets remote_debugging_port = 9123,
 * and this attaches there. Only Runtime.evaluate is needed.
 *
 * Node 22+ for the built-in WebSocket. No dependencies.
 */

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { sleep } from "./obs.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));

export async function listPages(port) {
	const res = await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(3000) });
	if (!res.ok) throw new Error(`DevTools returned HTTP ${res.status}`);
	return (await res.json()).filter((t) => t.type === "page" && t.webSocketDebuggerUrl);
}

/** Wait for an attachable page, preferring one whose url contains `match`. */
export async function waitForPage(port, match, timeoutMs = 60000) {
	const deadline = Date.now() + timeoutMs;
	let seen = [];
	while (Date.now() < deadline) {
		try {
			seen = await listPages(port);
			const hit = match
				? seen.find((p) => (p.url || "").toLowerCase().includes(match.toLowerCase()))
				: seen[0];
			if (hit) return hit;
		} catch { /* the port answers before any page exists; keep waiting */ }
		await sleep(500);
	}
	throw new Error(
		`no attachable page${match ? ` matching "${match}"` : ""} within ${timeoutMs / 1000}s.` +
		(seen.length ? `\nAttachable now: ${seen.map((p) => p.url).join(", ")}` : ""));
}

export class Cdp {
	#ws;
	#id = 0;
	#pending = new Map();
	#dead = null;

	static connect(url) {
		return new Promise((resolve, reject) => {
			const c = new Cdp();
			const ws = new WebSocket(url);
			c.#ws = ws;

			const onFail = (e) => reject(new Error(`DevTools websocket failed: ${e?.message || "closed"}`));
			ws.addEventListener("error", onFail, { once: true });
			ws.addEventListener("close", onFail, { once: true });

			ws.addEventListener("open", () => {
				ws.removeEventListener("error", onFail);
				ws.removeEventListener("close", onFail);

				// The connect-time handlers are gone, so without these a socket that dies
				// mid-call leaves its promise pending forever. Nothing above this settles it:
				// prepare() runs before the suite timeout starts, so the hang would only end
				// at the workflow's 90-minute limit.
				const dead = (why) => {
					c.#dead = why;
					for (const p of c.#pending.values()) p.reject(new Error(why));
					c.#pending.clear();
				};
				ws.addEventListener("error", () => dead("devtools websocket errored"), { once: true });
				ws.addEventListener("close", (ev) =>
					dead(`devtools websocket closed${ev?.code ? ` (${ev.code})` : ""} - did OBS exit?`), { once: true });

				ws.addEventListener("message", (ev) => {
					let msg;
					try { msg = JSON.parse(ev.data); } catch { return; }
					const p = c.#pending.get(msg.id);
					if (!p) return;
					c.#pending.delete(msg.id);
					msg.error ? p.reject(new Error(msg.error.message)) : p.resolve(msg.result);
				});
				resolve(c);
			}, { once: true });
		});
	}

	#send(method, params = {}) {
		if (this.#dead) return Promise.reject(new Error(this.#dead));
		const id = ++this.#id;
		return new Promise((resolve, reject) => {
			this.#pending.set(id, { resolve, reject });
			try {
				this.#ws.send(JSON.stringify({ id, method, params }));
			} catch (e) {
				this.#pending.delete(id);
				reject(e);
			}
		});
	}

	/** Returns the value, or throws with whatever the page threw. */
	async evaluate(expression, { awaitPromise = false } = {}) {
		const r = await this.#send("Runtime.evaluate", {
			expression,
			awaitPromise,
			returnByValue: true,
			allowUnsafeEvalBlockedByCSP: true,
		});
		if (r.exceptionDetails) {
			const d = r.exceptionDetails;
			throw new Error(d.exception?.description || d.text || "evaluation threw");
		}
		return r.result?.value;
	}

	/** Run a classic script in the page - a suite file, or the shared in-page helpers. */
	async inject(file) {
		await this.evaluate(readFileSync(file, "utf8"));
	}

	/**
	 * Get the page ready for a suite: wait for the document, install harness/inpage.js, wait
	 * for slabsGlobal, and prime the callback path.
	 *
	 * A target is listed - and reports its final url - before the document has committed, so
	 * injecting on first sight can land in the outgoing context and be wiped a moment later
	 * by the navigation. Hence: wait for readyState, then inject, then confirm it survived.
	 */
	async prepare({ expectUrl = null, timeoutMs = 30000 } = {}) {
		const state = () =>
			this.evaluate("({ready: document.readyState, href: location.href})").catch(() => null);

		// Falling through when this expires would inject into exactly the loading or
		// mid-navigation document the wait exists to avoid, and the helpers could then be
		// wiped after the check below said they were there. Fail instead of guessing.
		let deadline = Date.now() + timeoutMs;
		let ready = null;
		while (Date.now() < deadline) {
			ready = await state();
			if (ready?.ready === "complete" && (!expectUrl || ready.href === expectUrl)) break;
			ready = null;
			await sleep(250);
		}
		if (!ready) {
			const s = await state();
			throw new Error(
				`the page was not ready within ${timeoutMs / 1000}s ` +
				`(readyState ${s?.ready ?? "unknown"}, url ${s?.href ?? "unknown"}` +
				`${expectUrl ? `, expected ${expectUrl}` : ""}).`);
		}

		deadline = Date.now() + timeoutMs;
		let installed = false;
		while (Date.now() < deadline && !installed) {
			await this.inject(join(HERE, "inpage.js"));
			installed = (await this.evaluate("typeof window.__slt")) === "object";
			if (!installed) await sleep(250);
		}
		if (!installed) {
			const s = await state();
			throw new Error(
				`the in-page helpers did not survive injection (readyState ${s?.ready}, url ${s?.href}) - ` +
				`the page is probably still navigating.`);
		}

		deadline = Date.now() + timeoutMs;
		while (Date.now() < deadline) {
			if (await this.evaluate("__slt.available()")) {
				await this.call("sl_getVersionInfo"); // priming, see inpage.js
				return;
			}
			await sleep(500);
		}
		throw new Error(
			"window.slabsGlobal never appeared on this page. If it is not the Streamlabs " +
			"browser page, the suite's page url is wrong.");
	}

	/** Call a plugin api function and get its reply back parsed. See inpage.js for the shape. */
	call(fn, ...args) {
		const argList = [JSON.stringify(fn), ...args.map((a) => JSON.stringify(a))].join(", ");
		return this.evaluate(`__slt.call(${argList})`, { awaitPromise: true });
	}

	close() {
		try { this.#ws.close(); } catch { /* already gone */ }
	}
}
