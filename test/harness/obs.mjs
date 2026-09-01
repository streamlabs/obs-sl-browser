/*
 * Finding, starting and stopping OBS.
 *
 * The plugin's api only exists inside a running OBS, so every suite needs one. This is the
 * part none of them should be writing themselves.
 */

import { existsSync, readFileSync } from "node:fs";
import { spawn, execFileSync } from "node:child_process";
import { dirname, join } from "node:path";

/** Blocks the thread. Needed in teardown, which runs from signal handlers and cannot await. */
export const sleepSync = (ms) => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export function alive(pid) {
	if (!pid) return false;
	try {
		process.kill(pid, 0);
		return true;
	} catch {
		return false;
	}
}

/**
 * Where obs64.exe is, in order of preference: what the caller passed, $OBS_EXE, then the
 * rundir CI\dev_build.ps1 produces - builds\obs-studio-<obs.ver>\build_x64\rundir\<config>.
 */
export function resolveObsExe({ repoRoot, explicit, config = "RelWithDebInfo" }) {
	if (explicit) return explicit;
	if (process.env.OBS_EXE) return process.env.OBS_EXE;

	let version;
	try {
		version = readFileSync(join(repoRoot, "obs.ver"), "utf8").trim();
	} catch {
		return null;
	}
	return join(repoRoot, "builds", `obs-studio-${version}`,
		"build_x64", "rundir", config, "bin", "64bit", "obs64.exe");
}

/** The rundir root - the directory holding bin\, data\ and obs-plugins\ - for an obs64.exe. */
export function rundirOf(exe) {
	return join(dirname(exe), "..", "..");
}

/**
 * Check the path before anyone derives a rundir from it. A mistyped --obs would otherwise
 * have a config tree seeded next to it, somewhere the harness has no business writing,
 * before anything noticed the executable was not there.
 */
export function assertObsExe(exe) {
	if (!exe) throw new Error("could not work out where obs64.exe is. Pass --obs <path>.");
	if (!existsSync(exe)) {
		throw new Error(
			`obs64.exe not found at\n    ${exe}\n` +
			`Build it with  .\\CI\\dev_build.ps1 -Run  (once, so the rundir is populated), ` +
			`or pass --obs <path>.`);
	}
}

export async function devtoolsUp(port) {
	try {
		const r = await fetch(`http://127.0.0.1:${port}/json/version`, { signal: AbortSignal.timeout(1200) });
		return r.ok ? r.json() : null;
	} catch {
		return null;
	}
}

/**
 * Start OBS and wait for its DevTools port.
 *
 * The flags are not optional:
 *   --portable            config lives in <rundir>\config, not the developer's real profile
 *   --disable-shutdown-check  a run ends by killing OBS, which is an unclean shutdown; without
 *                         this the next start stops on the safe-mode modal and looks hung
 *   --multi               don't argue with an OBS the developer already has open
 *   --disable-updater     no network on the critical path
 */
export async function launchObs({ exe, pageUrl, collection, port, timeoutMs = 120000, say = () => {}, onSpawn = () => {} }) {
	assertObsExe(exe);

	const args = ["--portable", "--disable-shutdown-check", "--multi", "--disable-updater"];
	if (collection) args.push("--collection", collection);

	say(`starting ${exe}`);
	const child = spawn(exe, args, {
		cwd: dirname(exe), // OBS resolves data\, obs-plugins\ and its portable config relative to cwd
		env: { ...process.env, SL_PLUGIN_DEFAULT_URL: pageUrl },
		detached: true,
		stdio: "ignore",
	});
	child.unref();

	const obs = {
		pid: child.pid,
		exe,
		stop() {
			if (!this.pid) return;
			const pid = this.pid;
			this.pid = null;
			// No /F on the first pass: that sends WM_CLOSE so OBS saves and exits properly.
			try { execFileSync("taskkill", ["/PID", String(pid), "/T"], { stdio: "ignore" }); } catch { /* gone */ }
			for (let i = 0; i < 30 && alive(pid); i++) sleepSync(200);
			if (alive(pid)) {
				try { execFileSync("taskkill", ["/PID", String(pid), "/T", "/F"], { stdio: "ignore" }); } catch { /* gone */ }
			}
		},
	};

	// Hand the caller a stoppable handle before the wait below, not after it. The child is
	// detached, so a Ctrl-C during the startup window would otherwise leave OBS running.
	onSpawn(obs);

	const deadline = Date.now() + timeoutMs;
	while (Date.now() < deadline) {
		const v = await devtoolsUp(port);
		if (v) {
			say(`devtools up on ${port} after ${Math.round((timeoutMs - (deadline - Date.now())) / 1000)}s`);
			return obs;
		}
		if (!alive(child.pid)) {
			obs.pid = null;
			throw new Error(
				"OBS exited before opening the DevTools port. Start it by hand to see why - " +
				"a crash-recovery or safe-mode dialog is the usual cause.");
		}
		await sleep(500);
	}
	obs.stop();
	throw new Error(
		`OBS did not open port ${port} within ${timeoutMs / 1000}s. ` +
		`It may be sitting on a dialog; start it by hand once, then retry.`);
}
