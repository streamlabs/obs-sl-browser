/*
 * The zip fs_downloadZip is pointed at, and the server it is downloaded from.
 *
 * Built here rather than committed, because a fixture that lives in the repository is a
 * binary nobody can review and a hash somebody has to keep in step with it. Both the archive
 * and the digest the suite asserts against come out of this file, so they cannot disagree.
 *
 * The entries are stored, not deflated: a stored zip is a header, the bytes, and a directory
 * entry, which is short enough to read - and the suite is testing the checksum gate in front
 * of the unpacker, not the unpacker's inflate path.
 *
 * zlib.crc32 needs Node 22.2+; the harness already asks for 22.
 */

import { createServer } from "node:http";
import { crc32 } from "node:zlib";

const LOCAL_SIG = 0x04034b50;
const CENTRAL_SIG = 0x02014b50;
const EOCD_SIG = 0x06054b50;

// 1980-01-01, the epoch of the MS-DOS timestamp a zip carries. Fixed rather than "now" so the
// same file list always produces the same bytes, and so the same digest.
const DOS_DATE = 0x0021;
const DOS_TIME = 0x0000;

function entry(name, contents, offset) {
	// Zip paths are forward-slashed regardless of platform. minizip hands the name through to
	// std::filesystem, which is happy either way, but the format is not ours to reinterpret.
	const nameBytes = Buffer.from(name.replaceAll("\\", "/"), "utf8");
	const data = Buffer.from(contents, "utf8");
	const crc = crc32(data);

	const local = Buffer.alloc(30);
	local.writeUInt32LE(LOCAL_SIG, 0);
	local.writeUInt16LE(20, 4); // version needed
	local.writeUInt16LE(0, 6); // flags
	local.writeUInt16LE(0, 8); // method 0 = stored
	local.writeUInt16LE(DOS_TIME, 10);
	local.writeUInt16LE(DOS_DATE, 12);
	local.writeUInt32LE(crc, 14);
	local.writeUInt32LE(data.length, 18); // compressed
	local.writeUInt32LE(data.length, 22); // uncompressed
	local.writeUInt16LE(nameBytes.length, 26);
	local.writeUInt16LE(0, 28); // extra

	const central = Buffer.alloc(46);
	central.writeUInt32LE(CENTRAL_SIG, 0);
	central.writeUInt16LE(20, 4); // version made by
	central.writeUInt16LE(20, 6); // version needed
	central.writeUInt16LE(0, 8);
	central.writeUInt16LE(0, 10);
	central.writeUInt16LE(DOS_TIME, 12);
	central.writeUInt16LE(DOS_DATE, 14);
	central.writeUInt32LE(crc, 16);
	central.writeUInt32LE(data.length, 20);
	central.writeUInt32LE(data.length, 24);
	central.writeUInt16LE(nameBytes.length, 28);
	central.writeUInt16LE(0, 30); // extra
	central.writeUInt16LE(0, 32); // comment
	central.writeUInt16LE(0, 34); // disk number
	central.writeUInt16LE(0, 36); // internal attributes
	central.writeUInt32LE(0, 38); // external attributes
	central.writeUInt32LE(offset, 42);

	return {
		local: Buffer.concat([local, nameBytes, data]),
		central: Buffer.concat([central, nameBytes]),
	};
}

/** @param {{name: string, contents: string}[]} files */
export function buildZip(files) {
	const locals = [];
	const centrals = [];
	let offset = 0;

	for (const f of files) {
		const e = entry(f.name, f.contents, offset);
		offset += e.local.length;
		locals.push(e.local);
		centrals.push(e.central);
	}

	const body = Buffer.concat(locals);
	const directory = Buffer.concat(centrals);

	const eocd = Buffer.alloc(22);
	eocd.writeUInt32LE(EOCD_SIG, 0);
	eocd.writeUInt16LE(0, 4); // this disk
	eocd.writeUInt16LE(0, 6); // disk with the directory
	eocd.writeUInt16LE(files.length, 8);
	eocd.writeUInt16LE(files.length, 10);
	eocd.writeUInt32LE(directory.length, 12);
	eocd.writeUInt32LE(body.length, 16);
	eocd.writeUInt16LE(0, 20); // comment

	return Buffer.concat([body, directory, eocd]);
}

/**
 * Serve one buffer, on its own ephemeral port, to anyone who asks.
 *
 * Not the harness's server: that one serves the suite directory, and putting a generated
 * archive there would mean writing a binary into the repository for the length of the run.
 *
 * Content-Length is set explicitly because WindowsFunctions::DownloadFile asks for it with
 * HttpQueryInfo and gives up when it is absent - a chunked reply would fail the download
 * before the checksum this suite is about ever got a chance to run.
 */
export async function serveBytes(bytes, { name = "payload.zip" } = {}) {
	const server = createServer((req, res) => {
		res.writeHead(200, {
			"Content-Type": "application/zip",
			"Content-Length": bytes.length,
			"Cache-Control": "no-store",
		});
		res.end(req.method === "HEAD" ? undefined : bytes);
	});

	await new Promise((resolve, reject) => {
		server.once("error", reject);
		server.listen(0, "127.0.0.1", () => {
			server.removeListener("error", reject);
			resolve();
		});
	});

	return {
		url: `http://127.0.0.1:${server.address().port}/${name}`,
		close: () => new Promise((r) => server.close(r)),
	};
}
