/*
 * The tabs_* api: extra native Streamlabs windows that the main page opens and drives.
 *
 *   suite.mjs --cdp--> main page (slabsGlobal) --tabs_*--> sl-browser proxy --> tab window
 *   suite.mjs --cdp--> tab page  (slabsTab)    --tab_*---> sl-browser proxy --> main page
 *   tab.html  --http /report--> the harness observer
 *
 * What is actually being protected:
 *
 *   privilege   a tab window gets slabsTab and nothing else. If it ever sees slabsGlobal it can
 *               drive OBS and the filesystem, so this is asserted from the tab's own report and
 *               again through a CDP client attached to the tab.
 *   routing     a string goes to the browser it was addressed to, byte for byte, exactly once.
 *   per window  resizing a tab resizes that tab, not the main window, and to the size asked for.
 *   lifecycle   destroying a window, or the user closing it, removes it everywhere.
 *
 * Tab windows open on the real desktop, so nothing else should be driven interactively while
 * this runs.
 */

import { writeFileSync } from "node:fs";
import { join } from "node:path";

import { results, until } from "../../harness/suite.mjs";
import { attachTo, listPages } from "../../harness/cdp.mjs";
import { windowTitles, closeWindow } from "../../harness/windows.mjs";

const TABS_NAMES = [
	"tabs_createWindow", "tabs_destroyWindow", "tabs_resizeWindow", "tabs_loadUrl", "tabs_executeJs",
	"tabs_hideWindow", "tabs_showWindow", "tabs_getIsWindowHidden", "tabs_getWindowCefId",
	"tabs_queryAll", "tabs_setIcon", "tabs_setTitle", "tabs_sendStringToTab", "tabs_registerMsgReceiver",
];

const TAB_KEYS = ["pluginVersion", "tab_registerMsgReceiver", "tab_sendStringToMain"];

// What the main receiver is given when the user closes a tab window, see kTabClosedMessage.
const TAB_CLOSED = '{"event":"tabClosed"}';

// Quotes, a backslash, a newline and non-ASCII: anything that re-encodes the string fails.
const TRICKY = "quote \" apostrophe ' backslash \\ newline \n tab \t unicode ✓";

// How long a misrouted or duplicated message is given to show up. Pages report and receive over
// separate paths, so the skew being covered is scheduling.
const SETTLE_MS = 2000;

// A 1x1 png, for tabs_setIcon.
const PNG = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==", "base64");

const settle = (ms) => new Promise((r) => setTimeout(r, ms));

const isError = (res) => res && typeof res.error === "string" && res.error.length > 0;

