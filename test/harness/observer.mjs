/*
 * The suite's web server, and its out-of-band observation channel.
 *
 * Two jobs, one server:
 *
 *   serving   Suite pages are served over http rather than opened as file:// - CEF is far
 *             less awkward about http, and a browser source can only load a url anyway.
 *
 *   recording Some things cannot be asserted from the page that caused them. A message sent
 *             to a browser source lands in a different CEF process with no reply path, so
 *             the receiving page reports here instead, over a channel that is deliberately
 *             not the one under test: total failure of the mechanism shows up as silence in
 *             this log rather than as a missing observer.
 *
 * Pages address it as location.origin, so nothing hardcodes a port.
 */

import { createServer } from "node:http";
import { readFileSync, existsSync, statSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, extname, join, resolve, sep } from "node:path";

const HARNESS_DIR = dirname(fileURLToPath(import.meta.url));

const TYPES = {
	".html": "text/html; charset=utf-8",
	".js": "text/javascript; charset=utf-8",
	".css": "text/css; charset=utf-8",
	".json": "application/json; charset=utf-8",
	".png": "image/png",
};

const under = (dir, file) => file === dir || file.startsWith(dir + sep);

/**
 * @param {string} dir   the suite directory, served at /
 * @param {number} port  0 for an ephemeral port, which is the default and avoids collisions
 */
export async function startServer({ dir, port = 0, onEvent = () => {} }) {
	const root = resolve(dir);
	const events = [];
	const waiters = new Set();

	function record(who, event, data) {
		const e = { t: new Date().toISOString(), who, event, data };
		events.push(e);
		onEvent(e);
		for (const w of [...waiters]) w();
		return e;
	}

	// Nothing a page sends may end the run. Wraps the deferred callbacks too, not just the
	// synchronous path: a throw inside req.on("end") lands on the event loop long after any
	// try/catch around handle() has returned, and would be an uncaught exception.
	const guard = (req, res) => (fn) => {
		try {
			fn();
		} catch (e) {
			record("server", "handler-threw", { url: req?.url, error: String(e?.message || e) });
			try { res.writeHead(500, { "Content-Type": "text/plain" }).end("harness server error"); } catch { /* already sent */ }
		}
	};

	const server = createServer((req, res) => guard(req, res)(() => handle(req, res)));

	function handle(req, res) {
		const url = new URL(req.url, `http://${req.headers.host}`);

		const send = (code, type, body) => {
			res.writeHead(code, {
				"Content-Type": type,
				"Access-Control-Allow-Origin": "*",
				"Access-Control-Allow-Headers": "content-type",
				"Cache-Control": "no-store",
			});
			res.end(body);
		};

		if (req.method === "OPTIONS") return send(204, "text/plain", "");

		if (req.method === "POST" && url.pathname === "/report") {
			let body = "";
			req.on("data", (c) => (body += c));
			req.on("end", () => guard(req, res)(() => {
				// Parsing is not enough: "null", "42" and "[]" are all valid JSON, and reading
				// .who off any of them throws. Only an object is a report.
				let parsed;
				try { parsed = JSON.parse(body); } catch { parsed = undefined; }
				if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) {
					record("?", "report-unusable", String(body).slice(0, 500));
					return send(200, "application/json", '{"ok":false}');
				}
				record(String(parsed.who ?? "?"), String(parsed.event ?? "?"), parsed.data);
				send(200, "application/json", '{"ok":true}');
			}));
			return;
		}

		// Handy when poking at a run by hand; suites read ctx.observer.events instead.
		if (url.pathname === "/events") return send(200, TYPES[".json"], JSON.stringify(events, null, 2));

		// /_harness/* exposes the shared in-page helpers to a page that drives itself.
		const file = url.pathname.startsWith("/_harness/")
			? resolve(join(HARNESS_DIR, url.pathname.slice("/_harness/".length)))
			: resolve(join(root, url.pathname === "/" ? "page.html" : url.pathname.slice(1)));

		// isFile, not just exists: reading a directory throws EISDIR from inside this handler,
		// which would take the whole run down rather than answering the request.
		if (!(under(root, file) || under(HARNESS_DIR, file)) || !existsSync(file) || !statSync(file).isFile()) {
			return send(404, "text/plain", "not found");
		}
		record("server", "served", url.pathname + url.search);
		send(200, TYPES[extname(file)] || "application/octet-stream", readFileSync(file));
	}

	await new Promise((r) => server.listen(port, "127.0.0.1", r));
	const origin = `http://127.0.0.1:${server.address().port}`;

	return {
		origin,
		events,
		pageUrl: (file = "page.html") => `${origin}/${file}`,

		/** Reports matching a predicate, newest last. */
		reports(pred = () => true) {
			return events.filter((e) => e.who !== "server" && pred(e));
		},

		/** Wait until `pred(events)` is true, or give up. Resolves to true/false, never throws. */
		waitFor(pred, { timeoutMs = 60000 } = {}) {
			if (pred(events)) return Promise.resolve(true);
			return new Promise((resolve_) => {
				const done = (v) => { waiters.delete(check); clearTimeout(timer); clearInterval(tick); resolve_(v); };
				const check = () => { if (pred(events)) done(true); };
				const timer = setTimeout(() => done(false), timeoutMs);
				const tick = setInterval(check, 250); // for predicates over elapsed time, not just arrivals
				waiters.add(check);
			});
		},

		dump(path) {
			writeFileSync(path, JSON.stringify(events, null, 2));
		},

		close() {
			return new Promise((r) => server.close(r));
		},
	};
}
