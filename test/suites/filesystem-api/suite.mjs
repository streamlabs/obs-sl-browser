/*
 * The filesystem and process api the updater work added: does each call do what its comment
 * in JavascriptApi.h says, and does it stay inside the folder it is confined to?
 *
 * Every path-taking function here resolves its argument against %APPDATA%\StreamlabsOBS and
 * refuses anything landing outside it. That containment check is the part most worth
 * protecting - it is the only thing between a page and the rest of the disk - so it gets a
 * table of its own below, run against every function that takes a path, including the two
 * ends of fs_move separately.
 *
 * Node's own fs is the oracle, not fs_readFile and fs_exists. A suite that checked the api
 * with the api would agree with itself about a path resolved to the wrong place, the same
 * reason source-message reports over http rather than through the channel it is testing.
 *
 * Node also owns cleanup, because the sandbox root is the real Streamlabs folder with the
 * user's real data in it. Everything this suite creates goes in one uniquely named
 * subdirectory; the only thing removed outside it is a download folder that appeared during
 * the run and carries JS_DOWNLOAD_ZIP's all-digit name.
 *
 * Api names are spelled out at each cdp.call rather than passed through a helper, because a
 * literal is what test/check.mjs scans for - a wrapper taking the name as a variable would
 * quietly opt this suite out of the check that catches a misspelled call in a second instead
 * of in a fifteen-minute build.
 */

import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";

import { results, until } from "../../harness/suite.mjs";
import { buildZip, serveBytes } from "./fixture.mjs";

const STAMP = Date.now();
const SANDBOX = `sltest-fs-${STAMP}`;

// PluginJsHandler::getDownloadsDir() is CSIDL_APPDATA + "\StreamlabsOBS". Derived here rather
// than asked for, so the first check can compare the two and catch the api resolving anywhere
// else.
const ROOT = process.env.APPDATA ? join(process.env.APPDATA, "StreamlabsOBS") : null;

/** A path the way the api takes one: relative to the sandbox root. */
const P = (...parts) => [SANDBOX, ...parts].join("\\");
/** The same path the way Node sees it. */
const A = (...parts) => join(ROOT, SANDBOX, ...parts);

const sha256 = (buf) => createHash("sha256").update(buf).digest("hex");

/* --------------------------------------------------------------- replies --- */

/** The wrapper's three diagnostic shapes, told apart from a real answer. */
const diag = (fn, res) =>
	res?.__missing ? `${fn} is not exposed`
		: res?.__timeout ? `${fn}: the callback never fired`
			: res?.__raw !== undefined ? `${fn}: the reply was not JSON: ${String(res.__raw).slice(0, 120)}`
				: null;

const short = (res) => JSON.stringify(res)?.slice(0, 160);

const wantSuccess = (fn, res) =>
	diag(fn, res) ||
	(res.error ? `${fn}: ${res.error}` : res.success === true ? null : `expected success, got ${short(res)}`);

const wantError = (fn, res) =>
	diag(fn, res) || (res.error ? null : `expected an error, got ${short(res)}`);

/* -------------------------------------------------------------- processes --- */

/*
 * Whether a pid names a process that is still running.
 *
 * Deliberately not OpenProcess, and so not node's process.kill(pid, 0) either: windows keeps a
 * terminated process's object - and with it a resolvable pid - for as long as anyone holds a
 * handle to it, and the plugin holds one for every child it starts. A probe that only asks
 * whether the pid resolves cannot tell a live child from a dead one the plugin has not let go
 * of, which is the whole distinction sys_isProcessRunning is being judged on. tasklist
 * enumerates running processes, so it draws that line in the right place.
 */
const isRunning = (pid) => {
	try {
		const out = execFileSync("tasklist", ["/FI", `PID eq ${pid}`, "/NH", "/FO", "CSV"],
			{ encoding: "utf8", windowsHide: true, timeout: 15000 });
		return out.trim().startsWith('"');
	} catch {
		return false;
	}
};

/*
 * How many processes are running under an image name. The escape probe points fs_runSlExe at a
 * real exe outside the folder, where "it was refused" has to mean the process never started -
 * not merely that the reply said no.
 */
const countByImage = (image) => {
	try {
		const out = execFileSync("tasklist", ["/FI", `IMAGENAME eq ${image}`, "/NH", "/FO", "CSV"],
			{ encoding: "utf8", windowsHide: true, timeout: 15000 });
		return out.split("\n").filter((line) => line.trim().startsWith('"')).length;
	} catch {
		return 0;
	}
};

/*
 * The handle of the process's main window, or 0 when it has none that is visible - which is
 * what hideWindow asks for. MainWindowHandle enumerates the process's top-level windows and
 * passes over the ones IsWindowVisible refuses, so a window hidden by SW_HIDE reads as 0 here
 * even though it exists.
 */