export default {
	name: "multi-browser",
	description: "tabs_* windows: privilege boundary, messaging, per-window resize, lifecycle",
	timeoutMs: 420000,
	// Called through slabsTab on purpose, to prove a tab cannot reach it.
	expectMissing: ["tabs_destroyWindow"],

	async run({ cdp, observer, port, workDir }) {
		const r = results();

		const created = new Set();
		const tabCdp = new Map();
		const tabUrl = (uid, extra = "") => `${observer.origin}/tab.html?uid=${uid}${extra}`;
		const tabEvents = (uid, event) => observer.events.filter((e) => e.who === `tab${uid}` && e.event === event);
		const lastState = (uid) => tabEvents(uid, "RESIZED").concat(tabEvents(uid, "LOADED"))
			.sort((a, b) => a.t.localeCompare(b.t)).at(-1)?.data;

		const queryAll = () => cdp.call("tabs_queryAll");
		const mainSize = () => cdp.evaluate("({w: window.innerWidth, h: window.innerHeight})");

		// A create is only done when the tab's page has loaded and reported in.
		async function createTab(uid, title, extra = "") {
			const res = await cdp.call("tabs_createWindow", uid, tabUrl(uid, extra), title);
			if (res.__missing || res.__timeout || isError(res)) return res;
			created.add(uid);
			const loaded = await observer.waitFor((evs) => evs.some((e) => e.who === `tab${uid}` && e.event === "LOADED" && e.data?.href === tabUrl(uid, extra)), { timeoutMs: 60000 });
			return loaded ? res : { error: `tab${uid} never reported LOADED` };
		}

		async function attach(uid) {
			const c = await attachTo(port, `tab.html?uid=${uid}`);
			tabCdp.set(uid, c);
			return c;
		}

		const pageUrls = async () => (await listPages(port)).map((p) => p.url);

		// The saved window state decides whether the main window starts hidden, and a hidden
		// window does not apply a resize. Show it for the run, without saving that state, and
		// put it back afterwards.
		const mainWasHidden = (await cdp.call("tabs_getIsWindowHidden", 0)).result === true;
		if (mainWasHidden) await cdp.call("tabs_showWindow", 0);

		try {
			/* ------------------------------------------------------------ surface --- */

			await r.step("main has every tabs_* function and no slabsTab", async () => {
				const names = await cdp.evaluate("__slt.names()");
				const missing = TABS_NAMES.filter((n) => !names.includes(n));
				if (missing.length) return `missing on slabsGlobal: ${missing.join(", ")}`;
				if ((await cdp.evaluate("typeof window.slabsTab")) !== "undefined") return "slabsTab is defined on the main window";
			});

			/* ------------------------------------------------------------- create --- */

			await r.step("tabs_createWindow(101) opens a window that loads its page", async () => {
				const res = await createTab(101, "T101");
				if (res.__missing) return "tabs_createWindow is not exposed";
				if (res.__timeout) return "the callback never fired";
				if (isError(res)) return res.error;
				const urls = await until(async () => {
					const u = await pageUrls();
					return u.some((x) => x.includes("tab.html?uid=101")) ? u : null;
				}, { timeoutMs: 15000 });
				if (!urls) return `no CDP target for the tab, saw ${(await pageUrls()).join(", ")}`;
			});

			let t101 = null;
			await r.step("a CDP client attaches to the tab", async () => {
				t101 = await attach(101);
			});

			/* ---------------------------------------------------------- privilege --- */

			await r.step("the tab has slabsTab with exactly the tab api, and no slabsGlobal (its own report)", async () => {
				const s = lastState(101);
				if (!s) return "the tab reported nothing";
				if (s.hasSlabsGlobal) return "the tab reported slabsGlobal";
				if (JSON.stringify(s.slabsTabKeys) !== JSON.stringify(TAB_KEYS)) return `slabsTab keys were ${JSON.stringify(s.slabsTabKeys)}`;
			});

			await r.step("the tab has slabsTab with exactly the tab api, and no slabsGlobal (through CDP)", async () => {
				if (!t101) return "no CDP client on the tab";
				if ((await t101.evaluate("typeof window.slabsGlobal")) !== "undefined") return "slabsGlobal is defined in the tab";
				const keys = await t101.evaluate("Object.keys(window.slabsTab).sort()");
				if (JSON.stringify(keys) !== JSON.stringify(TAB_KEYS)) return `slabsTab keys were ${JSON.stringify(keys)}`;
			});

			await r.step("a tab cannot reach a main function through slabsTab", async () => {
				if (!t101) return "no CDP client on the tab";
				const res = await t101.callOn("slabsTab", "tabs_destroyWindow", 101);
				if (!res.__missing) return `expected the function to be absent, got ${JSON.stringify(res)}`;
			});

			/* -------------------------------------------------------- create rejects --- */

			await r.step("a duplicate uid is an error", async () => {
				const res = await cdp.call("tabs_createWindow", 101, tabUrl(101, "&dup=1"), "dup");
				if (!isError(res)) return `expected an error, got ${JSON.stringify(res)}`;
			});

			await r.step("uid 0, the main window, is an error", async () => {
				const res = await cdp.call("tabs_createWindow", 0, tabUrl(0), "zero");
				if (!isError(res)) return `expected an error, got ${JSON.stringify(res)}`;
			});

			await r.step("missing arguments are an error", async () => {
				const none = await cdp.call("tabs_createWindow");
				if (!isError(none)) return `no arguments: expected an error, got ${JSON.stringify(none)}`;
				const one = await cdp.call("tabs_createWindow", 109);
				if (!isError(one)) return `uid only: expected an error, got ${JSON.stringify(one)}`;
			});

			/* ----------------------------------------------------------- query --- */

			await r.step("tabs_queryAll lists the tab, and not the main window", async () => {
				const res = await queryAll();
				if (!Array.isArray(res)) return `expected an array, got ${JSON.stringify(res)}`;
				const mine = res.find((t) => t.uid === 101);
				if (!mine) return `uid 101 is not listed: ${JSON.stringify(res)}`;
				if (mine.url !== tabUrl(101)) return `url was ${mine.url}`;
				if (res.some((t) => t.uid === 0)) return "the main window (uid 0) is listed";
			});

			await r.step("tabs_getWindowCefId answers for a tab, and errors for main and unknown uids", async () => {
				const res = await cdp.call("tabs_getWindowCefId", 101);
				if (!Number.isInteger(res.result) || res.result <= 1) return `expected an id above 1, got ${JSON.stringify(res)}`;
				if (!isError(await cdp.call("tabs_getWindowCefId", 0))) return "uid 0 did not error";
				if (!isError(await cdp.call("tabs_getWindowCefId", 999))) return "uid 999 did not error";
			});

			/* ------------------------------------------------------ load and execute --- */

			await r.step("tabs_loadUrl navigates the tab", async () => {
				const res = await cdp.call("tabs_loadUrl", 101, tabUrl(101, "&nav=2"));
				if (isError(res) || res.__timeout) return JSON.stringify(res);
				const ok = await observer.waitFor((evs) => evs.some((e) => e.who === "tab101" && e.event === "LOADED" && e.data?.nav === "2"), { timeoutMs: 30000 });
				if (!ok) return "the tab never reported LOADED with nav=2";
				if (!isError(await cdp.call("tabs_loadUrl", 998, tabUrl(998)))) return "an unknown uid did not error";
			});

			await r.step("tabs_executeJs runs code in the tab, and refuses uid 0", async () => {
				const code = "fetch('/report', {method: 'POST', headers: {'content-type': 'application/json'}, body: JSON.stringify({who: 'tab101', event: 'EXEC', data: {n: 1 + 1}})})";
				const res = await cdp.call("tabs_executeJs", 101, code);
				if (isError(res) || res.__timeout) return JSON.stringify(res);
				const ok = await observer.waitFor((evs) => evs.some((e) => e.who === "tab101" && e.event === "EXEC" && e.data?.n === 2), { timeoutMs: 15000 });
				if (!ok) return "the injected code never reported";
				if (!isError(await cdp.call("tabs_executeJs", 0, "1"))) return "uid 0 did not error";
			});

			/* ------------------------------------------------------------ hide / show --- */

			await r.step("tabs_hideWindow and tabs_showWindow are reflected in tabs_getIsWindowHidden", async () => {
				if (isError(await cdp.call("tabs_hideWindow", 101))) return "hide errored";
				const hidden = await cdp.call("tabs_getIsWindowHidden", 101);
				if (hidden.result !== true) return `after hide: ${JSON.stringify(hidden)}`;
				if (isError(await cdp.call("tabs_showWindow", 101))) return "show errored";
				const shown = await cdp.call("tabs_getIsWindowHidden", 101);
				if (shown.result !== false) return `after show: ${JSON.stringify(shown)}`;
				if (!isError(await cdp.call("tabs_hideWindow", 999))) return "an unknown uid did not error";
			});

			/* ------------------------------------------------------------------ resize --- */

			await r.step("tabs_resizeWindow resizes that tab to width x height, and leaves main alone", async () => {
				const before = await mainSize();
				const res = await cdp.call("tabs_resizeWindow", 101, 900, 500);
				if (isError(res) || res.__timeout) return JSON.stringify(res);

				// The widget is 900x500 logical pixels; the page sees CSS pixels, so allow for
				// scaling and rounding but not for the width being used as the height.
				const near = (v, want) => Math.abs(v - want) <= 24;
				const ok = await observer.waitFor(() => {
					const s = lastState(101);
					return s && near(s.innerWidth, 900) && near(s.innerHeight, 500);
				}, { timeoutMs: 15000 });
				const s = lastState(101);
				if (!ok) return `the tab did not reach 900x500, last report ${s?.innerWidth}x${s?.innerHeight} at dpr ${s?.dpr}`;

				await settle(500);
				const after = await mainSize();
				if (after.w !== before.w || after.h !== before.h) return `main changed from ${before.w}x${before.h} to ${after.w}x${after.h}`;
			});

			await r.step("tabs_resizeWindow rejects sizes outside 200..8096, and an unknown uid", async () => {
				if (!isError(await cdp.call("tabs_resizeWindow", 101, 100, 100))) return "100x100 did not error";
				if (!isError(await cdp.call("tabs_resizeWindow", 101, 9000, 500))) return "9000x500 did not error";
				if (!isError(await cdp.call("tabs_resizeWindow", 999, 900, 500))) return "an unknown uid did not error";
			});

			/* ------------------------------------------------------------- title / icon --- */

			await r.step("tabs_setTitle changes the window title", async () => {
				const title = "Renamed ✓";
				const res = await cdp.call("tabs_setTitle", 101, title);
				if (isError(res) || res.__timeout) return JSON.stringify(res);
				const seen = await until(async () => windowTitles(workDir).includes(title), { timeoutMs: 10000, everyMs: 500 });
				if (!seen) return `no window titled "${title}"`;
				if (!isError(await cdp.call("tabs_setTitle", 999, "x"))) return "an unknown uid did not error";
			});

			await r.step("tabs_setIcon is accepted for a tab, and errors for an unknown uid", async () => {
				const icon = join(workDir, "icon.png");
				writeFileSync(icon, PNG);
				const res = await cdp.call("tabs_setIcon", 101, icon);
				if (isError(res) || res.__timeout) return JSON.stringify(res);
				if (!isError(await cdp.call("tabs_setIcon", 999, icon))) return "an unknown uid did not error";
			});

			/* --------------------------------------------------------------- messaging --- */

			await r.step("a tab's message to main with no receiver registered is an error", async () => {
				const res = await t101.callOn("slabsTab", "tab_sendStringToMain", "early");
				if (!isError(res)) return `expected an error, got ${JSON.stringify(res)}`;
			});

			await r.step("registering receivers does not invoke them", async () => {
				const m = await cdp.listen("slabsGlobal", "tabs_registerMsgReceiver");
				if (m.__missing) return "tabs_registerMsgReceiver is not exposed";
				const t = await t101.listen("slabsTab", "tab_registerMsgReceiver");
				if (t.__missing) return "tab_registerMsgReceiver is not exposed";
				await settle(1000);
				const mainInbox = await cdp.inbox();
				const tabInbox = await t101.inbox();
				if (mainInbox.length || tabInbox.length) return `main inbox ${JSON.stringify(mainInbox)}, tab inbox ${JSON.stringify(tabInbox)}`;
			});

			await r.step("tab to main: arrives once with the sender uid, and the sender's callback fires", async () => {
				const res = await t101.callOn("slabsTab", "tab_sendStringToMain", TRICKY);
				if (res.__timeout) return "the sender's callback never fired";
				if (isError(res)) return res.error;
				const got = await until(async () => (await cdp.inbox()).length >= 1, { timeoutMs: 10000, everyMs: 250 });
				if (!got) return "main received nothing";
				await settle(SETTLE_MS);
				const inbox = await cdp.inbox();
				if (inbox.length !== 1) return `main received ${inbox.length} messages: ${JSON.stringify(inbox)}`;
				if (inbox[0].length !== 2 || inbox[0][0] !== TRICKY || inbox[0][1] !== 101) {
					return `got ${JSON.stringify(inbox[0])}, expected ${JSON.stringify([TRICKY, 101])}`;
				}
			});

			await r.step("main to tab: arrives once as the string alone, and main's callback fires", async () => {
				const res = await cdp.call("tabs_sendStringToTab", 101, TRICKY);
				if (res.__timeout) return "main's callback never fired";
				if (isError(res)) return res.error;
				const got = await until(async () => (await t101.inbox()).length >= 1, { timeoutMs: 10000, everyMs: 250 });
				if (!got) return "the tab received nothing";
				await settle(SETTLE_MS);
				const inbox = await t101.inbox();
				if (inbox.length !== 1) return `the tab received ${inbox.length} messages: ${JSON.stringify(inbox)}`;
				if (inbox[0].length !== 1 || inbox[0][0] !== TRICKY) return `got ${JSON.stringify(inbox[0])}, expected ${JSON.stringify([TRICKY])}`;
			});

			let t102 = null;
			await r.step("a message to a tab with no receiver is an error", async () => {
				const res = await createTab(102, "T102");
				if (isError(res)) return res.error;
				t102 = await attach(102);
				const sent = await cdp.call("tabs_sendStringToTab", 102, "nobody home");
				if (!isError(sent)) return `expected an error, got ${JSON.stringify(sent)}`;
				if (!isError(await cdp.call("tabs_sendStringToTab", 999, "x"))) return "an unknown uid did not error";
			});

			await r.step("messages are routed to their addressee only", async () => {
				if (!t102) return "tab 102 is not attached";
				const registered = await t102.listen("slabsTab", "tab_registerMsgReceiver");
				if (registered.__missing) return "tab_registerMsgReceiver is not exposed";

				const to102 = `to-102 ${Date.now()}`;
				const to101 = `to-101 ${Date.now()}`;
				const from102 = `from-102 ${Date.now()}`;

				if (isError(await cdp.call("tabs_sendStringToTab", 102, to102))) return "send to 102 errored";
				if (isError(await cdp.call("tabs_sendStringToTab", 101, to101))) return "send to 101 errored";
				if (isError(await t102.callOn("slabsTab", "tab_sendStringToMain", from102))) return "send from 102 errored";
				await settle(SETTLE_MS);

				const in101 = (await t101.inbox()).map((m) => m[0]);
				const in102 = (await t102.inbox()).map((m) => m[0]);
				const inMain = await cdp.inbox();

				if (in102.length !== 1 || in102[0] !== to102) return `tab 102 received ${JSON.stringify(in102)}`;
				if (in101.includes(to102) || !in101.includes(to101)) return `tab 101 received ${JSON.stringify(in101)}`;
				const mine = inMain.filter((m) => m[0] === from102);
				if (mine.length !== 1 || mine[0][1] !== 102) return `main received ${JSON.stringify(inMain)}`;
			});

			/* ------------------------------------------------------------ destroy --- */

			await r.step("tabs_destroyWindow removes the window everywhere", async () => {
				const res = await cdp.call("tabs_destroyWindow", 101);
				if (isError(res) || res.__timeout) return JSON.stringify(res);
				created.delete(101);
				tabCdp.get(101)?.close();
				tabCdp.delete(101);

				const gone = await until(async () => !(await pageUrls()).some((u) => u.includes("tab.html?uid=101")), { timeoutMs: 15000 });
				if (!gone) return "the CDP target is still listed";
				const all = await queryAll();
				if (all.some((t) => t.uid === 101)) return `tabs_queryAll still lists 101: ${JSON.stringify(all)}`;
				if (!isError(await cdp.call("tabs_loadUrl", 101, tabUrl(101)))) return "tabs_loadUrl on the destroyed uid did not error";
				if (!isError(await cdp.call("tabs_destroyWindow", 101))) return "a second destroy did not error";
			});

			await r.step("the main window cannot be destroyed, and an unknown uid is an error", async () => {
				if (!isError(await cdp.call("tabs_destroyWindow", 0))) return "uid 0 did not error";
				if (!isError(await cdp.call("tabs_destroyWindow", 999))) return "uid 999 did not error";
				const v = await cdp.call("sl_getVersionInfo");
				if (v.__timeout || v.__missing) return "main stopped answering";
			});

			await r.step("the user closing a tab destroys it and tells main", async () => {
				const title = "SLT-close-103";
				const res = await createTab(103, title);
				if (isError(res)) return res.error;
				const before = (await cdp.inbox()).length;

				if (!closeWindow(workDir, title)) return `no window titled "${title}" to close`;

				const told = await until(async () => (await cdp.inbox()).slice(before).some((m) => m[0] === TAB_CLOSED && m[1] === 103), { timeoutMs: 15000, everyMs: 250 });
				if (!told) return `main was not told, inbox tail ${JSON.stringify((await cdp.inbox()).slice(before))}`;
				created.delete(103);

				const gone = await until(async () => !(await pageUrls()).some((u) => u.includes("tab.html?uid=103")), { timeoutMs: 15000 });
				if (!gone) return "the CDP target is still listed";
				if ((await queryAll()).some((t) => t.uid === 103)) return "tabs_queryAll still lists 103";

				// Its uid is free again, which shows the registry entry went with the window.
				const again = await createTab(103, "T103 again", "&again=1");
				if (isError(again)) return `could not reuse uid 103: ${again.error}`;
				if (isError(await cdp.call("tabs_destroyWindow", 103))) return "destroy of the reused uid errored";
				created.delete(103);
			});

			/* ------------------------------------------------------------------ churn --- */

			await r.step("create and destroy churn leaves the proxy answering", async () => {
				for (let i = 1; i <= 5; i++) {
					const res = await createTab(103, `churn ${i}`, `&churn=${i}`);
					if (isError(res)) return `round ${i}: ${res.error}`;
					const d = await cdp.call("tabs_destroyWindow", 103);
					if (isError(d) || d.__timeout) return `round ${i}: destroy ${JSON.stringify(d)}`;
					created.delete(103);
				}
				// Destroyed before its page had any time to load.
				const quick = await cdp.call("tabs_createWindow", 104, tabUrl(104, "&quick=1"), "quick");
				if (isError(quick)) return `quick create: ${quick.error}`;
				const quickGone = await cdp.call("tabs_destroyWindow", 104);
				if (isError(quickGone)) return `quick destroy: ${quickGone.error}`;

				const v = await cdp.call("sl_getVersionInfo");
				if (v.__timeout || v.__missing) return "sl_getVersionInfo stopped answering";
				const scenes = await cdp.call("obs_enum_scenes");
				if (!Array.isArray(scenes)) return `obs_enum_scenes answered ${JSON.stringify(scenes)}`;
			});

			/* ------------------------------------------------------ main's own browser api --- */

			await r.step("browser_resizeBrowser still resizes the main window, within its bounds", async () => {
				if (!isError(await cdp.call("browser_resizeBrowser", 100, 100))) return "100x100 did not error";
				const res = await cdp.call("browser_resizeBrowser", 1000, 700);
				if (isError(res) || res.__timeout) return JSON.stringify(res);
				const near = (v, want) => Math.abs(v - want) <= 24;
				const ok = await until(async () => {
					const s = await mainSize();
					return near(s.w, 1000) && near(s.h, 700);
				}, { timeoutMs: 10000, everyMs: 250 });
				if (!ok) {
					const s = await mainSize();
					return `main is ${s.w}x${s.h}, expected about 1000x700`;
				}
			});
		} finally {
			// Whatever a failed step left open must not outlive the suite.
			for (const uid of created) await cdp.call("tabs_destroyWindow", uid).catch(() => {});
			for (const c of tabCdp.values()) c.close();
			if (mainWasHidden) await cdp.call("tabs_hideWindow", 0).catch(() => {});
		}

		await r.step("no tab windows are left", async () => {
			const all = await until(async () => {
				const res = await queryAll();
				return Array.isArray(res) && res.length === 0 ? res : null;
			}, { timeoutMs: 10000 });
			if (!all) return `tabs_queryAll still lists ${JSON.stringify(await queryAll())}`;
		});

		return r.list;
	},
};
