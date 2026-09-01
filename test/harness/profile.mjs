/*
 * The throwaway OBS profile a suite runs against.
 *
 * OBS in portable mode reads its config from <rundir>\config\obs-studio (CONFIG_PATH is
 * BASE_PATH "/config" in frontend/OBSApp.cpp, and BASE_PATH is ..\.. from bin\64bit). There
 * is no way to point it somewhere else, so the harness owns that directory and drops a
 * marker in it - a config tree without the marker belongs to someone else and is not touched.
 *
 * Settings here exist to get OBS started unattended:
 *   FirstRun         the auto-configuration wizard is a modal, and startup never completes
 *                    behind it
 *   ConfirmOnExit    a second modal, on the way out
 *   BrowserHWAccel   CEF's shared-texture path, which a CI runner has no hardware for
 */

import { mkdirSync, writeFileSync, readFileSync, existsSync, readdirSync, statSync } from "node:fs";
import { basename, extname, join } from "node:path";

const MARKER = ".written-by-the-test-harness";
const PROFILE = "SLTest";

const ini = (sections) =>
	Object.entries(sections)
		.map(([name, keys]) =>
			`[${name}]\n` + Object.entries(keys).map(([k, v]) => `${k}=${v}`).join("\n"))
		.join("\n") + "\n";

/**
 * Write a clean profile into a rundir, replacing whatever the harness put there last time.
 *
 * @param {string} rundir      directory holding bin\, data\ and obs-plugins\
 * @param {string} [collection] path to a scene collection .json to install and select
 * @param {object} [replace]   {NAME: value} substituted for {{NAME}} in the collection, which
 *                             is how a browser source in it reaches the harness server without
 *                             the collection having to hardcode a port
 * @param {boolean} [force]    overwrite a config tree the harness does not own
 * @returns {{configDir: string, logsDir: string, collectionName: string}}
 */
export function seedProfile({ rundir, collection, replace = {}, force = false }) {
	const configDir = join(rundir, "config", "obs-studio");

	if (existsSync(configDir) && !existsSync(join(configDir, MARKER)) && !force) {
		throw new Error(
			`${configDir}\nalready holds a portable OBS config that this harness did not create. ` +
			`Move it aside, or pass --force to overwrite it.`);
	}

	const scenes = join(configDir, "basic", "scenes");
	const profile = join(configDir, "basic", "profiles", PROFILE);
	const logsDir = join(configDir, "logs");
	for (const d of [scenes, profile, logsDir]) mkdirSync(d, { recursive: true });
	writeFileSync(join(configDir, MARKER), "");

	let collectionName = "SLTest";
	if (collection) {
		if (!existsSync(collection)) throw new Error(`scene collection not found: ${collection}`);
		collectionName = basename(collection, extname(collection));
		let json = readFileSync(collection, "utf8");
		for (const [k, v] of Object.entries(replace)) json = json.split(`{{${k}}}`).join(v);
		const unresolved = json.match(/\{\{[A-Z_]+\}\}/g);
		if (unresolved) throw new Error(`${collection} has unsubstituted placeholders: ${[...new Set(unresolved)].join(", ")}`);
		writeFileSync(join(scenes, `${collectionName}.json`), json, "utf8");
	} else {
		writeFileSync(join(scenes, `${collectionName}.json`), JSON.stringify(emptyCollection(collectionName), null, 2));
	}

	const user = ini({
		General: {
			FirstRun: "true",
			ConfirmOnExit: "false",
			EnableAutoUpdates: "false",
			BrowserHWAccel: "false",
		},
		Basic: {
			Profile: PROFILE,
			ProfileDir: PROFILE,
			SceneCollection: collectionName,
			SceneCollectionFile: collectionName,
		},
	});
	// OBS 30+ reads user.ini; older builds read global.ini. Writing both costs nothing.
	for (const f of ["user.ini", "global.ini"]) writeFileSync(join(configDir, f), user, "utf8");

	// A small canvas, so a software rasterizer has less to do per frame.
	writeFileSync(join(profile, "basic.ini"), ini({
		General: { Name: PROFILE },
		Video: { BaseCX: 1280, BaseCY: 720, OutputCX: 1280, OutputCY: 720, FPSCommon: 30 },
	}), "utf8");

	return { configDir, logsDir, collectionName };
}

/**
 * A collection with one empty scene, for suites that bring no collection of their own.
 * OBS fills in whatever is left out, but it does need a scene: a collection with none puts
 * the frontend in a state where scene-addressing api calls have nothing to resolve against.
 */
function emptyCollection(name) {
	return {
		name,
		current_scene: "Scene",
		current_program_scene: "Scene",
		scene_order: [{ name: "Scene" }],
		sources: [{
			id: "scene",
			versioned_id: "scene",
			name: "Scene",
			settings: { custom_size: false, id_counter: 0, items: [] },
			enabled: true,
			muted: false,
			volume: 1.0,
			flags: 0,
			mixers: 0,
			sync: 0,
			monitoring_type: 0,
			balance: 0.5,
			hotkeys: {},
			private_settings: {},
		}],
	};
}

/** The log OBS wrote most recently, for the diagnostics a failing run should carry. */
export function latestLog(logsDir) {
	if (!existsSync(logsDir)) return null;
	const files = readdirSync(logsDir)
		.filter((f) => f.endsWith(".txt"))
		.map((f) => ({ path: join(logsDir, f), mtime: statSync(join(logsDir, f)).mtimeMs }))
		.sort((a, b) => b.mtime - a.mtime);
	return files.length ? files[0].path : null;
}
