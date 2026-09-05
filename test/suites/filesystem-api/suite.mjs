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
 * user's real data in it. Almost everything this suite creates goes in one uniquely named
 * subdirectory; the rest - the download directories fs_downloadZip names for itself, and the
 * fixtures the escape probes are aimed at - is recorded path by path as it is created, and
 * only those paths are ever removed. Nothing sweeps the root for things that look like ours.
 *
 * Api names are spelled out at each cdp.call rather than passed through a helper, because a
 * literal is what test/check.mjs scans for - a wrapper taking the name as a variable would
 * quietly opt this suite out of the check that catches a misspelled call in a second instead
 * of in a fifteen-minute build.
 */

import { execFileSync, spawn } from "node:child_process";
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

/*
 * How many open handles a process holds. This is what makes the sweep observable from outside:
 * the plugin keeps one handle per child it is still tracking, and nothing in the api reports
 * how many that is.
 */
const handleCount = (pid) => {
	try {
		const out = execFileSync("powershell",
			["-NoProfile", "-NonInteractive", "-Command",
				`(Get-Process -Id ${pid} -ErrorAction SilentlyContinue).HandleCount`],
			{ encoding: "utf8", windowsHide: true, timeout: 30000 });
		return Number(out.trim()) || 0;
	} catch {
		return 0;
	}
};

// kChildReapAt in PluginJsHandler.h: the number of tracked children at which a launch sweeps.
const REAP_AT = 128;

/* ------------------------------------------------------------ containment --- */

/*
 * The places outside the root that the escape probes aim at. OUTSIDE_DIR is a sibling of the
 * root whose name starts with the root's, which is the case resolveWithinDownloads goes out of
 * its way to handle: a prefix compare without the separator check that follows it would read
 * "...\StreamlabsOBS-1234" as being inside "...\StreamlabsOBS".
 *
 * PRESENT is created for real, with contents. That matters more than it looks: an unguarded
 * fs_sha256 or fs_move pointed at something that does not exist still answers with an error,
 * and a table that accepts any error as a refusal would pass against no guard at all. Aimed at
 * a file that is really there, only a working guard can produce an error.
 */
// Conditional because ROOT is: test/check.mjs imports every suite, and it runs on a linux
// runner where APPDATA does not exist. Anything derived eagerly from a null root throws during
// that import, which fails the checks before run() can report the missing root properly.
const OUTSIDE_DIR = ROOT ? join(ROOT, "..", `StreamlabsOBS-${STAMP}`) : null;
const OUTSIDE_PRESENT = OUTSIDE_DIR ? join(OUTSIDE_DIR, "present.txt") : null;
const OUTSIDE_PRESENT_BODY = "the guard is what has to stop this being read or moved";

/*
 * Each escape is one shape of path that must never resolve, in two flavours: one aimed at the
 * file that exists, for the calls that read or consume a path, and one aimed at a name that
 * does not, for the calls that create one. `lands` is what must not appear on disk afterwards.
 */
const escapes = () => [
	{
		what: "a parent-directory traversal",
		present: `..\\StreamlabsOBS-${STAMP}\\present.txt`,
		absent: `..\\sltest-escape-${STAMP}.txt`,
		lands: join(ROOT, "..", `sltest-escape-${STAMP}.txt`),
	},
	{
		what: "a sibling directory whose name starts with the root's",
		present: `..\\StreamlabsOBS-${STAMP}\\present.txt`,
		absent: `..\\StreamlabsOBS-${STAMP}\\created.txt`,
		lands: join(OUTSIDE_DIR, "created.txt"),
	},
	{
		what: "an absolute path outside the root",
		present: OUTSIDE_PRESENT,
		absent: join(ROOT, "..", `sltest-abs-${STAMP}.txt`),
		lands: join(ROOT, "..", `sltest-abs-${STAMP}.txt`),
	},
	{
		/*
		 * No target to aim at, so this one only asks whether an unusable path is refused - and it
		 * is the one row that must skip the destructive probes.
		 *
		 * "" is not a path outside the root, it *is* the root: an unguarded resolver answers
		 * `root / ""`, which is the real Streamlabs folder with the user's real data in it. Every
		 * other row can be handed to fs_remove because the worst it can destroy is a fixture this
		 * suite created. This one would hand the user's whole folder to a recursive delete, at the
		 * exact moment the guard meant to prevent that has regressed.
		 */
		what: "an empty path",
		present: "",
		absent: "",
		lands: null,
		sparesDestructive: true,
	},
];

