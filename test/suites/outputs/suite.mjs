/*
 * Can the main page start, pause and stop the outputs, and set the replay buffer's length?
 *
 * The calls answer as soon as the request is made, so every step that starts or stops something
 * waits for the outputStateChanged events and compares the whole tally, as obs-events does.
 *
 * The profile asks for a confirmation before stopping a recording or a stream. The button asks,
 * these calls must not, so a window that is open after one is a failure.
 *
 * Windows are compared for obs's own process only: the harness starts it minimized to the tray, so a
 * failed connection raises a toast from another process rather than a message box. obs only asks the
 * confirmations while its window is visible, so the recording stop is made with the window shown.
 *
 * Not covered: a stream that connects, and the message boxes obs opens for a start it refuses.
 * The stream is pointed at a local socket that accepts and never answers, so it stays connecting.
 */

import { existsSync, readdirSync, readFileSync } from "node:fs";
import net from "node:net";
import { join } from "node:path";

import { results, until } from "../../harness/suite.mjs";
import { rundirOf } from "../../harness/obs.mjs";
import { describeWindow, clickTrayIcon, windowList, windowState } from "../../harness/windows.mjs";

// Longer than the coalescing windows, so a held-back trailing event has arrived
const SETTLE_MS = 800;

const COLOR = JSON.stringify({ color: 4278190335, width: 100, height: 100 });

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export default {
	name: "outputs",
	description: "obs_recording_*, obs_streaming_* errors, obs_replay_buffer_getSettings and obs_replay_buffer_setDuration",
	timeoutMs: 240000,

	// A replay buffer to read the settings of, and a recording that is not the stream's encoder, which cannot pause
	profile: (workDir) => ({
		Output: { Mode: "Simple" },
		SimpleOutput: { RecRB: "true", RecRBTime: 10, RecQuality: "Small", RecEncoder: "x264", RecFormat2: "mkv", FilePath: workDir.replaceAll("\\", "/") },
	}),

	user: () => ({
		BasicWindow: { WarnBeforeStartingStream: "true", WarnBeforeStoppingStream: "true", WarnBeforeStoppingRecord: "true" },
	}),

	async run({ cdp, obs, workDir }) {
		const r = results();

		const clear = () => cdp.evaluate("window.__sltInbox.length = 0");
		const events = async () => (await cdp.inbox()).map((args) => JSON.parse(args[0]));
		const states = async () => (await events()).filter((e) => e.event === "outputStateChanged").map((e) => `${e.output}:${e.state}`).join();
		const windows = () => windowList(workDir, obs?.pid);
		const recordings = () => readdirSync(workDir).filter((f) => f.endsWith(".mkv"));

		// Clears, runs the action, waits, and gives back the output:state sequence of what obs reported
		const happens = async (action, lastState) => {
			await clear();
			await action();
			// A stop is not over when it is asked for: the file is finished first
			if (lastState) await until(async () => (await states()).endsWith(lastState), { timeoutMs: 20000, everyMs: 250 });
			await sleep(SETTLE_MS);
			return states();
		};

		const registered = await cdp.listen("slabsGlobal", "events_registerReceiver");

		if (registered.__missing)
		{
			r.fail("register the receiver", "events_registerReceiver is not exposed");
			return r.list;
		}

		// Events are held until obs has finished loading, which this page has no way to hear about,
		// so make things until one is heard
		await r.step("events start flowing once obs has finished loading", async () => {
			let n = 0;

			const heard = await until(
				async () => {
					const name = `OutProbe${n++}`;
					await clear();
					await cdp.call("obs_source_create", "color_source_v3", name, COLOR, "{}");
					await sleep(1000);
					const got = (await events()).some((e) => e.event === "sourceCreated" && e.name === name);
					await cdp.call("obs_source_destroy", name);
					return got;
				},
				{ timeoutMs: 90000, everyMs: 500 },
			);

			if (!heard) return "no event arrived in 90 s";
			await sleep(SETTLE_MS);
		});

		await r.step("obs_replay_buffer_getSettings reports the profile", async () => {
			const settings = await cdp.call("obs_replay_buffer_getSettings");

			if (settings.enabled !== true || settings.available !== true || settings.duration !== 10) return JSON.stringify(settings);
		});

		await r.step("obs_replay_buffer_setDuration refuses what is not a whole number of seconds from 5 to 21600", async () => {
			const bad = [
				["4", await cdp.call("obs_replay_buffer_setDuration", 4)],
				["21601", await cdp.call("obs_replay_buffer_setDuration", 21601)],
				["a string", await cdp.call("obs_replay_buffer_setDuration", "x")],
				["a fraction", await cdp.call("obs_replay_buffer_setDuration", 7.5)],
				["none", await cdp.call("obs_replay_buffer_setDuration")],
			].filter(([, reply]) => typeof reply.error !== "string");

			if (bad.length) return `no error for ${bad.map(([what, reply]) => `${what} (${JSON.stringify(reply)})`).join(", ")}`;
			if ((await cdp.call("obs_replay_buffer_getSettings")).duration !== 10) return "a refused duration was applied";
		});

		await r.step("obs_replay_buffer_setDuration: the new length is reported and saved to the profile", async () => {
			const reply = await cdp.call("obs_replay_buffer_setDuration", 7);

			if (reply.error) return reply.error;
			if ((await cdp.call("obs_replay_buffer_getSettings")).duration !== 7) return "getSettings still has the old duration";
			if (!obs?.exe) return;

			const ini = join(rundirOf(obs.exe), "config", "obs-studio", "basic", "profiles", "SLTest", "basic.ini");

			if (!existsSync(ini)) return `${ini} does not exist`;
			if (!/^RecRBTime=7\s*$/m.test(readFileSync(ini, "utf8"))) return `RecRBTime=7 is not in ${ini}`;
		});

		await r.step("obs_replay_buffer_setDuration while the buffer runs is accepted, and the buffer keeps running", async () => {
			const started = await happens(() => cdp.call("obs_replay_buffer_start"));

			if (started !== "replayBuffer:starting,replayBuffer:started") return `start: ${started}`;

			const reply = await cdp.call("obs_replay_buffer_setDuration", 8);

			if (reply.error) return reply.error;
			if ((await cdp.call("obs_replay_buffer_getSettings")).duration !== 8) return "getSettings still has the old duration";
			if ((await cdp.call("obs_get_output_state")).replayBuffer.state !== "started") return "the buffer stopped";

			const stopped = await happens(() => cdp.call("obs_replay_buffer_stop"), "replayBuffer:stopped");

			if (stopped !== "replayBuffer:stopping,replayBuffer:stopped") return `stop: ${stopped}`;
		});

		await r.step("recording: start is starting then started, the getter agrees, and no window opens", async () => {
			const before = windows().map((w) => w.title);
			let reply;
			const got = await happens(async () => (reply = await cdp.call("obs_recording_start")));

			if (reply?.error) return reply.error;
			if (got !== "recording:starting,recording:started") return `states ${got}`;

			const state = await cdp.call("obs_get_output_state");

			if (state.recording.state !== "started" || state.recording.paused !== false) return JSON.stringify(state.recording);
			if (typeof (await cdp.call("obs_recording_start")).error !== "string") return "no error for starting twice";

			const opened = windows().filter((w) => !before.includes(w.title));

			if (opened.length) return `windows opened: ${opened.map(describeWindow).join(", ")}`;
		});

		await r.step("recording: pause is one paused event, a second pause is an error", async () => {
			await sleep(1000);

			let reply;
			const got = await happens(async () => (reply = await cdp.call("obs_recording_pause")));

			if (reply?.error) return reply.error;
			if (got !== "recording:paused") return `states ${got}`;
			if ((await cdp.call("obs_get_output_state")).recording.paused !== true) return "the getter does not say paused";
			if (typeof (await cdp.call("obs_recording_pause")).error !== "string") return "no error for pausing twice";
		});

		await r.step("recording: unpause is one started event, a second unpause is an error", async () => {
			let reply;
			const got = await happens(async () => (reply = await cdp.call("obs_recording_unpause")));

			if (reply?.error) return reply.error;
			if (got !== "recording:started") return `states ${got}`;
			if ((await cdp.call("obs_get_output_state")).recording.paused !== false) return "the getter still says paused";
			if (typeof (await cdp.call("obs_recording_unpause")).error !== "string") return "no error for unpausing twice";
		});

		const stopRecording = async () => {
			const before = windows().map((w) => w.title);
			let reply;
			const got = await happens(async () => (reply = await cdp.call("obs_recording_stop")), "recording:stopped");

			if (reply?.error) return reply.error;
			if (got !== "recording:stopping,recording:stopped") return `states ${got}`;
			if ((await cdp.call("obs_get_output_state")).recording.state !== "stopped") return "the getter still says running";
			if (!(await until(() => recordings().length > 0, { timeoutMs: 20000, everyMs: 250 }))) return `no .mkv in ${workDir}`;

			const opened = windows().filter((w) => !before.includes(w.title));

			if (opened.length) return `windows opened: ${opened.map(describeWindow).join(", ")}`;
			if (typeof (await cdp.call("obs_recording_stop")).error !== "string") return "no error for stopping twice";
		};

		await r.step("recording: stop is stopping then stopped, the file is written, and a second stop is an error", async () => {
			const main = windows().find((w) => w.process === "obs64" && w.title.startsWith("OBS "))?.title;

			if (!main) return "obs's main window was not found";

			// The same click a user makes on the tray icon, so obs's own idea of visible changes too
			if (!clickTrayIcon(workDir, obs.pid)) return "obs has no tray icon";

			try {
				if (!(await until(() => windowState(workDir, main).visible, { timeoutMs: 5000, everyMs: 100 }))) return "obs's window did not become visible";

				return await stopRecording();
			} finally {
				clickTrayIcon(workDir, obs.pid);
				await until(() => !windowState(workDir, main).visible, { timeoutMs: 5000, everyMs: 100 });
			}
		});

		await r.step("recording: pause and unpause when nothing runs are errors", async () => {
			const bad = [
				["pause", await cdp.call("obs_recording_pause")],
				["unpause", await cdp.call("obs_recording_unpause")],
			].filter(([, reply]) => typeof reply.error !== "string");

			if (bad.length) return `no error for ${bad.map(([what, reply]) => `${what} (${JSON.stringify(reply)})`).join(", ")}`;
		});

		await r.step("streaming: a start against a server that never answers stays starting, a second start and a stop are errors, and the stream ends in stopped", async () => {
			const sockets = new Set();
			let refusing = false;
			const server = net.createServer((socket) => {
				socket.on("error", () => {});
				if (refusing) return socket.destroy();
				sockets.add(socket);
			});
			const seen = {};
			// Everything a failure needs to be understood from the report alone
			const evidence = async (why) => `${why}; ${JSON.stringify({
				at: new Date().toISOString(),
				states: await states(),
				getter: (await cdp.call("obs_get_output_state")).streaming,
				...seen,
				sockets: sockets.size,
			})}`;

			await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));

			try {
				const settings = await cdp.call("obs_set_stream_settings", "rtmp_custom", "RTMP", `rtmp://127.0.0.1:${server.address().port}/live`, false, "", "", "slt");

				if (settings.error) return settings.error;

				const before = windows().map((w) => w.title);
				let connected;
				const got = await happens(async () => {
					// The second goes out before any event could have reported the first, as with Enhanced Broadcasting's async setup
					[seen.start, seen.immediateStart] = await Promise.all([cdp.call("obs_streaming_start"), cdp.call("obs_streaming_start")]);
					connected = await until(() => sockets.size > 0, { timeoutMs: 10000, everyMs: 100 });
				});

				if (seen.start?.error) return evidence(`start refused: ${seen.start.error}`);
				if (!/Already starting/.test(seen.immediateStart?.error)) return evidence("no Already starting error for a start issued right after the first");
				if (!connected) return evidence("obs never connected");
				if (got !== "streaming:starting") return evidence(`states ${got}`);
				if ((await cdp.call("obs_get_output_state")).streaming.state !== "starting") return evidence("the getter does not say starting");

				seen.secondStart = await cdp.call("obs_streaming_start");

				if (!/Already starting/.test(seen.secondStart.error)) return evidence("no Already starting error for a second start");

				seen.stop = await cdp.call("obs_streaming_stop");

				if (!/Not running/.test(seen.stop.error)) return evidence("no Not running error for stopping a stream that is still connecting");

				// Ending the connection fails the handshake at once, rather than after librtmp's own timeout
				refusing = true;
				sockets.forEach((socket) => socket.destroy());
				server.close();

				if (!(await until(async () => (await states()).includes("streaming:stopped"), { timeoutMs: 30000, everyMs: 250 }))) return evidence("no stopped in 30 s");
				if ((await cdp.call("obs_get_output_state")).streaming.state !== "stopped") return evidence("the getter does not say stopped");

				await sleep(SETTLE_MS);

				const opened = windows().filter((w) => !before.includes(w.title));

				if (opened.length) return evidence(`windows opened: ${opened.map(describeWindow).join(", ")}`);
			} finally {
				refusing = true;
				sockets.forEach((socket) => socket.destroy());
				server.close();
			}
		});

		await r.step("streaming: stop when nothing runs is an error, and starts no event", async () => {
			const got = await happens(async () => {
				if (typeof (await cdp.call("obs_streaming_stop")).error !== "string") throw new Error("no error for stopping a stream that is not running");
			});

			if (got) return `states ${got}`;
			if ((await cdp.call("obs_get_output_state")).streaming.state !== "stopped") return "the getter does not say stopped";
		});

		return r.list;
	},
};
