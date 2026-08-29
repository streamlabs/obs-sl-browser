/*
 * Dual Output API test suite.
 *
 * Runs inside the Streamlabs browser page, which is the only context where the
 * plugin injects window.slabsGlobal. Two things drive it:
 *   - tests/dual-output-api.html  loads this with a <script> tag and renders the results
 *   - tests/run-api-tests.mjs     injects this over CDP and prints the results to a console
 *
 * Both call __slDualTests.run(). Keep this file free of module syntax and of
 * anything browser-only: CDP evaluates it as a classic script.
 */

(function () {
  "use strict";

  var V = "vertical";
  var PREFIX = "__slt_";
  var CALL_TIMEOUT_MS = 5000;

  /* Every api function takes a callback as its first argument and hands it a
     JSON string. Setters answer with an empty string on success, so "no error
     key" is the success signal. */
  function call(fn) {
    var args = Array.prototype.slice.call(arguments, 1);
    return new Promise(function (resolve) {
      var g = window.slabsGlobal;
      if (!g || typeof g[fn] !== "function") {
        resolve({ __missing: fn });
        return;
      }
      var settled = false;
      var timer = setTimeout(function () {
        if (!settled) { settled = true; resolve({ __timeout: fn }); }
      }, CALL_TIMEOUT_MS);

      g[fn].apply(g, [function (json) {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        if (!json) { resolve({}); return; }
        try { resolve(JSON.parse(json)); }
        catch (e) { resolve({ __raw: String(json) }); }
      }].concat(args));
    });
  }

  function names(arr) {
    if (!Array.isArray(arr)) return [];
    return arr.map(function (s) { return s && s.name; }).filter(Boolean);
  }

  function Fail(msg) { this.message = msg; this.__isFail = true; }

  function check(cond, msg) { if (!cond) throw new Fail(msg); }

  function checkOk(res, what) {
    if (res.__missing) throw new Fail(what + ": function not exposed (" + res.__missing + ")");
    if (res.__timeout) throw new Fail(what + ": callback never fired");
    if (res.error) throw new Fail(what + ": " + res.error);
    return res;
  }

  var tests = [];
  function test(name, fn) { tests.push({ name: name, fn: fn }); }

  /* ------------------------------------------------------------- tests --- */

  test("api surface is exposed", function () {
    var expected = [
      "dualoutput_getState", "dualoutput_setEnabled", "dualoutput_setCanvasSize",
      "dualoutput_setOutputMode", "dualoutput_setStreamSettings",
      "dualoutput_getStreamSettings", "dualoutput_startStream",
      "dualoutput_stopStream", "dualoutput_removeScene"
    ];
    var missing = expected.filter(function (n) { return typeof window.slabsGlobal[n] !== "function"; });
    check(missing.length === 0, "not exposed: " + missing.join(", "));
    return Promise.resolve();
  });

  test("getState reports the documented shape", function (ctx) {
    return call("dualoutput_getState").then(function (s) {
      checkOk(s, "getState");
      ["available", "enabled", "canvas", "active_scene", "scenes", "stream_state", "output_mode", "eb_available"]
        .forEach(function (k) { check(k in s, "missing key: " + k); });
      check(typeof s.canvas.width === "number" && typeof s.canvas.height === "number", "canvas dimensions are not numbers");
      check(Array.isArray(s.scenes), "scenes is not an array");
      ctx.state = s;
      check(s.available === true,
        "canvas is not attached (available=false); the rest will fail. Wait for the scene collection to finish loading.");
    });
  });

  test("main canvas enumeration is unchanged without a canvas argument", function (ctx) {
    return call("obs_enum_scenes").then(function (before) {
      check(Array.isArray(before), "obs_enum_scenes did not return an array");
      check(before.length > 0, "no horizontal scenes found, cannot judge isolation");
      ctx.mainScenesBefore = names(before);
    });
  });

  test("creates a scene on the vertical canvas", function (ctx) {
    ctx.sceneA = PREFIX + "a";
    return call("obs_create_scene", ctx.sceneA, V).then(function (r) {
      checkOk(r, "create vertical scene");
      return call("obs_enum_scenes", V);
    }).then(function (listed) {
      check(names(listed).indexOf(ctx.sceneA) !== -1, "created scene is not in the vertical enumeration");
    });
  });

  test("vertical scenes stay invisible to the main canvas", function (ctx) {
    return call("obs_enum_scenes").then(function (after) {
      var n = names(after);
      check(n.indexOf(ctx.sceneA) === -1, "vertical scene leaked into the main scene list");
      check(n.length === ctx.mainScenesBefore.length,
        "main scene count changed: " + ctx.mainScenesBefore.length + " -> " + n.length);
    });
  });

  test("the same scene name may exist on both canvases", function (ctx) {
    var borrowed = ctx.mainScenesBefore[0];
    return call("obs_create_scene", borrowed, V).then(function (r) {
      checkOk(r, "create '" + borrowed + "' on vertical");
      ctx.sceneCollide = borrowed;
      return call("obs_enum_scenes", V);
    }).then(function (listed) {
      check(names(listed).indexOf(ctx.sceneCollide) !== -1, "collided name missing from vertical");
    });
  });

  test("an unrecognised canvas name is refused, not redirected", function () {
    var bogus = PREFIX + "bogus";
    return call("obs_create_scene", bogus, "verticle").then(function (res) {
      check(!!res.error, "expected an error for canvas 'verticle', got: " + JSON.stringify(res));
      return call("obs_enum_scenes");
    }).then(function (main) {
      check(names(main).indexOf(bogus) === -1, "typo fell through and created the scene on the MAIN canvas");
      return call("obs_enum_scenes", V);
    }).then(function (vert) {
      check(names(vert).indexOf(bogus) === -1, "typo created the scene on the vertical canvas");
    });
  });

  test("adds a source to a vertical scene and round-trips its position", function (ctx) {
    ctx.sourceName = PREFIX + "src";
    var ids = ["color_source_v3", "color_source_v2", "color_source"];

    function tryId(i) {
      if (i >= ids.length) return Promise.resolve(null);
      return call("obs_source_create", ids[i], ctx.sourceName, "", "").then(function (r) {
        if (!r.error && !r.__missing && !r.__timeout) return ids[i];
        return tryId(i + 1);
      });
    }

    return tryId(0).then(function (made) {
      check(made, "could not create a color source of any known id");
      return call("obs_scene_add", ctx.sceneA, ctx.sourceName, V);
    }).then(function (r) {
      checkOk(r, "scene_add on vertical");
      return call("obs_sceneitem_set_pos", ctx.sceneA, ctx.sourceName, 111, 222, V);
    }).then(function (r) {
      checkOk(r, "set_pos on vertical");
      return call("obs_sceneitem_get_pos", ctx.sceneA, ctx.sourceName, V);
    }).then(function (pos) {
      checkOk(pos, "get_pos on vertical");
      check(Math.round(pos.x) === 111 && Math.round(pos.y) === 222,
        "position round-trip gave " + pos.x + "," + pos.y);
    });
  });

  test("the same source is not added to the main canvas", function (ctx) {
    return call("obs_scene_get_sources", ctx.mainScenesBefore[0]).then(function (items) {
      var listed = Array.isArray(items)
        ? items.map(function (i) { return i && (i.name || i.sourceName); })
        : [];
      check(listed.indexOf(ctx.sourceName) === -1, "the vertical item also appeared in a main scene");
    });
  });

  test("reports vertical canvas dimensions, not the main ones", function () {
    var v;
    return call("obs_canvas_get_dimensions", V).then(function (r) {
      v = checkOk(r, "vertical dimensions");
      return call("obs_canvas_get_dimensions");
    }).then(function (m) {
      checkOk(m, "main dimensions");
      check(v.width > 0 && v.height > 0, "vertical dimensions are zero");
      check(!(v.width === m.width && v.height === m.height),
        "vertical dimensions match main exactly (" + v.width + "x" + v.height + "), suspicious");
    });
  });

  test("stream settings round-trip", function (ctx) {
    return call("dualoutput_getStreamSettings").then(function (before) {
      ctx.settingsBefore = checkOk(before, "getStreamSettings");
      return call("dualoutput_setStreamSettings",
        "rtmp://127.0.0.1/slt", "slt-key", false, "", "", "obs_x264", 3210, 96, 1, false);
    }).then(function (r) {
      checkOk(r, "setStreamSettings");
      return call("dualoutput_getStreamSettings");
    }).then(function (got) {
      checkOk(got, "getStreamSettings after set");
      check(got.server === "rtmp://127.0.0.1/slt", "server not stored, got: " + got.server);
      check(got.key === "slt-key", "key not stored");
      check(got.video_bitrate === 3210, "video_bitrate not stored, got: " + got.video_bitrate);
      check(got.audio_bitrate === 96, "audio_bitrate not stored");
    });
  });

  test("a partial update keeps the values it omits", function () {
    // Empty strings and non-positive numbers are documented as "keep stored value".
    return call("dualoutput_setStreamSettings",
      "rtmp://127.0.0.1/changed", "slt-key", false, "", "", "", 0, 0, 0, false).then(function (r) {
      checkOk(r, "partial setStreamSettings");
      return call("dualoutput_getStreamSettings");
    }).then(function (got) {
      checkOk(got, "getStreamSettings after partial");
      check(got.server === "rtmp://127.0.0.1/changed", "server did not update");
      check(got.video_bitrate === 3210, "video_bitrate was clobbered by 0, got: " + got.video_bitrate);
      check(got.audio_bitrate === 96, "audio_bitrate was clobbered by 0");
      check(got.encoder_id && got.encoder_id.length > 0, "encoder_id was cleared by an empty string");
    });
  });

  test("enhanced_broadcasting mode refuses to start our own output", function (ctx) {
    return call("dualoutput_getState").then(function (s0) {
      checkOk(s0, "getState");
      ctx.modeBefore = s0.output_mode;
      return call("dualoutput_setOutputMode", "enhanced_broadcasting");
    }).then(function (r) {
      checkOk(r, "setOutputMode(eb)");
      return call("dualoutput_getState");
    }).then(function (s1) {
      check(s1.output_mode === "enhanced_broadcasting", "mode did not change, got: " + s1.output_mode);
      return call("dualoutput_startStream");
    }).then(function (started) {
      check(!!started.error, "startStream was allowed in enhanced_broadcasting mode");
      return call("dualoutput_setOutputMode", "rtmp");
    }).then(function (r) {
      checkOk(r, "setOutputMode(rtmp)");
      return call("dualoutput_getState");
    }).then(function (s2) {
      check(s2.output_mode === "rtmp", "mode did not revert to rtmp");
    });
  });

  test("an invalid output mode is rejected", function () {
    return call("dualoutput_setOutputMode", "sideways").then(function (res) {
      check(!!res.error, "expected an error for an unknown mode");
      return call("dualoutput_getState");
    }).then(function (s) {
      check(s.output_mode === "rtmp", "an invalid mode changed the stored mode");
    });
  });

  test("canvas size reports what was actually applied", function (ctx) {
    return call("dualoutput_getState").then(function (s) {
      checkOk(s, "getState");
      ctx.sizeBefore = s.canvas;
      // 1079 is not 4-aligned; width should come back aligned to a multiple of 4.
      return call("dualoutput_setCanvasSize", 1079, 1920);
    }).then(function (applied) {
      checkOk(applied, "setCanvasSize");
      check(typeof applied.width === "number", "no width returned");
      check(applied.width % 4 === 0, "returned width is not 4-aligned: " + applied.width);
      check(applied.height % 2 === 0, "returned height is not 2-aligned: " + applied.height);
    });
  });

  /* ----------------------------------------------------------- cleanup --- */

  function cleanup(ctx) {
    var removed = [];
    ctx = ctx || {};

    return call("obs_enum_scenes", V).then(function (list) {
      var doomed = names(list).filter(function (n) {
        return n.indexOf(PREFIX) === 0 || n === ctx.sceneCollide;
      });

      return doomed.reduce(function (chain, n) {
        return chain.then(function () {
          return call("dualoutput_removeScene", n).then(function (r) {
            if (!r.error) removed.push(n);
          });
        });
      }, Promise.resolve());
    }).then(function () {
      return ctx.sourceName ? call("obs_source_destroy", ctx.sourceName) : null;
    }).then(function () {
      return ctx.sizeBefore
        ? call("dualoutput_setCanvasSize", ctx.sizeBefore.width, ctx.sizeBefore.height)
        : null;
    }).then(function () {
      return ctx.modeBefore ? call("dualoutput_setOutputMode", ctx.modeBefore) : null;
    }).then(function () {
      if (!ctx.settingsBefore) return null;
      var b = ctx.settingsBefore;
      return call("dualoutput_setStreamSettings", b.server || "", b.key || "", !!b.use_auth,
        b.username || "", b.password || "", b.encoder_id || "",
        b.video_bitrate || 0, b.audio_bitrate || 0, b.audio_track || 0, !!b.auto_start);
    }).then(function () { return removed; });
  }

  /* ------------------------------------------------------------ runner --- */

  function run(onProgress) {
    var ctx = {};
    var results = [];

    function record(status, name, why) {
      var r = { status: status, name: name, why: why || "" };
      results.push(r);
      if (typeof onProgress === "function") { try { onProgress(r); } catch (e) { /* reporting must not fail a run */ } }
    }

    var chain = tests.reduce(function (prev, t) {
      return prev.then(function () {
        return Promise.resolve()
          .then(function () { return t.fn(ctx); })
          .then(function () { record("pass", t.name); })
          .catch(function (e) {
            record("fail", t.name, e && e.__isFail ? e.message : String((e && e.stack) || e).split("\n")[0]);
          });
      });
    }, Promise.resolve());

    return chain.then(function () {
      return cleanup(ctx).then(function (removed) {
        record("info", "cleanup", removed.length ? "removed: " + removed.join(", ") : "nothing to remove");
      }).catch(function (e) {
        record("fail", "cleanup", String(e));
      });
    }).then(function () {
      var pass = results.filter(function (r) { return r.status === "pass"; }).length;
      var fail = results.filter(function (r) { return r.status === "fail"; }).length;
      return { pass: pass, fail: fail, total: tests.length, results: results };
    });
  }

  globalThis.__slDualTests = {
    run: run,
    cleanup: function () { return cleanup(null); },
    count: tests.length,
    available: function () { return !!window.slabsGlobal; }
  };
})();
