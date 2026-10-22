/*
 * The Chromium switches sl-browser passes to every CEF process, and the features they turn off.
 */

import { execFileSync } from "node:child_process";
import { results } from "../../harness/suite.mjs";

function pageProcessCommandLines() {
	const out = execFileSync("powershell.exe", ["-NoProfile", "-Command", "Get-CimInstance Win32_Process -Filter \"Name='sl-browser-page.exe'\" | ForEach-Object { $_.CommandLine }"], { encoding: "utf8" });
	return out.split(/\r?\n/).filter(Boolean);
}

export default {
	name: "chrome-hardening",
	description: "disabled Chromium features reach the CEF processes",
	timeoutMs: 60000,

	async run({ cdp }) {
		const r = results();

		await r.step("the helper processes are started with the disabled features", async () => {
			const lines = pageProcessCommandLines();
			if (!lines.length) return "no sl-browser-page.exe process found";
			for (const feature of ["WebBluetooth", "EnableWindowsGamingInputDataFetcher"]) {
				const missing = lines.filter((l) => !l.includes(feature));
				if (missing.length) return `${feature} missing from ${missing.length} of ${lines.length} helper command lines`;
			}
		});

		await r.step("Web Bluetooth is not exposed to the page", async () => {
			if ((await cdp.evaluate("typeof navigator.bluetooth")) !== "undefined") return "navigator.bluetooth exists";
		});

		await r.step("the Document Picture-in-Picture API is not exposed to the page", async () => {
			if (await cdp.evaluate("'documentPictureInPicture' in window")) return "window.documentPictureInPicture exists";
		});

		return r.list;
	},
};