const mainWindow = (pid) => {
	try {
		const out = execFileSync("powershell",
			["-NoProfile", "-NonInteractive", "-Command",
				`(Get-Process -Id ${pid} -ErrorAction SilentlyContinue).MainWindowHandle`],
			{ encoding: "utf8", windowsHide: true, timeout: 30000 });
		return Number(out.trim()) || 0;
	} catch {
		return 0;
	}
};

/* ------------------------------------------------------------ containment --- */

/*
 * Paths that must never resolve. Each one names a real place on disk, so the assertion is not
 * only that the call reported an error but that the file it would have written is not there.
 *
 * The second is the case resolveWithinDownloads goes out of its way to handle: a sibling whose
 * name starts with the root's. A prefix compare without the separator check that follows it
 * would let "...\StreamlabsOBS-1234" through as being inside "...\StreamlabsOBS".
 */
const escapes = () => [
	{
		what: "a parent-directory traversal",
		path: `..\\sltest-escape-${STAMP}.txt`,
		lands: join(ROOT, "..", `sltest-escape-${STAMP}.txt`),
	},
	{
		what: "a sibling directory whose name starts with the root's",
		path: `..\\StreamlabsOBS-${STAMP}\\escape.txt`,
		lands: join(ROOT, "..", `StreamlabsOBS-${STAMP}`),
	},
	{
		what: "an absolute path outside the root",
		path: join(ROOT, "..", `sltest-abs-${STAMP}.txt`),
		lands: join(ROOT, "..", `sltest-abs-${STAMP}.txt`),
	},
	{
		what: "an empty path",
		path: "",
		lands: null,
	},
];

/*
 * Every function that takes a path, and a plausible call to it. Each closure spells its own
 * api name out, so the table does not hide those names from test/check.mjs.
 *
 * fs_remove is called with force=true on purpose: "a missing path is not an error" must not
 * be allowed to swallow "this path is not yours".
 *
 * fs_runSlExe is confined too but is not in here, because none of these paths is an exe: it
 * would answer "CreateProcess failed" whether or not it checked, and the table could not tell
 * a refusal from a failed launch. It gets a probe of its own, pointed at a real exe.
 */
const GUARDED = [
	["fs_mkdir", (cdp, p) => cdp.call("fs_mkdir", p)],
	["fs_exists", (cdp, p) => cdp.call("fs_exists", p)],
	["fs_remove", (cdp, p) => cdp.call("fs_remove", p, true, true)],
	["fs_move (source)", (cdp, p) => cdp.call("fs_move", p, P("moved-in"))],
	["fs_move (destination)", (cdp, p) => cdp.call("fs_move", P("keep.txt"), p)],
	["fs_sha256", (cdp, p) => cdp.call("fs_sha256", p)],
	["fs_writeFile", (cdp, p) => cdp.call("fs_writeFile", p, "escaped", false)],
];

/* ---------------------------------------------------------------- fixture --- */

const ZIP_FILES = [
	{ name: "hello.txt", contents: "hello from the fixture\n" },
	{ name: "nested/deep.txt", contents: "one level down\n" },
];

// Bytes, not characters: fs_writeFile is documented as writing raw bytes with no newline
// translation, so this carries a CRLF, a bare LF, the characters json escapes, and a
// non-ASCII one whose utf-8 encoding is three bytes long.
const EXACT = 'first\r\nsecond\n\ttab "quote" \\backslash\\ check ✓\r\n';

// Every character outside ascii is more than one utf-8 byte, which is the whole point: widening
// the name a byte at a time produces a different name, and a launch that fails as file-not-found.
const UNICODE_RUNNER = "runner-ünïcodé.exe";