/*
 * Every function that takes a path, and a plausible call to it. Each closure spells its own api
 * name out, so the table does not hide those names from test/check.mjs.
 *
 * "aims" picks which flavour of the escape it is given: the calls that read or consume a path
 * are aimed at the file that exists, so that removing the guard would make them succeed rather
 * than fail for the unrelated reason that there was nothing there.
 *
 * fs_remove is called with force=true on purpose: "a missing path is not an error" must not be
 * allowed to swallow "this path is not yours".
 *
 * fs_runSlExe is confined too but is not in here, because none of these paths is an exe: it
 * would answer "CreateProcess failed" whether or not it checked. It gets a probe of its own,
 * pointed at a real one.
 */
const GUARDED = [
	{ fn: "fs_mkdir", aims: "absent", call: (cdp, p) => cdp.call("fs_mkdir", p) },
	{ fn: "fs_writeFile", aims: "absent", call: (cdp, p) => cdp.call("fs_writeFile", p, "escaped", false) },
	{ fn: "fs_move (destination)", aims: "absent", call: (cdp, p) => cdp.call("fs_move", P("keep.txt"), p) },
	{ fn: "fs_exists", aims: "present", call: (cdp, p) => cdp.call("fs_exists", p) },
	{ fn: "fs_sha256", aims: "present", call: (cdp, p) => cdp.call("fs_sha256", p) },

	/*
	 * recursive=false, which is not a weakening: containment is what is under test, and force is
	 * the flag that could plausibly swallow it. Passing recursive as well would mean that a probe
	 * aimed at a directory could empty it, and the value of that over `rm` on one file is nil
	 * against the cost of being wrong.
	 */
	{ fn: "fs_remove", aims: "present", destroys: true, call: (cdp, p) => cdp.call("fs_remove", p, false, true) },
	{ fn: "fs_move (source)", aims: "present", destroys: true, call: (cdp, p) => cdp.call("fs_move", p, P("moved-in")) },
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
	timeoutMs: 420000,

	async run({ cdp, obs, say }) {
		const r = results();
		const launched = []; // pids fs_runSlExe handed back that have not been stopped yet
		const downloadDirs = []; // the directories fs_downloadZip made, noted as it made them
		const outsiders = []; // processes node started itself, to try the api on something not its own
		let zipServer = null;

		if (!ROOT) {
			r.fail("APPDATA is set", "the sandbox root cannot be derived without it");
			return r.list;
		}

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

			// The file every "present" probe is aimed at. Written by node, outside the root, so
			// that only the guard can be the reason a call fails to read or move it.
			mkdirSync(OUTSIDE_DIR, { recursive: true });
			writeFileSync(OUTSIDE_PRESENT, OUTSIDE_PRESENT_BODY);

			for (const esc of escapes()) {
				await r.step(`${esc.what} is refused by every function that takes a path`, async () => {
					const allowed = [];
					const tried = [];

					for (const guard of GUARDED) {
						if (guard.destroys && esc.sparesDestructive) continue;
						tried.push(guard.fn);

						const res = await guard.call(cdp, guard.aims === "present" ? esc.present : esc.absent);
						const bad = diag(guard.fn, res);
						if (bad) return bad;
						if (!res.error) allowed.push(`${guard.fn} answered ${short(res)}`);
					}

					if (!tried.length) return "no probe ran";

					// Believing the error messages is not enough. Whatever they said, nothing may
					// have been created outside the root, and the file that was already there has
					// to still be there, unread is not checkable but unmoved and unchanged are.
					const leaked = esc.lands && existsSync(esc.lands);
					const lost = !existsSync(OUTSIDE_PRESENT) ||
						readFileSync(OUTSIDE_PRESENT, "utf8") !== OUTSIDE_PRESENT_BODY;

					const notes = [
						...allowed,
						leaked ? `${esc.lands} was created` : null,
						lost ? `${OUTSIDE_PRESENT} was removed or altered` : null,
					].filter(Boolean);

					if (notes.length) return notes.join("; ");
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
			const sys32 = join(process.env.SystemRoot || "C:\\Windows", "System32");

			const stageRunner = (as) => {
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
				// Cleanup has nothing left to do about this one, and must not go near the pid again:
				// the plugin has closed its handle, so windows is free to hand the number to
				// somebody else.
				const at = launched.indexOf(pid);
				if (at !== -1) launched.splice(at, 1);
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
				// Through start(), not a bare call: if the guard ever regresses this launch
				// succeeds, and the pid has to be on the cleanup list before the step gives up on
				// it - a test for a stray process must not leave one.
				const res = await start(outside, true);
				const bad = wantError("fs_runSlExe", res);
				if (bad) return bad;

				if (/CreateProcess/i.test(res.error)) return `it tried to launch it: ${res.error}`;
				if (countByImage("charmap.exe") > before) return "a charmap.exe started anyway";
			});

			/*
			 * Both calls are documented as answering only about processes fs_runSlExe started, and
			 * the way to show that is a process this one could have stopped and did not.
			 *
			 * A protected pid like the System process proves nothing here: an implementation that
			 * blindly terminated whatever it was handed would fail on it too and look identical.
			 * So node starts one of its own - same user, ordinary rights, genuinely stoppable - and
			 * the assertion is that it is still running afterwards.
			 *
			 * ping rather than the staged runner: no window, and it gives up on its own if any of
			 * this goes wrong.
			 */
			const outsider = spawn("ping", ["-n", "60", "127.0.0.1"], { windowsHide: true, stdio: "ignore" });
			outsiders.push(outsider);

			// A spawn failure arrives as an asynchronous "error" event, and node treats one with no
			// listener as an uncaught exception - so without this, a machine where ping cannot be
			// started ends the whole harness instead of reaching the skip below.
			let outsiderFailed = null;
			outsider.on("error", (e) => { outsiderFailed = e; });

			const outsiderUp = await until(
				() => outsiderFailed || (outsider.pid && isRunning(outsider.pid)),
				{ timeoutMs: 10000, everyMs: 200 });

			if (!outsiderUp || outsiderFailed) {
				r.skip("a process we did not start is left alone",
					`node could not start a process to try it on${outsiderFailed ? `: ${outsiderFailed.message}` : ""}`);
			} else {
				await r.step("a process we did not start is not reported as running", async () => {
					const res = await cdp.call("sys_isProcessRunning", outsider.pid);
					const bad = diag("sys_isProcessRunning", res);
					if (bad) return bad;
					if (res.running !== false) return `it claimed pid ${outsider.pid} is one of ours: ${short(res)}`;
				});

				await r.step("a process we did not start cannot be stopped", async () => {
					const res = await cdp.call("sys_stopProcess", outsider.pid);
					const bad = diag("sys_stopProcess", res);
					if (bad) return bad;
					if (res.success !== false) return `expected success:false, got ${short(res)}`;

					// The assertion that matters: it was refused, and the process is untouched.
					if (!isRunning(outsider.pid)) return `pid ${outsider.pid} was terminated anyway`;
				});
			}

			await r.step("fs_runSlExe reports an exe that is not there", async () => {
				const res = await cdp.call("fs_runSlExe", P("no-such-program.exe"), true);
				const bad = diag("fs_runSlExe", res);
				if (bad) return bad;
				if (res.success !== false) return `expected success:false, got ${short(res)}`;
				if (!res.error) return "no error explaining why";
			});

			/* ------------------------------------------ the sweep at 128 children --- */

			/*
			 * Nothing above this point comes near the reaper. The suite launches a handful of
			 * children and asks about every one, which releases each handle where it stands, so
			 * the sweep only exists for the caller that launches and never asks - and it takes a
			 * caller that does exactly that to reach it.
			 *
			 * The measurement is obs64's handle count, because the plugin holds one handle per
			 * child it is still tracking and nothing in the api reports how many that is. The
			 * arrangement below is what makes that number mean something:
			 *
			 *   fill    REAP_AT - 1 children, one short of the mark, so no sweep happens yet
			 *   settle  wait until every one of them has exited - the sweep can only release a
			 *           child that has, so tripping it while they are alive would prove nothing
			 *   held    the count now, holding that many dead children
			 *   trip    one more launch, which reaches the mark and sweeps
			 *   after   the count again
			 *
			 * A first attempt launched all of them in one burst and compared the count across the
			 * whole run. That passed here and failed on a slower machine, where the children were
			 * still alive when the mark was reached: the sweep correctly released nothing, and the
			 * test called correct behaviour a leak. Waiting for them to exit first is the fix, and
			 * only one thing happens between `held` and `after`, so the difference between those
			 * two is attributable in a way a delta across 128 api calls is not.
			 *
			 * hostname.exe is the child: it prints a line and exits, wants neither a console nor a
			 * window, and needs no resources beside it. They are deliberately not put on the
			 * cleanup list - each has ended by itself long before the end of the suite, and 128
			 * pids there would mean 128 stop calls for processes that are already gone.
			 */
			const quickExit = join(sys32, "hostname.exe");
			const FILL = REAP_AT - 1;

			const fill = async () => {
				for (let i = 0; i < FILL; i++) {
					const res = await cdp.call("fs_runSlExe", P("quickexit.exe"), true);
					if (res?.success !== true) return `launch ${i + 1} of ${FILL} failed: ${short(res)}`;
				}
				return null;
			};

			let sweepSetup = null; // {baseline, held} once the fill is done and quiet

			if (!obs?.pid) {
				r.skip("the sweep releases the handles of children nobody asked about",
					"--no-launch, so there is no known obs process to measure");
			} else if (!existsSync(quickExit)) {
				r.skip("the sweep releases the handles of children nobody asked about",
					`${quickExit} is not there to use as a short-lived child`);
			} else {
				copyFileSync(quickExit, A("quickexit.exe"));

				const baseline = handleCount(obs.pid);
				const failed = await fill();
				say(`filled to ${FILL} children, waiting for them to exit`);

				// Generous: a machine that scans each new image on execution takes far longer over
				// this than one that does not, and being slow is not the same as being broken.
				const quiet = await until(() => countByImage("quickexit.exe") === 0, { timeoutMs: 120000, everyMs: 500 });

				if (failed) {
					r.fail("the sweep releases the handles of children nobody asked about", failed);
				} else if (!quiet) {
					r.skip("the sweep releases the handles of children nobody asked about",
						`${countByImage("quickexit.exe")} of the ${FILL} short-lived children were still running after two minutes, so there is nothing for a sweep to release`);
				} else {
					sweepSetup = { baseline, held: handleCount(obs.pid) };
				}
			}

			if (sweepSetup) {
				await r.step("the sweep releases the handles of children nobody asked about", async () => {
					const { baseline, held } = sweepSetup;

					// One more reaches the mark. Everything already tracked has exited, so a working
					// sweep releases all of it here.
					const res = await cdp.call("fs_runSlExe", P("quickexit.exe"), true);
					if (res?.success !== true) return `the launch that should trip the sweep failed: ${short(res)}`;

					await until(() => countByImage("quickexit.exe") === 0, { timeoutMs: 30000, everyMs: 250 });
					const after = handleCount(obs.pid);

					r.info("obs handles", `${baseline} at rest, ${held} holding ${FILL} exited children, ${after} after the sweep`);

					// Checked before the sweep is judged: if the handles were never held in the
					// first place, a count that does not fall says nothing about the sweep.
					if (held - baseline < FILL / 2) {
						return `holding ${FILL} exited children only moved the count by ${held - baseline}, so this check cannot see the handles it is meant to be counting`;
					}

					if (held - after < FILL / 2) {
						return `the sweep released ${held - after} handles of the ${FILL} exited children it was holding`;
					}
				});
			}

			/* ------------------------------------- fs_downloadZip's sha256 gate --- */

			const zip = buildZip(ZIP_FILES);
			const digest = sha256(zip);
			zipServer = await serveBytes(zip);
			say(`serving the fixture zip at ${zipServer.url}`);

			/*
			 * What is missing from an unpack, judged from disk rather than from the reply: a gate
			 * that refused and unpacked anyway would still have answered with an error.
			 *
			 * Every entry is looked up by name and read back. Counting how many of the returned
			 * paths exist is not the same thing - the same path twice would count as two - and the
			 * contents are what say the file came out of this archive rather than a previous one.
			 */
			const notUnpacked = (res) => {
				const paths = Array.isArray(res) ? res.map((e) => e?.path).filter(Boolean) : [];
				const problems = [];

				for (const entry of ZIP_FILES) {
					const tail = `\\${entry.name.replaceAll("/", "\\")}`;
					const hit = paths.find((p) => p.endsWith(tail));

					if (!hit) problems.push(`${entry.name} is not among ${paths.join(", ") || "no paths at all"}`);
					else if (!existsSync(hit)) problems.push(`${entry.name} was named but is not on disk`);
					else if (readFileSync(hit, "utf8") !== entry.contents) {
						problems.push(`${entry.name} reads ${JSON.stringify(readFileSync(hit, "utf8"))}`);
					}
				}

				return problems;
			};

			/*
			 * Every download, with the directory it created noted at the time.
			 *
			 * JS_DOWNLOAD_ZIP names that directory after a thread id and a timestamp and does not
			 * say which one it chose, and on the refusal path it answers with no paths at all. It
			 * has to be identified from the difference across this one call: sweeping the root for
			 * digit-named directories afterwards would also carry off one the real Streamlabs had
			 * created in the meantime, in the folder holding the user's actual downloads.
			 */
			const downloadZip = async (...args) => {
				const names = () => new Set(existsSync(ROOT) ? readdirSync(ROOT) : []);
				const before = names();
				const res = await cdp.call("fs_downloadZip", ...args);

				const created = [...names()].filter((n) => !before.has(n)).map((n) => join(ROOT, n));
				downloadDirs.push(...created);
				return { res, created };
			};

			let transportWorks = false;

			await r.step("fs_downloadZip downloads and unpacks with no checksum given", async () => {
				const { res } = await downloadZip(zipServer.url);
				const bad = diag("fs_downloadZip", res);
				if (bad) return bad;
				if (!Array.isArray(res)) return `expected an array of paths, got ${short(res)}`;

				const problems = notUnpacked(res);
				if (problems.length) return problems.join("; ");
				transportWorks = true;
			});

			// Separated from the gate's own checks so that a wininet or proxy problem reads as
			// "the download did not happen" rather than as "the checksum is broken".
			const gate = async (name, fn) => {
				if (!transportWorks) return r.skip(name, "the plain download did not work, so the gate cannot be judged");
				return r.step(name, fn);
			};

			await gate("a matching checksum is accepted, whatever its case", async () => {
				const { res } = await downloadZip(zipServer.url, digest.toUpperCase());
				const bad = diag("fs_downloadZip", res);
				if (bad) return bad;
				if (!Array.isArray(res)) return `expected an array of paths, got ${short(res)}`;

				const problems = notUnpacked(res);
				if (problems.length) return problems.join("; ");
			});

			await gate("a mismatched checksum refuses, and unpacks nothing", async () => {
				const { res, created } = await downloadZip(zipServer.url, "0".repeat(64));
				const bad = wantError("fs_downloadZip", res);
				if (bad) return bad;

				// The folder it downloaded into may hold download.zip and nothing else. Anything
				// from the archive appearing in it means the gate ran after the unpacker.
				for (const dir of created) {
					const extra = readdirSync(dir).filter((n) => n.toLowerCase() !== "download.zip");
					if (extra.length) return `${dir} also holds ${extra.join(", ")}`;
				}
			});

			// The error names both digests. The one it computed has to be the fixture's, or the
			// comparison passing is luck rather than the file being what it claims to be.
			await gate("the refusal names the digest the file actually has", async () => {
				const { res } = await downloadZip(zipServer.url, "0".repeat(64));
				const bad = wantError("fs_downloadZip", res);
				if (bad) return bad;
				if (!String(res.error).includes(digest)) return `error was ${JSON.stringify(res.error)}, expected it to name ${digest}`;
			});

			/* -------------------------------------------------- a path that is not utf-8 --- */

			/*
			 * A javascript string may hold an unpaired surrogate, and what reaches the plugin then
			 * is not valid utf-8. The conversion behind every path in this api throws on that, and
			 * nothing above it catches, so this used to end the process.
			 *
			 * Which of "refused" or "sanitised somewhere in the transport" happens is not the
			 * assertion - either is a fine answer. That the plugin is still answering afterwards
			 * is. A crash shows up here as a callback that never fires.
			 */
			await r.step("a path that is not valid utf-8 does not take the plugin down", async () => {
				const lone = "\ud800";

				/*
				 * Not just the one call. fs_mkdir reaches the conversion through the resolver, which
				 * answers for itself, but fs_pathJoin and sys_getEnvVar convert their argument
				 * directly - a fix in the resolver alone leaves those two able to end the process,
				 * which is the shape this regression had the first time.
				 */
				const probes = [
					["fs_mkdir", () => cdp.call("fs_mkdir", `${SANDBOX}\\lone-${lone}-surrogate`)],
					["fs_pathJoin", () => cdp.call("fs_pathJoin", lone, "tail")],
					["fs_pathJoin", () => cdp.call("fs_pathJoin", "head", lone)],
					["sys_getEnvVar", () => cdp.call("sys_getEnvVar", lone)],
				];

				for (const [label, invoke] of probes) {
					const res = await invoke();
					const bad = diag(label, res);
					if (bad) return bad;

					// Refused, or sanitised somewhere in the transport - either is a fine answer.
					// That the plugin is still there is the assertion; a crash reads as the next
					// callback never firing.
					const after = await cdp.call("sl_getVersionInfo");
					if (diag("sl_getVersionInfo", after)) return `the plugin stopped answering after ${label}`;
				}
			});
		} catch (e) {
			r.fail("the suite ran to the end", String(e?.message || e).split("\n")[0]);
		} finally {
			await zipServer?.close();

			/*
			 * Anything still running has to go, or it holds runner.exe open and the sandbox will
			 * not delete. Through the plugin rather than process.kill: sys_stopProcess works from
			 * the handle it kept and refuses a pid it does not own, whereas a pid whose handle has
			 * already been closed may by now belong to somebody else's process entirely. Only the
			 * ones stopAndWait did not already account for are in this list.
			 *
			 * If the plugin cannot answer, the job object takes them down when OBS exits.
			 */
			for (const pid of launched) {
				try {
					await cdp.call("sys_stopProcess", pid);
				} catch { /* obs is already gone, and the job object has it */ }
			}

			// Node's own children are a different matter: it holds the handle, so the pid cannot
			// have been recycled underneath it and kill() is addressing what it thinks it is.
			for (const child of outsiders) {
				try {
					child.kill();
				} catch { /* already gone */ }
			}

			/*
			 * Everything this suite is known to have created, named individually - not "whatever
			 * appeared in the root while we were running", which is the user's business.
			 *
			 * Each is attempted on its own and a failure only recorded. The likeliest one is the
			 * sandbox, held open by a child that outlived the suite; letting that throw would
			 * abandon every path after it and leave the rest in the user's real AppData.
			 */
			const created = [A(), OUTSIDE_DIR, ...downloadDirs, ...escapes().map((e) => e.lands)].filter(Boolean);
			const refused = [];

			for (const path of created) {
				try {
					rmSync(path, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
				} catch (e) {
					refused.push(`${path} (${String(e?.message || e).split("\n")[0]})`);
				}
			}

			const leftovers = [...new Set([...refused, ...created.filter((p) => existsSync(p))])];

			r.check("nothing was left behind on disk", !leftovers.length,
				`${leftovers.join(", ")} - a launched process may still be holding one open`);
		}

		return r.list;
	},
};
