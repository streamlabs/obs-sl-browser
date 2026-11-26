/*
 * Do obs_source_filter_add and obs_source_filter_remove always answer, and say what is wrong?
 *
 * They used to reply with nothing at all, whatever happened, because the reply was written after
 * the call had already returned. Every reply here is checked for being a json object that says
 * which of the names is the problem.
 *
 * Not covered: a filter that is found. obs makes every filter private, and the plugin looks the
 * filter up by name among the public sources, so no filter can be named to either call yet.
 */

import { results } from "../../harness/suite.mjs";

const describe = (reply) => JSON.stringify(reply)?.slice(0, 200);

export default {
	name: "source-filters",
	description: "obs_source_filter_add and obs_source_filter_remove answer with an error that names what is wrong",
	collection: "collection.json",
	timeoutMs: 120000,

	async run({ cdp }) {
		const r = results();

		// The callback never firing reads as __timeout, which is not an error string and fails here too
		const wantError = (reply, pattern) => {
			if (typeof reply?.error !== "string") return `expected an error, got ${describe(reply)}`;
			if (!pattern.test(reply.error)) return `the error is ${JSON.stringify(reply.error)}, wanted ${pattern}`;
		};

		await r.step("add: an unknown source is an error", async () =>
			wantError(await cdp.call("obs_source_filter_add", "NoSuchSource", "FltCorrection"), /source named NoSuchSource/));

		await r.step("add: an unknown filter is an error", async () =>
			wantError(await cdp.call("obs_source_filter_add", "FltOther", "NoSuchFilter"), /filter named NoSuchFilter/));

		await r.step("add: a source that is not a filter is an error", async () =>
			wantError(await cdp.call("obs_source_filter_add", "FltOther", "FltColor"), /FltColor is not a filter/));

		await r.step("remove: an unknown source is an error", async () =>
			wantError(await cdp.call("obs_source_filter_remove", "NoSuchSource", "FltCorrection"), /source named NoSuchSource/));

		await r.step("remove: an unknown filter is an error", async () =>
			wantError(await cdp.call("obs_source_filter_remove", "FltColor", "NoSuchFilter"), /filter named NoSuchFilter/));

		await r.step("remove: a source that is not a filter is an error", async () =>
			wantError(await cdp.call("obs_source_filter_remove", "FltOther", "FltColor"), /not attached to FltOther/));

		return r.list;
	},
};