export default {
	name: "filesystem-api",
	description: "the sandboxed filesystem and process api, and the folder it is confined to",
	timeoutMs: 300000,

	async run({ cdp, say }) {
		const r = results();
		const launched = []; // pids fs_runSlExe handed back, so cleanup can be sure they are gone
		let zipServer = null;

		if (!ROOT) {
			r.fail("APPDATA is set", "the sandbox root cannot be derived without it");
			return r.list;
		}

		// Anything in the root now is the user's, or a previous run's. Only names that appear
		// during this run are ever removed.
		const preexisting = new Set(existsSync(ROOT) ? readdirSync(ROOT) : []);
		r.info("sandbox", A());

		/* ------------------------------------------------ where the root is --- */

		await r.step("the api resolves paths against %APPDATA%\\StreamlabsOBS", async () => {
			const env = await cdp.call("sys_getEnvVar", "APPDATA");
			const bad = diag("sys_getEnvVar", env);
			if (bad) return bad;
			if (env.error) return env.error;

			// Exact equality, not a prefix match. GetEnvironmentVariableW is sized by a first
			// call whose answer includes the null terminator and filled by a second whose answer
			// does not; getting that resize wrong leaves a trailing NUL on the value.
			if (env.value !== process.env.APPDATA) {
				return `sys_getEnvVar returned ${JSON.stringify(env.value)}, node has ${JSON.stringify(process.env.APPDATA)}`;
			}

			const made = await cdp.call("fs_mkdir", SANDBOX);
			const badMade = wantSuccess("fs_mkdir", made);
			if (badMade) return badMade;
			if (!existsSync(A())) return `fs_mkdir reported success but ${A()} is not on disk`;
		});

		if (!existsSync(A())) {
			r.info("nothing downstream could run", "every later check writes inside the sandbox");
			return r.list;
		}

		try {
			/* ---------------------------------------------------- sys_getEnvVar --- */

			await r.step("an unset environment variable is an error, not an empty string", async () => {
				const res = await cdp.call("sys_getEnvVar", `SL_TEST_UNSET_${STAMP}`);
				const bad = wantError("sys_getEnvVar", res);
				if (bad) return bad;
				if (res.value !== undefined) return `it also returned a value: ${JSON.stringify(res.value)}`;
			});

			await r.step("an empty variable name is an error", async () =>
				wantError("sys_getEnvVar", await cdp.call("sys_getEnvVar", "")));

			/* ------------------------------------------------------- fs_pathJoin --- */

			await r.step("fs_pathJoin joins with the windows separator", async () => {
				const res = await cdp.call("fs_pathJoin", "alpha", "beta");
				const bad = diag("fs_pathJoin", res);
				if (bad) return bad;
				if (res.path !== "alpha\\beta") return `got ${JSON.stringify(res.path)}`;
			});

			await r.step("fs_pathJoin normalises forward slashes", async () => {
				const res = await cdp.call("fs_pathJoin", "alpha/beta", "gamma/delta");
				const bad = diag("fs_pathJoin", res);
				if (bad) return bad;
				if (res.path !== "alpha\\beta\\gamma\\delta") return `got ${JSON.stringify(res.path)}`;
			});

			await r.step("fs_pathJoin passes a lone segment through", async () => {
				const left = await cdp.call("fs_pathJoin", "", "beta");
				const right = await cdp.call("fs_pathJoin", "alpha", "");
				const bad = diag("fs_pathJoin", left) || diag("fs_pathJoin", right);
				if (bad) return bad;
				if (left.path !== "beta") return `empty first segment gave ${JSON.stringify(left.path)}`;
				if (right.path !== "alpha") return `empty second segment gave ${JSON.stringify(right.path)}`;
			});

			await r.step("fs_pathJoin with nothing to join is an error", async () =>
				wantError("fs_pathJoin", await cdp.call("fs_pathJoin", "", "")));

			/* -------------------------------------------- fs_mkdir and fs_exists --- */

			await r.step("fs_mkdir creates missing parents in one call", async () => {
				const bad = wantSuccess("fs_mkdir", await cdp.call("fs_mkdir", P("a", "b", "c")));
				if (bad) return bad;
				if (!existsSync(A("a", "b", "c"))) return `${A("a", "b", "c")} is not on disk`;
			});

			await r.step("fs_mkdir on a directory that already exists succeeds", async () =>
				wantSuccess("fs_mkdir", await cdp.call("fs_mkdir", P("a", "b", "c"))));

			await r.step("fs_exists reports a directory as one", async () => {
				const res = await cdp.call("fs_exists", P("a", "b"));
				const bad = diag("fs_exists", res);
				if (bad) return bad;
				if (res.exists !== true || res.isDirectory !== true) return short(res);
			});

			await r.step("fs_exists reports a missing path without erroring", async () => {
				const res = await cdp.call("fs_exists", P("no-such-thing"));
				const bad = diag("fs_exists", res);
				if (bad) return bad;
				if (res.error) return `errored instead of answering: ${res.error}`;
				if (res.exists !== false || res.isDirectory !== false) return short(res);
			});

			/* ----------------------------------------------------- fs_writeFile --- */

			await r.step("fs_writeFile writes the exact bytes it was given", async () => {
				const res = await cdp.call("fs_writeFile", P("exact.txt"), EXACT, false);
				const bad = wantSuccess("fs_writeFile", res);
				if (bad) return bad;

				const expected = Buffer.from(EXACT, "utf8");
				if (res.bytesWritten !== expected.length) {
					return `bytesWritten was ${res.bytesWritten}, the content is ${expected.length} bytes`;
				}
				if (!readFileSync(A("exact.txt")).equals(expected)) {
					return `on disk: ${JSON.stringify(readFileSync(A("exact.txt")).toString("utf8"))}`;
				}
			});

			await r.step("fs_exists reports a file as not a directory", async () => {
				const res = await cdp.call("fs_exists", P("exact.txt"));
				const bad = diag("fs_exists", res);
				if (bad) return bad;
				if (res.exists !== true || res.isDirectory !== false) return short(res);
			});

			await r.step("fs_writeFile appends when asked to and truncates when not", async () => {
				let bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("append.txt"), "first", false));
				if (bad) return bad;
				bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("append.txt"), "+second", true));
				if (bad) return bad;
				if (readFileSync(A("append.txt"), "utf8") !== "first+second") {
					return `after appending: ${JSON.stringify(readFileSync(A("append.txt"), "utf8"))}`;
				}

				bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("append.txt"), "x", false));
				if (bad) return bad;
				if (readFileSync(A("append.txt"), "utf8") !== "x") {
					return `after overwriting: ${JSON.stringify(readFileSync(A("append.txt"), "utf8"))}`;
				}
			});

			await r.step("fs_writeFile does not create the parent directory for you", async () => {
				const bad = wantError("fs_writeFile", await cdp.call("fs_writeFile", P("missing-parent", "f.txt"), "x", false));
				if (bad) return bad;
				if (existsSync(A("missing-parent"))) return "it created the directory anyway";
			});

			await r.step("fs_writeFile onto a directory is an error", async () =>
				wantError("fs_writeFile", await cdp.call("fs_writeFile", P("a"), "x", false)));

			/* -------------------------------------------------------- fs_sha256 --- */

			// The two published vectors, hardcoded on purpose: computing them here with node
			// would only prove the api agrees with node about what sha-256 is.
			await r.step("fs_sha256 matches the published digest for 'abc'", async () => {
				let bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("abc.txt"), "abc", false));
				if (bad) return bad;

				const res = await cdp.call("fs_sha256", P("abc.txt"));
				bad = diag("fs_sha256", res);
				if (bad) return bad;
				if (res.error) return res.error;
				if (res.sha256 !== "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
					return `got ${res.sha256}`;
				}
			});

			await r.step("fs_sha256 hashes an empty file", async () => {
				let bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("empty.bin"), "", false));
				if (bad) return bad;

				const res = await cdp.call("fs_sha256", P("empty.bin"));
				bad = diag("fs_sha256", res);
				if (bad) return bad;
				if (res.sha256 !== "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") {
					return `got ${res.sha256}`;
				}
			});

			// sha256File reads in 64 KiB chunks, so a file that fits in one buffer never exercises
			// the loop that feeds the next block to BCryptHashData. Written by node rather than
			// through the api, both to keep 200 KB out of a CDP evaluate and so the bytes hashed
			// are unambiguously the bytes node hashed.
			await r.step("fs_sha256 hashes a file larger than its read buffer", async () => {
				const big = Buffer.alloc(200_000);
				for (let i = 0; i < big.length; i++) big[i] = (i * 31) & 0xff;
				writeFileSync(A("large.bin"), big);

				const res = await cdp.call("fs_sha256", P("large.bin"));
				const bad = diag("fs_sha256", res);
				if (bad) return bad;
				if (res.error) return res.error;
				if (res.sha256 !== sha256(big)) return `got ${res.sha256}, expected ${sha256(big)}`;
			});

			await r.step("fs_sha256 refuses a directory and a missing file", async () => {
				const onDir = await cdp.call("fs_sha256", P("a"));
				const onMissing = await cdp.call("fs_sha256", P("no-such-file"));
				return wantError("fs_sha256 (directory)", onDir) || wantError("fs_sha256 (missing)", onMissing);
			});

			/* -------------------------------------------------------- fs_remove --- */

			await r.step("fs_remove deletes a file", async () => {
				const bad = wantSuccess("fs_remove", await cdp.call("fs_remove", P("abc.txt"), false, false));
				if (bad) return bad;
				if (existsSync(A("abc.txt"))) return "the file is still there";
			});

			await r.step("fs_remove deletes an empty directory without recursive", async () => {
				let bad = wantSuccess("fs_mkdir", await cdp.call("fs_mkdir", P("empty-dir")));
				if (bad) return bad;
				bad = wantSuccess("fs_remove", await cdp.call("fs_remove", P("empty-dir"), false, false));
				if (bad) return bad;
				if (existsSync(A("empty-dir"))) return "the directory is still there";
			});

			await r.step("fs_remove refuses a non-empty directory without recursive", async () => {
				const bad = wantError("fs_remove", await cdp.call("fs_remove", P("a"), false, false));
				if (bad) return bad;
				// A refusal that had already deleted part of the tree would be worse than the removal.
				if (!existsSync(A("a", "b", "c"))) return "it refused but the tree is gone anyway";
			});

			await r.step("fs_remove with recursive deletes a non-empty directory", async () => {
				const bad = wantSuccess("fs_remove", await cdp.call("fs_remove", P("a"), true, false));
				if (bad) return bad;
				if (existsSync(A("a"))) return "the tree is still there";
			});

			await r.step("fs_remove on a missing path is an error without force", async () =>
				wantError("fs_remove", await cdp.call("fs_remove", P("never-existed"), false, false)));

			await r.step("fs_remove on a missing path succeeds with force", async () =>
				wantSuccess("fs_remove", await cdp.call("fs_remove", P("never-existed"), false, true)));

			/* ---------------------------------------------------------- fs_move --- */

			await r.step("fs_move renames a file and keeps its contents", async () => {
				let bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("from.txt"), "payload", false));
				if (bad) return bad;
				bad = wantSuccess("fs_move", await cdp.call("fs_move", P("from.txt"), P("to.txt")));
				if (bad) return bad;

				if (existsSync(A("from.txt"))) return "the source is still there";
				if (!existsSync(A("to.txt"))) return "the destination was not created";
				if (readFileSync(A("to.txt"), "utf8") !== "payload") {
					return `contents changed: ${JSON.stringify(readFileSync(A("to.txt"), "utf8"))}`;
				}
			});

			await r.step("fs_move will not overwrite an existing destination", async () => {
				let bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("other.txt"), "keep me", false));
				if (bad) return bad;

				bad = wantError("fs_move", await cdp.call("fs_move", P("other.txt"), P("to.txt")));
				if (bad) return bad;

				// Both ends untouched: refusing and then destroying either one would be the worst
				// outcome of the two the refusal exists to prevent.
				if (readFileSync(A("to.txt"), "utf8") !== "payload") return "the destination was overwritten";
				if (readFileSync(A("other.txt"), "utf8") !== "keep me") return "the source was consumed";
			});

			await r.step("fs_move reports a missing source", async () =>
				wantError("fs_move", await cdp.call("fs_move", P("not-here.txt"), P("wherever.txt"))));

			await r.step("fs_move moves a directory and everything under it", async () => {
				let bad = wantSuccess("fs_mkdir", await cdp.call("fs_mkdir", P("tree", "inner")));
				if (bad) return bad;
				bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("tree", "inner", "leaf.txt"), "leaf", false));
				if (bad) return bad;

				bad = wantSuccess("fs_move", await cdp.call("fs_move", P("tree"), P("moved-tree")));
				if (bad) return bad;

				if (existsSync(A("tree"))) return "the source tree is still there";
				if (!existsSync(A("moved-tree", "inner", "leaf.txt"))) return "the leaf did not come with it";
			});

			/* ----------------------------------------------------- containment --- */

			// Referenced by the fs_move rows of the table, so the only thing that can make those
			// calls fail is the path being tested.
			await cdp.call("fs_writeFile", P("keep.txt"), "keep", false);

			await r.step("a '..' that stays inside the sandbox is allowed", async () => {
				const bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", P("down", "..", "inside.txt"), "ok", false));
				if (bad) return bad;
				if (!existsSync(A("inside.txt"))) return `it did not land at ${A("inside.txt")}`;
			});

			await r.step("forward slashes are accepted in a path", async () => {
				const bad = wantSuccess("fs_writeFile", await cdp.call("fs_writeFile", `${SANDBOX}/fwd.txt`, "ok", false));
				if (bad) return bad;
				if (!existsSync(A("fwd.txt"))) return `it did not land at ${A("fwd.txt")}`;
			});

			for (const esc of escapes()) {
				await r.step(`${esc.what} is refused by every function that takes a path`, async () => {
					const allowed = [];
					for (const [label, invoke] of GUARDED) {
						const res = await invoke(cdp, esc.path);
						const bad = diag(label, res);
						if (bad) return bad;
						if (!res.error) allowed.push(`${label} answered ${short(res)}`);
					}

					// Believing the error messages is not enough: the assertion is that the file is
					// not there, whatever any of them said.
					const leaked = esc.lands && existsSync(esc.lands);
					if (allowed.length) {
						return allowed.join("; ") + (leaked ? ` - and ${esc.lands} exists` : "");
					}
					if (leaked) return `every call reported an error, yet ${esc.lands} exists`;
				});
			}

			/* ------------------------------------------- processes we started --- */

			/*
			 * Staging something to launch is fussier than it looks.
			 *
			 * fs_runSlExe passes no command line, so the exe has to be one that stays up on its
			 * own with nothing to go on. It cannot be a console application: the process that
			 * starts it has no console of its own to pass down, so a copy of cmd.exe reaches eof
			 * on its first read and is gone before the next call can ask about it. That leaves a
			 * gui app, whose lifetime does not depend on a console at all.
			 *
			 * Copying one out of System32 is not enough by itself either. These are MUI binaries
			 * whose strings live in <locale>\<name>.exe.mui beside them, and one that cannot load
			 * its resources exits before it draws anything. Two things follow. The lookup goes by
			 * the name the exe is given, so the copy's .mui is renamed with it. And it goes by the
			 * machine's ui language, which is not knowable from here - System32 carries a folder
			 * per installed language and picking one of them is a coin toss - so every language
			 * the exe ships in comes along and the loader takes the one it wants.
			 */
			const stageRunner = (as) => {
				const sys32 = join(process.env.SystemRoot || "C:\\Windows", "System32");
				for (const name of ["charmap.exe", "mmc.exe"]) {
					if (!existsSync(join(sys32, name))) continue;

					const locales = readdirSync(sys32, { withFileTypes: true })
						.filter((d) => d.isDirectory() && existsSync(join(sys32, d.name, `${name}.mui`)))
						.map((d) => d.name);
					if (!locales.length) continue;

					copyFileSync(join(sys32, name), A(as));
					for (const locale of locales) {
						mkdirSync(A(locale), { recursive: true });
						copyFileSync(join(sys32, locale, `${name}.mui`), A(locale, `${as}.mui`));
					}
					return `${name} (${locales.join(", ")})`;
				}
				return null;
			};

			const staged = stageRunner("runner.exe");
			if (staged) r.info("runner.exe", `a copy of ${staged}`);
			else r.skip("a process can be launched at all", "no gui exe in System32 could be staged");

			const start = async (fileName, hideWindow) => {
				const res = await cdp.call("fs_runSlExe", fileName, hideWindow);
				if (Number.isInteger(res?.pid)) launched.push(res.pid);
				return res;
			};

			const stopAndWait = async (pid) => {
				const bad = wantSuccess("sys_stopProcess", await cdp.call("sys_stopProcess", pid));
				if (bad) return bad;
				if (!(await until(() => !isRunning(pid), { timeoutMs: 15000, everyMs: 250 }))) {
					return `pid ${pid} is still running 15s after being stopped`;
				}
			};

			let shown = null;
			let hidden = null;

			if (staged) {
				await r.step("fs_runSlExe starts a process and returns its pid", async () => {
					shown = await start(P("runner.exe"), false);
					const bad = wantSuccess("fs_runSlExe", shown);
					if (bad) return bad;
					if (!Number.isInteger(shown.pid) || shown.pid <= 0) return `pid was ${JSON.stringify(shown.pid)}`;
					if (!(await until(() => isRunning(shown.pid), { timeoutMs: 15000, everyMs: 250 }))) {
						return `nothing with pid ${shown.pid} is running`;
					}
				});

				await r.step("sys_isProcessRunning sees it", async () => {
					if (!shown?.pid) return "nothing was started";
					const res = await cdp.call("sys_isProcessRunning", shown.pid);
					const bad = diag("sys_isProcessRunning", res);
					if (bad) return bad;
					if (res.running !== true) return short(res);
				});

				// The launch used to be keyed by filename and refused while a previous one was
				// still alive. It is keyed by pid now, so the same exe twice over is a supported
				// call and each instance is addressable on its own.
				await r.step("the same exe can be started twice, each with its own pid", async () => {
					if (!shown?.pid) return "nothing was started";
					hidden = await start(P("runner.exe"), true);
					const bad = wantSuccess("fs_runSlExe", hidden);
					if (bad) return bad;
					if (hidden.pid === shown.pid) return `both launches reported pid ${hidden.pid}`;

					for (const pid of [shown.pid, hidden.pid]) {
						const res = await cdp.call("sys_isProcessRunning", pid);
						if (res.running !== true) return `pid ${pid} is not running: ${short(res)}`;
					}
				});

				/*
				 * "No window yet" and "no window ever" look the same from out here, so the visible
				 * instance calibrates the wait: it has to produce a window before the hidden one's
				 * lack of one means anything, and the hidden one is then given three times as long
				 * as it needed. A slow machine makes this pass rather than fail, which is the right
				 * way round for a check that depends on timing.
				 */
				const startedAt = Date.now();
				const sawWindow = shown?.pid
					? await until(() => mainWindow(shown.pid) !== 0, { timeoutMs: 30000, everyMs: 500 })
					: null;
				const budget = Math.max(5000, (Date.now() - startedAt) * 3);

				if (!sawWindow) {
					r.skip("hideWindow starts the process without a visible window",
						"the instance started with hideWindow=false never showed a window either, so there is nothing to compare against");
				} else {
					await r.step("hideWindow starts the process without a visible window", async () => {
						if (!hidden?.pid) return "the hidden instance never started";
						if (await until(() => mainWindow(hidden.pid) !== 0, { timeoutMs: budget, everyMs: 500 })) {
							return `pid ${hidden.pid} has a visible main window`;
						}
					});
				}

				await r.step("sys_stopProcess terminates it", async () => {
					if (!shown?.pid) return "nothing was started";
					return stopAndWait(shown.pid);
				});

				await r.step("sys_isProcessRunning no longer sees it", async () => {
					if (!shown?.pid) return "nothing was started";
					const res = await cdp.call("sys_isProcessRunning", shown.pid);
					const bad = diag("sys_isProcessRunning", res);
					if (bad) return bad;
					if (res.running !== false) return short(res);
				});

				await r.step("stopping the same pid twice reports an unknown pid", async () => {
					if (!shown?.pid) return "nothing was started";
					const res = await cdp.call("sys_stopProcess", shown.pid);
					const bad = diag("sys_stopProcess", res);
					if (bad) return bad;
					if (res.success !== false) return `expected success:false, got ${short(res)}`;
					if (!res.error) return "no error explaining why";
				});

				await r.step("the hidden instance stops too", async () => {
					if (!hidden?.pid) return "the hidden instance never started";
					return stopAndWait(hidden.pid);
				});

				// fs_downloadZip answers with absolute paths, so handing one of them straight back
				// is the obvious next call. Relative to the folder and absolute inside it are the
				// two shapes the contract admits.
				await r.step("fs_runSlExe takes an absolute path inside the folder", async () => {
					const res = await start(A("runner.exe"), true);
					const bad = wantSuccess("fs_runSlExe", res);
					if (bad) return bad;
					if (!(await until(() => isRunning(res.pid), { timeoutMs: 15000, everyMs: 250 }))) {
						return `nothing with pid ${res.pid} is running`;
					}
					return stopAndWait(res.pid);
				});

				// The name used to be widened a byte at a time, which turns anything outside ascii
				// into a different name and then into a file that is not there. It goes through the
				// same utf-8 conversion as every other path now.
				await r.step("fs_runSlExe takes a filename outside ascii", async () => {
					if (!stageRunner(UNICODE_RUNNER)) return "the runner could not be staged under that name";
					const res = await start(P(UNICODE_RUNNER), true);
					const bad = wantSuccess("fs_runSlExe", res);
					if (bad) return bad;
					if (!(await until(() => isRunning(res.pid), { timeoutMs: 15000, everyMs: 250 }))) {
						return `nothing with pid ${res.pid} is running`;
					}
					return stopAndWait(res.pid);
				});
			}

			/*
			 * The one function whose containment cannot be judged from the reply alone. Pointed at a
			 * real, launchable exe outside the folder: refusing has to mean CreateProcess was never
			 * reached, and no such process appeared.
			 */
			await r.step("fs_runSlExe will not run an exe outside the folder", async () => {
				const outside = join(process.env.SystemRoot || "C:\\Windows", "System32", "charmap.exe");
				if (!existsSync(outside)) return "charmap.exe is not there to try to run";

				const before = countByImage("charmap.exe");
				const res = await cdp.call("fs_runSlExe", outside, true);
				const bad = wantError("fs_runSlExe", res);
				if (bad) return bad;

				if (/CreateProcess/i.test(res.error)) return `it tried to launch it: ${res.error}`;
				if (countByImage("charmap.exe") > before) return "a charmap.exe started anyway";
			});

			// pid 4 is the System process: real, running, and not one of ours. Both calls are
			// documented as answering only about processes fs_runSlExe started.
			await r.step("a process we did not start is not reported as running", async () => {
				const res = await cdp.call("sys_isProcessRunning", 4);
				const bad = diag("sys_isProcessRunning", res);
				if (bad) return bad;
				if (res.running !== false) return `sys_isProcessRunning claimed pid 4 is ours: ${short(res)}`;
			});

			await r.step("a process we did not start cannot be stopped", async () => {
				const res = await cdp.call("sys_stopProcess", 4);
				const bad = diag("sys_stopProcess", res);
				if (bad) return bad;
				if (res.success !== false) return `expected success:false, got ${short(res)}`;
				if (!isRunning(4)) return "pid 4 was terminated";
			});

			await r.step("fs_runSlExe reports an exe that is not there", async () => {
				const res = await cdp.call("fs_runSlExe", P("no-such-program.exe"), true);
				const bad = diag("fs_runSlExe", res);
				if (bad) return bad;
				if (res.success !== false) return `expected success:false, got ${short(res)}`;
				if (!res.error) return "no error explaining why";
			});

			/* ------------------------------------- fs_downloadZip's sha256 gate --- */

			const zip = buildZip(ZIP_FILES);
			const digest = sha256(zip);
			zipServer = await serveBytes(zip);
			say(`serving the fixture zip at ${zipServer.url}`);

			// Whether the archive was unpacked, judged from disk rather than from the reply: a
			// gate that refuses and unpacks anyway would still answer with an error.
			const unpacked = (res) => {
				const paths = Array.isArray(res) ? res.map((e) => e?.path).filter(Boolean) : [];
				return paths.filter((p) => existsSync(p));
			};

			let transportWorks = false;

			await r.step("fs_downloadZip downloads and unpacks with no checksum given", async () => {
				const res = await cdp.call("fs_downloadZip", zipServer.url);
				const bad = diag("fs_downloadZip", res);
				if (bad) return bad;
				if (!Array.isArray(res)) return `expected an array of paths, got ${short(res)}`;

				const files = unpacked(res);
				if (files.length !== ZIP_FILES.length) {
					return `${files.length} of ${ZIP_FILES.length} entries are on disk: ${short(res)}`;
				}
				const hello = files.find((p) => p.endsWith("hello.txt"));
				if (!hello) return `no hello.txt among ${files.join(", ")}`;
				if (readFileSync(hello, "utf8") !== ZIP_FILES[0].contents) {
					return `hello.txt reads ${JSON.stringify(readFileSync(hello, "utf8"))}`;
				}
				transportWorks = true;
			});

			// Separated from the gate's own checks so that a wininet or proxy problem reads as
			// "the download did not happen" rather than as "the checksum is broken".
			const gate = async (name, fn) => {
				if (!transportWorks) return r.skip(name, "the plain download did not work, so the gate cannot be judged");
				return r.step(name, fn);
			};

			await gate("a matching checksum is accepted, whatever its case", async () => {
				const res = await cdp.call("fs_downloadZip", zipServer.url, digest.toUpperCase());
				const bad = diag("fs_downloadZip", res);
				if (bad) return bad;
				if (!Array.isArray(res)) return `expected an array of paths, got ${short(res)}`;
				if (unpacked(res).length !== ZIP_FILES.length) return `only ${unpacked(res).length} entries are on disk`;
			});

			await gate("a mismatched checksum refuses, and unpacks nothing", async () => {
				const wrong = "0".repeat(64);
				const before = new Set(existsSync(ROOT) ? readdirSync(ROOT) : []);

				const res = await cdp.call("fs_downloadZip", zipServer.url, wrong);
				const bad = wantError("fs_downloadZip", res);
				if (bad) return bad;

				// The folder it downloaded into may hold download.zip and nothing else. Anything
				// from the archive appearing in it means the gate ran after the unpacker.
				const created = (existsSync(ROOT) ? readdirSync(ROOT) : []).filter((n) => !before.has(n));
				for (const dir of created) {
					const contents = readdirSync(join(ROOT, dir));
					const extra = contents.filter((n) => n.toLowerCase() !== "download.zip");
					if (extra.length) return `${dir} also holds ${extra.join(", ")}`;
				}
			});

			// The error names both digests. The one it computed has to be the fixture's, or the
			// comparison passing is luck rather than the file being what it claims to be.
			await gate("the refusal names the digest the file actually has", async () => {
				const res = await cdp.call("fs_downloadZip", zipServer.url, "0".repeat(64));
				const bad = wantError("fs_downloadZip", res);
				if (bad) return bad;
				if (!String(res.error).includes(digest)) return `error was ${JSON.stringify(res.error)}, expected it to name ${digest}`;
			});
		} catch (e) {
			r.fail("the suite ran to the end", String(e?.message || e).split("\n")[0]);
		} finally {
			await zipServer?.close();

			// Nothing launched may outlive the suite. The job object would take them down with
			// OBS anyway, but a leaked child holds runner.exe open and blocks the cleanup below.
			for (const pid of launched) {
				try {
					process.kill(pid);
				} catch { /* already gone, which is the point */ }
			}

			rmSync(A(), { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });

			// Only what appeared during this run, and only under the name JS_DOWNLOAD_ZIP gives
			// its folders: GetCurrentThreadId() followed by a millisecond timestamp.
			for (const name of existsSync(ROOT) ? readdirSync(ROOT) : []) {
				if (preexisting.has(name) || !/^\d+$/.test(name)) continue;
				rmSync(join(ROOT, name), { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
			}

			// Whatever the escape checks concluded, none of it may be left on disk.
			for (const esc of escapes()) {
				if (esc.lands) rmSync(esc.lands, { recursive: true, force: true });
			}

			r.check("nothing was left behind in the streamlabs folder", !existsSync(A()),
				`${A()} is still on disk - a launched process may still be holding it open`);
		}

		return r.list;
	},
};
