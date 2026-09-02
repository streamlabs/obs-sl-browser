/*
 * Injected into the Streamlabs browser page before a suite runs.
 *
 * Every plugin api function takes a callback as its first argument and hands it a JSON
 * string, so calling one from a test means wrapping it in a promise, deciding what a
 * missing function or a callback that never fires looks like, and parsing the result.
 * That is the same wrapper in every suite, so it lives here.
 *
 * Classic script, no module syntax: CDP evaluates this as-is, and the same file is loadable
 * from a <script> tag by a suite page that wants to drive itself in the browser.
 */

(function () {
	"use strict";

	var CALL_TIMEOUT_MS = 15000;

	/*
	 * Resolves to the parsed reply, or to one of the diagnostic shapes below - never rejects,
	 * so a suite decides what counts as a failure:
	 *   {__missing: name}  the function is not on slabsGlobal
	 *   {__timeout: name}  the callback never fired
	 *   {__raw: string}    the reply was not JSON
	 * Setters answer with an empty string on success, which parses to {}.
	 */
	function call(fn) {
		var args = Array.prototype.slice.call(arguments, 1);
		return new Promise(function (resolve) {
			var g = window.slabsGlobal;
			if (!g || typeof g[fn] !== "function") {
				resolve({ __missing: fn });
				return;
			}
			var settled = false;
			var timer = setTimeout(function () {
				if (settled) return;
				settled = true;
				resolve({ __timeout: fn });
			}, CALL_TIMEOUT_MS);

			g[fn].apply(g, [function (json) {
				if (settled) return;
				settled = true;
				clearTimeout(timer);
				if (!json) { resolve({}); return; }
				try { resolve(JSON.parse(json)); }
				catch (e) { resolve({ __raw: String(json) }); }
			}].concat(args));
		});
	}

	/*
	 * GrpcBrowser::com_grpc_run_javascriptOnBrowser pushes to
	 * BrowserClient::GetMostRecentRenderKnown(), which - despite the name - is only ever
	 * assigned in RegisterCallback. Until this page calls some slabsGlobal function that
	 * target is null and pushes are dropped to a printf. The production Streamlabs page
	 * primes itself through its own startup calls; a test page has to do it deliberately.
	 */
	function prime() {
		return call("sl_getVersionInfo");
	}

	function names() {
		return window.slabsGlobal ? Object.keys(window.slabsGlobal) : [];
	}

	window.__slt = {
		call: call,
		prime: prime,
		names: names,
		available: function () { return !!window.slabsGlobal; },
	};
})();
