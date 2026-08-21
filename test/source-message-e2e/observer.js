// Independent observation channel for the browser-source message e2e test.
// Serves the harness pages and records whatever either page reports. Deliberately NOT the
// channel under test: the pages report over plain HTTP to this process, so a total failure
// of javascript_event shows up here as silence rather than as a missing observer.
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = 28951;
const events = [];

function record(tag, data) {
	const e = {t: new Date().toISOString(), tag, data};
	events.push(e);
	console.log(`[${e.t}] ${tag}: ${JSON.stringify(data)}`);
}

function send(res, code, type, body, extraHeaders) {
	res.writeHead(code, Object.assign({
		'Content-Type': type,
		'Access-Control-Allow-Origin': '*',
		'Cache-Control': 'no-store'
	}, extraHeaders || {}));
	res.end(body);
}

// Sources that must each have received something. One page receiving while the other
// stays silent is the failure a single global counter would hide.
const EXPECTED_SOURCES = ['MsgSourceA', 'MsgSourceB'];

// Every payload names the source it was addressed to. A page reporting one addressed
// elsewhere means javascript_event reached the wrong browser.
function verdict() {
	const reports = events.filter((e) => e.tag === 'report').map((e) => e.data);
	const received = reports.filter((r) => r.event === 'RECEIVED');
	const sends = reports.filter((r) => r.event === 'SENT');

	const misrouted = received.filter((r) => !r.data || r.data.target !== r.who);
	const failedSends = sends.filter((s) => !/"success":\s*true/.test(String(s.data.result)));

	const perSource = {};
	received.forEach((r) => (perSource[r.who] = (perSource[r.who] || 0) + 1));

	return {
		received: received.length,
		perSource,
		misrouted: misrouted.length,
		misroutedSamples: misrouted.slice(0, 5),
		sends: sends.length,
		failedSends: failedSends.length,
		failedSendSamples: failedSends.slice(0, 5),
		missingSources: EXPECTED_SOURCES.filter((s) => !perSource[s]),
		pass:
			EXPECTED_SOURCES.every((s) => perSource[s] > 0) &&
			misrouted.length === 0 &&
			sends.length > 0 &&
			failedSends.length === 0
	};
}

const server = http.createServer((req, res) => {
	const url = new URL(req.url, `http://${req.headers.host}`);

	if (req.method === 'OPTIONS') {
		return send(res, 204, 'text/plain', '', {'Access-Control-Allow-Headers': 'content-type'});
	}

	if (req.method === 'POST' && url.pathname === '/report') {
		let body = '';
		req.on('data', (c) => (body += c));
		req.on('end', () => {
			try {
				record('report', JSON.parse(body));
			} catch (e) {
				record('report-unparseable', body.slice(0, 500));
			}
			send(res, 200, 'application/json', '{"ok":true}');
		});
		return;
	}

	if (url.pathname === '/events') {
		return send(res, 200, 'application/json', JSON.stringify(events, null, 2));
	}

	if (url.pathname === '/verdict') {
		return send(res, 200, 'application/json', JSON.stringify(verdict(), null, 2));
	}

	const file = path.join(__dirname, url.pathname === '/' ? 'index.html' : url.pathname.replace(/^\//, ''));

	if (file.endsWith('.html') && fs.existsSync(file)) {
		record('page-served', url.pathname + url.search);
		return send(res, 200, 'text/html; charset=utf-8', fs.readFileSync(file));
	}

	send(res, 404, 'text/plain', 'not found');
});

server.listen(PORT, '0.0.0.0', () => console.log(`observer listening on ${PORT}`));
