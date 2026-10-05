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

import { writeFileSync, mkdirSync, rmSync, existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
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
		const loads = (uid) => tabEvents(uid, "LOADED");
		const lastState = (uid) => tabEvents(uid, "RESIZED").concat(tabEvents(uid, "LOADED"))
			.sort((a, b) => a.t.localeCompare(b.t)).at(-1)?.data;

		const queryAll = () => cdp.call("tabs_queryAll");
		const mainSize = () => cdp.evaluate("({w: window.innerWidth, h: window.innerHeight})");

		// A create is only done when the tab's page has loaded and reported in.
		async function createTab(uid, title, extra = "", moreArgs = []) {
			const res = await cdp.call("tabs_createWindow", uid, tabUrl(uid, extra), title, ...moreArgs);
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

		// Icons are only taken from under %APPDATA%\StreamlabsOBS\, which is where the app store downloads them.
		const appData = process.env.APPDATA;
		const iconRoot = join(appData, "StreamlabsOBS");
		const iconDirPath = join(iconRoot, `slt-icons-${Date.now()}`);
		const outsidePath = join(appData, `slt-icon-outside-${Date.now()}.png`);
		mkdirSync(iconDirPath, { recursive: true });
		const iconFile = (name) => {
			const f = join(iconDirPath, name);
			writeFileSync(f, PNG);
			return f;
		};
		const iconDir = { png: iconFile("icon.png") };

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

			/* ---------------------------------------------------------------- readiness --- */

			await r.step("tabs_createWindow replies once the tab exists: an immediate hide, show, resize and cef id all work", async () => {
				const res = await cdp.call("tabs_createWindow", 110, tabUrl(110), "T110");
				created.add(110);
				if (isError(res) || res.__timeout) return `create: ${JSON.stringify(res)}`;

				// Straight after the reply, with no waiting for the page to load.
				const hide = await cdp.call("tabs_hideWindow", 110);
				if (isError(hide)) return `hide: ${hide.error}`;
				const hidden = await cdp.call("tabs_getIsWindowHidden", 110);
				if (hidden.result !== true) return `after hide: ${JSON.stringify(hidden)}`;
				const show = await cdp.call("tabs_showWindow", 110);
				if (isError(show)) return `show: ${show.error}`;
				if ((await cdp.call("tabs_getIsWindowHidden", 110)).result !== false) return "after show the tab is still hidden";
				const resize = await cdp.call("tabs_resizeWindow", 110, 640, 480);
				if (isError(resize)) return `resize: ${resize.error}`;
				const id = await cdp.call("tabs_getWindowCefId", 110);
				if (!Number.isInteger(id.result) || id.result <= 1) return `cef id: ${JSON.stringify(id)}`;
				const exec = await cdp.call("tabs_executeJs", 110, "1");
				if (isError(exec)) return `executeJs: ${exec.error}`;
				const nav = await cdp.call("tabs_loadUrl", 110, tabUrl(110, "&nav=3"));
				if (isError(nav)) return `loadUrl: ${nav.error}`;
				const loaded = await observer.waitFor((evs) => evs.some((e) => e.who === "tab110" && e.event === "LOADED" && e.data?.nav === "3"), { timeoutMs: 30000 });
				if (!loaded) return "the tab never loaded after the immediate calls";
			});

			await r.step("a tab call issued before the tab is ready answers not ready, or has its effect", async () => {
				// All issued in one turn of the page's script, so they reach the proxy back to back,
				// before the Qt thread has built the window.
				const calls = [
					["tabs_createWindow", 111, tabUrl(111), "T111"],
					["tabs_hideWindow", 111],
					["tabs_getIsWindowHidden", 111],
					["tabs_resizeWindow", 111, 700, 500],
					["tabs_executeJs", 111, "1"],
					["tabs_loadUrl", 111, tabUrl(111, "&nav=4")],
					["tabs_getWindowCefId", 111],
					["tabs_showWindow", 111],
				];
				const replies = await cdp.evaluate(`Promise.all(${JSON.stringify(calls)}.map((c) => __slt.call(...c)))`, { awaitPromise: true });
				created.add(111);
				const [create, hide, hidden, resize, exec, nav, cefId, show] = replies;
				if (isError(create) || create.__timeout) return `create: ${JSON.stringify(create)}`;

				r.info("calls answered not ready before the tab existed", [["hide", hide], ["isHidden", hidden], ["resize", resize], ["executeJs", exec], ["loadUrl", nav], ["cefId", cefId], ["show", show]].filter(([, x]) => x.error === "not ready").map(([n]) => n).join(", ") || "none");

				// A call may only succeed if it really did something; "not ready" is the one honest failure.
				for (const [name, reply] of [["hide", hide], ["isHidden", hidden], ["resize", resize], ["executeJs", exec], ["loadUrl", nav], ["cefId", cefId], ["show", show]]) {
					if (isError(reply) && reply.error !== "not ready") return `${name}: ${JSON.stringify(reply)}`;
				}

				// The same calls once the tab is ready.
				if (isError(await cdp.call("tabs_hideWindow", 111))) return "hide errored once the tab was ready";
				if ((await cdp.call("tabs_getIsWindowHidden", 111)).result !== true) return "the tab was not hidden by a hide once ready";
				if (isError(await cdp.call("tabs_showWindow", 111))) return "show errored once the tab was ready";

				for (const uid of [110, 111]) {
					await cdp.call("tabs_destroyWindow", uid);
					created.delete(uid);
				}
			});

			await r.step("a hide or show that claimed success before the tab was ready took effect", async () => {
				const calls = [
					["tabs_createWindow", 113, tabUrl(113), "T113"],
					["tabs_hideWindow", 113],
				];
				const [create, hide] = await cdp.evaluate(`Promise.all(${JSON.stringify(calls)}.map((c) => __slt.call(...c)))`, { awaitPromise: true });
				created.add(113);
				if (isError(create) || create.__timeout) return `create: ${JSON.stringify(create)}`;
				if (isError(hide)) {
					if (hide.error !== "not ready") return `hide: ${JSON.stringify(hide)}`;
				} else if ((await cdp.call("tabs_getIsWindowHidden", 113)).result !== true) {
					return "the early hide answered success but the tab is not hidden";
				}
				await cdp.call("tabs_destroyWindow", 113);
				created.delete(113);
			});

			await r.step("a burst of tabs_resizeWindow calls ends at the last size, and main is untouched", async () => {
				const before = await mainSize();
				const res = await createTab(112, "T112");
				if (isError(res)) return res.error;
				const sizes = [[600, 400], [800, 600], [640, 480], [900, 500], [700, 450]];
				for (let i = 0; i < 40; i++) {
					const [w, h] = sizes[i % sizes.length];
					if (isError(await cdp.call("tabs_resizeWindow", 112, w, h))) return `resize ${i} errored`;
				}
				const near = (v, want) => Math.abs(v - want) <= 24;
				const ok = await observer.waitFor(() => {
					const s = lastState(112);
					return s && near(s.innerWidth, 700) && near(s.innerHeight, 450);
				}, { timeoutMs: 15000 });
				if (!ok) return `the tab ended at ${lastState(112)?.innerWidth}x${lastState(112)?.innerHeight}, not 700x450`;
				const after = await mainSize();
				if (after.w !== before.w || after.h !== before.h) return `main changed from ${before.w}x${before.h} to ${after.w}x${after.h}`;
				await cdp.call("tabs_destroyWindow", 112);
				created.delete(112);
			});

			/* ----------------------------------------------------------- init script --- */

			const INIT = "window.__slInit = (window.__slInit || 0) + 1; window.__slInitTab = typeof window.slabsTab;";

			await r.step("an initScript runs in the tab before the page's own scripts, once slabsTab exists", async () => {
				const res = await createTab(120, "T120", "", ["", INIT]);
				if (isError(res)) return res.error;
				const s = loads(120).at(-1).data;
				if (s.initAtStart !== 1) return `the page saw init count ${s.initAtStart} when its own script started, expected 1`;
				if (s.initSawSlabsTab !== "object") return `slabsTab was ${s.initSawSlabsTab} when the script ran`;
				if (s.hasSlabsGlobal) return "the tab reported slabsGlobal";
			});

			await r.step("the initScript runs again after a reload, after tabs_loadUrl, and after the page navigates itself", async () => {
				const loadsBefore = () => loads(120).length;
				const next = (n) => observer.waitFor(() => loads(120).length >= n, { timeoutMs: 30000 });

				let n = loadsBefore();
				if (isError(await cdp.call("tabs_executeJs", 120, "location.reload()"))) return "reload errored";
				if (!(await next(n + 1))) return "no load after location.reload()";
				let s = loads(120).at(-1).data;
				if (s.initAtStart !== 1) return `after reload the page saw init count ${s.initAtStart}, expected 1`;

				n = loadsBefore();
				if (isError(await cdp.call("tabs_loadUrl", 120, tabUrl(120, "&nav=5")))) return "loadUrl errored";
				if (!(await next(n + 1))) return "no load after tabs_loadUrl";
				s = loads(120).at(-1).data;
				if (s.nav !== "5" || s.initAtStart !== 1) return `after loadUrl: nav ${s.nav}, init count ${s.initAtStart}`;

				n = loadsBefore();
				if (isError(await cdp.call("tabs_executeJs", 120, `location.href = ${JSON.stringify(tabUrl(120, "&nav=6"))}`))) return "navigate errored";
				if (!(await next(n + 1))) return "no load after the page navigated itself";
				s = loads(120).at(-1).data;
				if (s.nav !== "6" || s.initAtStart !== 1) return `after self navigation: nav ${s.nav}, init count ${s.initAtStart}`;

				await cdp.call("tabs_destroyWindow", 120);
				created.delete(120);
			});

			await r.step("a throwing initScript does not stop the page, and a large one runs", async () => {
				const bad = await createTab(121, "T121", "", ["", "throw new Error('init failed')"]);
				if (isError(bad)) return `throwing script: ${bad.error}`;
				const keys = loads(121).at(-1).data.slabsTabKeys;
				if (JSON.stringify(keys) !== JSON.stringify(TAB_KEYS)) return `slabsTab keys were ${JSON.stringify(keys)}`;
				await cdp.call("tabs_destroyWindow", 121);
				created.delete(121);

				const big = `/*${"x".repeat(200000)}*/ ${INIT}`;
				const res = await createTab(122, "T122", "", ["", big]);
				if (isError(res)) return `large script: ${res.error}`;
				if (loads(122).at(-1).data.initAtStart !== 1) return "the 200 KB script did not run";
				await cdp.call("tabs_destroyWindow", 122);
				created.delete(122);
			});

			await r.step("without an initScript nothing is run, and a tab does not inherit another's", async () => {
				const res = await createTab(123, "T123");
				if (isError(res)) return res.error;
				const s = loads(123).at(-1).data;
				if (s.initAtStart !== null || s.init !== null) return `unexpected init state ${JSON.stringify(s)}`;
				await cdp.call("tabs_destroyWindow", 123);
				created.delete(123);
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
				const res = await cdp.call("tabs_setIcon", 101, iconDir.png);
				if (isError(res) || res.__timeout) return JSON.stringify(res);
				if (!isError(await cdp.call("tabs_setIcon", 999, iconDir.png))) return "an unknown uid did not error";
			});

			/* ------------------------------------------------------------- iframe gating --- */

			const otherOrigin = observer.origin.replace("127.0.0.1", "localhost");
			const FRAME_ORIGINS = [["same-origin", observer.origin], ["cross-origin", otherOrigin]];
			const frameReport = (who, event) => observer.events.find((e) => e.who === who && e.event === event)?.data;
			const addFrame = (page, who, origin) => page.evaluate(`(() => { const f = document.createElement("iframe"); f.id = ${JSON.stringify(who)}; f.src = ${JSON.stringify(`${origin}/frame.html?who=${who}`)}; document.body.appendChild(f); return true; })()`);
			const hasTabApi = (keys) => (keys || []).some((k) => k.startsWith("tabs_") || k.startsWith("tab_"));

			await r.step("an iframe in the main page has no tabs_* or tab_*, but keeps the rest of slabsGlobal", async () => {
				for (const [label, origin] of FRAME_ORIGINS) {
					const who = `main-${label}-frame`;
					await addFrame(cdp, who, origin);
					// A cross-origin iframe is in another renderer process, which no reply is routed to, so only same-origin can be called.
					const last = label === "same-origin" ? "FRAME_CALL" : "FRAME";
					if (!(await observer.waitFor((evs) => evs.some((e) => e.who === who && e.event === last), { timeoutMs: 30000 }))) return `${label}: the iframe never reported`;
					const f = frameReport(who, "FRAME");
					if (f.isTop) return `${label}: the report came from the top frame`;
					if (!f.slabsGlobalKeys) return `${label}: the iframe has no slabsGlobal at all`;
					if (hasTabApi(f.slabsGlobalKeys)) return `${label}: the iframe has ${f.slabsGlobalKeys.filter((k) => k.startsWith("tab")).join(", ")}`;
					if (!f.slabsGlobalKeys.includes("sl_getVersionInfo")) return `${label}: the iframe lost sl_getVersionInfo`;
					if (f.slabsTabKeys) return `${label}: the iframe has slabsTab`;
					if (label === "same-origin") {
						const reply = frameReport(who, "FRAME_CALL").reply;
						if (reply.__missing || reply.__timeout || isError(reply)) return `${label}: sl_getVersionInfo from the iframe answered ${JSON.stringify(reply)}`;
					}
					await cdp.evaluate(`document.getElementById(${JSON.stringify(who)}).remove()`);
				}
				// The top frame is unaffected.
				const names = await cdp.evaluate("__slt.names()");
				const missing = TABS_NAMES.filter((n) => !names.includes(n));
				if (missing.length) return `the main frame lost: ${missing.join(", ")}`;
			});

			await r.step("an iframe in a tab has neither slabsTab nor slabsGlobal, and a cross-origin one still loads", async () => {
				const res = await createTab(160, "T160");
				if (isError(res)) return res.error;
				const t = await attach(160);

				for (const [label, origin] of FRAME_ORIGINS) {
					const who = `tab-${label}-frame`;
					await addFrame(t, who, origin);
					if (!(await observer.waitFor((evs) => evs.some((e) => e.who === who && e.event === "FRAME"), { timeoutMs: 30000 }))) return `${label}: the iframe never loaded`;
					const f = frameReport(who, "FRAME");
					if (f.isTop) return `${label}: the report came from the top frame`;
					if (f.slabsGlobalKeys) return `${label}: the iframe has slabsGlobal`;
					if (f.slabsTabKeys) return `${label}: the iframe has slabsTab: ${JSON.stringify(f.slabsTabKeys)}`;
				}

				// The tab's own top frame keeps its api.
				const keys = await t.evaluate("Object.keys(window.slabsTab).sort()");
				if (JSON.stringify(keys) !== JSON.stringify(TAB_KEYS)) return `the top frame's slabsTab keys were ${JSON.stringify(keys)}`;
				t.close();
				tabCdp.delete(160);
				await cdp.call("tabs_destroyWindow", 160);
				created.delete(160);
			});

			/* ----------------------------------------------------------- icon path rules --- */

			await r.step("an icon outside %APPDATA%\\StreamlabsOBS, with the wrong extension, or on a UNC or device path is an error", async () => {
				writeFileSync(outsidePath, PNG);
				const inside = (name) => iconFile(name);
				const junction = join(iconDirPath, "link");
				mkdirSync(workDir, { recursive: true });
				writeFileSync(join(workDir, "outside.png"), PNG);
				execFileSync("cmd.exe", ["/c", "mklink", "/J", junction, workDir], { stdio: "ignore" });

				const bad = {
					"a png in the work dir": join(workDir, "icon.png"),
					"a png beside StreamlabsOBS": outsidePath,
					"dot-dot out of StreamlabsOBS": join(iconRoot, "..", outsidePath.split("\\").pop()),
					"through a junction": join(junction, "outside.png"),
					"a UNC path": "\\\\127.0.0.1\\share\\a.png",
					"a UNC path with forward slashes": "//127.0.0.1/share/a.png",
					"a device path to a real icon": `\\\\?\\${iconDir.png}`,
					"a dot device path to a real icon": `\\\\.\\${iconDir.png}`,
					"a relative path": "icon.png",
					"a missing file": join(iconDirPath, "missing.png"),
					"an alternate data stream": `${iconDir.png}:stream`,
					"an exe": inside("icon.exe"),
					"a png.exe": inside("icon.png.exe"),
					"an svg": inside("icon.svg"),
					"a bmp": inside("icon.bmp"),
					"a directory": iconDirPath,
					"an empty path": "",
				};
				for (const [what, path] of Object.entries(bad)) {
					const res = await cdp.call("tabs_setIcon", 101, path);
					if (!isError(res)) return `tabs_setIcon accepted ${what}: ${JSON.stringify(res)}`;
				}
				for (const [what, path] of Object.entries(bad)) {
					if (what === "an empty path") continue;
					const res = await cdp.call("tabs_createWindow", 150, tabUrl(150), "icon", path);
					if (!isError(res)) {
						await cdp.call("tabs_destroyWindow", 150);
						return `tabs_createWindow accepted ${what}: ${JSON.stringify(res)}`;
					}
				}
				if ((await queryAll()).some((t) => t.uid === 150)) return "a refused create left uid 150 listed";
			});

			await r.step("icons .png, .ico, .jpg, .jpeg are accepted from inside the folder, in any letter case", async () => {
				for (const name of ["ok.png", "ok.ico", "ok.jpg", "ok.jpeg", "OK.PNG"]) {
					const f = iconFile(name);
					const res = await cdp.call("tabs_setIcon", 101, f);
					if (isError(res) || res.__timeout) return `${name}: ${JSON.stringify(res)}`;
				}
				const upper = iconDir.png.toUpperCase();
				const res = await cdp.call("tabs_setIcon", 101, upper);
				if (isError(res)) return `an upper-cased path was refused: ${JSON.stringify(res)}`;

				const made = await createTab(151, "T151", "", [iconDir.png]);
				if (isError(made)) return `tabs_createWindow with an icon: ${made.error}`;
				await cdp.call("tabs_destroyWindow", 151);
				created.delete(151);
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

			/* ------------------------------------------------------------- url allow-list --- */

			// Everything but https on the CDN host. The harness's own origin is let through by
			// SL_PLUGIN_TEST_TAB_ORIGIN, which is how these tests load pages at all.
			const BAD_URLS = [
				"http://absolute/C:/x.html", "file:///C:/Windows/win.ini", "data:text/html,<b>x</b>", "javascript:1", "chrome://version",
				"devtools://devtools/bundled/inspector.html", "about:blank", "", "ws://platform-cdn.streamlabs.com/",
				"http://platform-cdn.streamlabs.com/", "https://platform-cdn.streamlabs.com.example.com/",
				"https://example.com/?https://platform-cdn.streamlabs.com/", "https://platform-cdn.streamlabs.com@example.com/",
				"https://user:pw@platform-cdn.streamlabs.com/", "//platform-cdn.streamlabs.com/", "https://streamlabs.com/",
				"https://platform-cdn.streamlabs.com:8443/", "http://127.0.0.1:1/tab.html",
			];

			await r.step("tabs_createWindow refuses every url that is not https on the CDN host", async () => {
				for (const url of BAD_URLS) {
					const res = await cdp.call("tabs_createWindow", 140, url, "bad");
					if (!isError(res)) {
						await cdp.call("tabs_destroyWindow", 140);
						return `${JSON.stringify(url)} was accepted: ${JSON.stringify(res)}`;
					}
				}
				// None of the refusals may have taken the uid.
				if ((await queryAll()).some((t) => t.uid === 140)) return "a refused create left uid 140 listed";
				const ok = await createTab(140, "T140");
				if (isError(ok)) return `uid 140 was not free after the refusals: ${ok.error}`;
				await cdp.call("tabs_destroyWindow", 140);
				created.delete(140);
			});

			await r.step("tabs_createWindow accepts the CDN origin, however its host and default port are spelled", async () => {
				for (const url of ["https://platform-cdn.streamlabs.com/", "HTTPS://PLATFORM-CDN.STREAMLABS.COM:443/index.html"]) {
					const res = await cdp.call("tabs_createWindow", 141, url, "cdn");
					created.add(141);
					if (isError(res) || res.__timeout) return `${url}: ${JSON.stringify(res)}`;
					const d = await cdp.call("tabs_destroyWindow", 141);
					created.delete(141);
					if (isError(d)) return `${url}: destroy ${JSON.stringify(d)}`;
				}
			});

			await r.step("tabs_loadUrl refuses the same urls, and leaves the tab where it was", async () => {
				const res = await createTab(142, "T142");
				if (isError(res)) return res.error;
				for (const url of BAD_URLS) {
					if (!isError(await cdp.call("tabs_loadUrl", 142, url))) return `${JSON.stringify(url)} was accepted by tabs_loadUrl`;
				}
				await settle(1000);
				const mine = (await queryAll()).find((t) => t.uid === 142);
				if (mine?.url !== tabUrl(142)) return `the tab is at ${mine?.url}`;
				await cdp.call("tabs_destroyWindow", 142);
				created.delete(142);
			});

			/* ------------------------------------------------------------ navigation lock --- */

			await r.step("a tab's main frame cannot navigate off the approved origin, by script or by location", async () => {
				const res = await createTab(143, "T143");
				if (isError(res)) return res.error;
				const t = await attach(143);

				// Same host name, different origin (localhost is not 127.0.0.1), so it would load if allowed.
				const otherOrigin = observer.origin.replace("127.0.0.1", "localhost");
				const targets = [`${otherOrigin}/tab.html?uid=143&off=1`, "https://example.com/", "http://absolute/C:/x.html", "file:///C:/Windows/win.ini"];

				for (const target of targets) {
					await t.evaluate(`location.href = ${JSON.stringify(target)}`).catch(() => {});
					await settle(1500);
				}
				await t.evaluate(`location.assign(${JSON.stringify(targets[0])})`).catch(() => {});
				await t.evaluate(`location.replace(${JSON.stringify(targets[0])})`).catch(() => {});
				await settle(2000);

				if (tabEvents(143, "LOADED").some((e) => e.data?.off === "1" || e.data?.href !== tabUrl(143))) return `the tab loaded somewhere else: ${JSON.stringify(tabEvents(143, "LOADED").map((e) => e.data?.href))}`;
				const href = await t.evaluate("location.href");
				if (href !== tabUrl(143)) return `the tab is at ${href}`;
				if (observer.events.some((e) => e.event === "served" && String(e.data).includes("off=1"))) return "the off-origin page was requested from the server";

				// The tab still works, and still moves within its origin.
				const n = loads(143).length;
				if (isError(await cdp.call("tabs_loadUrl", 143, tabUrl(143, "&nav=7")))) return "loadUrl within the origin errored";
				if (!(await observer.waitFor(() => loads(143).length > n, { timeoutMs: 20000 }))) return "no load within the origin afterwards";
				t.close();
				tabCdp.delete(143);
				await cdp.call("tabs_destroyWindow", 143);
				created.delete(143);
			});

			/* ------------------------------------------------------------ close flag --- */

			await r.step("a tab created with hideOnClose is only hidden when the user closes it, and main is not told", async () => {
				const title = "SLT-hide-on-close-130";
				const res = await createTab(130, title, "", ["", "", true]);
				if (isError(res)) return res.error;
				const before = (await cdp.inbox()).length;

				if (!closeWindow(workDir, title)) return `no window titled "${title}" to close`;

				const hidden = await until(async () => (await cdp.call("tabs_getIsWindowHidden", 130)).result === true, { timeoutMs: 10000, everyMs: 250 });
				if (!hidden) return "the tab was not hidden";
				await settle(SETTLE_MS);
				const told = (await cdp.inbox()).slice(before).filter((m) => m[1] === 130);
				if (told.length) return `main was told: ${JSON.stringify(told)}`;
				if (!(await queryAll()).some((t) => t.uid === 130)) return "the tab is no longer listed";
				if (!windowTitles(workDir).includes(title)) return "the window is gone";

				// Still a working tab: it can be shown again and closed again without being destroyed.
				if (isError(await cdp.call("tabs_showWindow", 130))) return "show errored";
				if ((await cdp.call("tabs_getIsWindowHidden", 130)).result !== false) return "show did not unhide it";
				if (!closeWindow(workDir, title)) return "the second close found no window";
				const again = await until(async () => (await cdp.call("tabs_getIsWindowHidden", 130)).result === true, { timeoutMs: 10000, everyMs: 250 });
				if (!again) return "the second close did not hide it";

				if (isError(await cdp.call("tabs_destroyWindow", 130))) return "destroy errored";
				created.delete(130);
			});

			await r.step("a tab created with hideOnClose false is destroyed when the user closes it, as by default", async () => {
				const title = "SLT-destroy-on-close-131";
				const res = await createTab(131, title, "", ["", "", false]);
				if (isError(res)) return res.error;
				const before = (await cdp.inbox()).length;

				if (!closeWindow(workDir, title)) return `no window titled "${title}" to close`;
				const told = await until(async () => (await cdp.inbox()).slice(before).some((m) => m[0] === TAB_CLOSED && m[1] === 131), { timeoutMs: 15000, everyMs: 250 });
				if (!told) return "main was not told";
				created.delete(131);
				if ((await queryAll()).some((t) => t.uid === 131)) return "the tab is still listed";
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
			// Best effort: a junction is removed as a link, so its target is untouched.
			try { rmSync(iconDirPath, { recursive: true, force: true }); rmSync(outsidePath, { force: true }); } catch { /* still in use */ }
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
