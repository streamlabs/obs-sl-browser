/*
 * Dual Output API test suite.
 *
 * Runs inside the Streamlabs browser page, which is the only context where the
 * plugin injects window.slabsGlobal. Two things drive it:
 *   - page.html loads this with a <script> tag and renders the results
 *   - suite.mjs injects this over CDP and folds the results into a run
 *
 * Both call __slDualTests.run(). Keep this file free of module syntax and of
 * anything browser-only: CDP evaluates it as a classic script.
 */

(function () {
  "use strict";

  var V = "vertical";
  var PREFIX = "__slt_";

  /* The harness installs window.__slt (test/harness/inpage.js) before a suite runs, and it
     already wraps the callback-and-JSON-string convention every api function uses. Resolves
     to {__missing}, {__timeout} or {__raw} rather than rejecting; a setter answering with an
     empty string parses to {}. */
  function call() {
    return window.__slt.call.apply(null, arguments);
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
    /* Creates the name on both rather than borrowing an existing main scene: the vertical canvas
       seeds itself with a scene called "Scene" (SlDualCanvas kDefaultSceneName), which is also the
       usual first main scene, so borrowing would collide within the vertical canvas instead. */
    var shared = PREFIX + "shared";
    ctx.sceneSharedMain = shared;

    return call("obs_create_scene", shared).then(function (r) {
      checkOk(r, "create '" + shared + "' on main");
      return call("obs_create_scene", shared, V);
    }).then(function (r) {
      checkOk(r, "create '" + shared + "' on vertical");
      return call("obs_enum_scenes");
    }).then(function (m) {
      check(names(m).indexOf(shared) !== -1, "shared name missing from main");
      return call("obs_enum_scenes", V);
    }).then(function (v) {
      check(names(v).indexOf(shared) !== -1, "shared name missing from vertical");
    });
  });

  /* Deleting a background scene used to select it first, which put it on air through the transition
     and then left whichever scene sorted first on air after the delete. */
  test("removing a background vertical scene leaves the active one on air", function () {
    var doomed = PREFIX + "bg";
    var active;

    return call("obs_get_current_scene", V).then(function (r) {
      active = checkOk(r, "current vertical scene").name;
      return call("obs_create_scene", doomed, V);
    }).then(function (r) {
      checkOk(r, "create the background scene");

      // creation makes it active; put the original back so the removal is of a background scene
      return call("obs_set_current_scene", active, V);
    }).then(function (r) {
      checkOk(r, "restore the active scene");
      return call("dualoutput_removeScene", doomed);
    }).then(function (r) {
      checkOk(r, "removeScene");
      return call("obs_enum_scenes", V);
    }).then(function (listed) {
      check(names(listed).indexOf(doomed) === -1, "the background scene is still listed");
      return call("obs_get_current_scene", V);
    }).then(function (r) {
      checkOk(r, "current vertical scene after the removal");
      check(r.name === active, "the active scene changed from '" + active + "' to '" + r.name + "'");
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

  /* The handlers above resolve a scene, so an unknown canvas is refused on the way. These three
     take a canvas and no scene, so nothing else would catch a typo: before they were guarded, each
     one skipped its vertical branch and answered for the main canvas instead. */
  test("a typo is refused by the handlers that take no scene name", function () {
    var bogus = "verticle";

    return call("obs_get_current_scene", bogus).then(function (res) {
      check(!!res.error, "obs_get_current_scene answered for a canvas named '" + bogus + "': " + JSON.stringify(res));
      return call("obs_canvas_get_dimensions", bogus);
    }).then(function (res) {
      check(!!res.error, "obs_canvas_get_dimensions answered for '" + bogus + "': " + JSON.stringify(res));
      return call("obs_enum_scenes", bogus);
    }).then(function (res) {
      // This one answers with an array when it works, so an error object is the only refusal shape.
      check(!Array.isArray(res) && !!res.error,
        "obs_enum_scenes enumerated a canvas named '" + bogus + "': " + JSON.stringify(res));
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

  /* Equal main and vertical sizes are a perfectly valid setup, so "they differ" proves nothing
     about which canvas was read. Set a size on the vertical canvas and require exactly it back. */
  test("reports vertical canvas dimensions, not the main ones", function (ctx) {
    var w = 540;
    var h = 960;

    return call("dualoutput_getState").then(function (s) {
      checkOk(s, "getState");
      if (!ctx.sizeBefore) ctx.sizeBefore = s.canvas;
      return call("dualoutput_setCanvasSize", w, h);
    }).then(function (applied) {
      checkOk(applied, "setCanvasSize");
      // The canvas aligns what it is given; compare against what it said it took.
      w = applied.width;
      h = applied.height;
      return call("obs_canvas_get_dimensions", V);
    }).then(function (v) {
      checkOk(v, "vertical dimensions");
      check(v.width === w && v.height === h,
        "vertical dimensions are " + v.width + "x" + v.height + ", expected the " + w + "x" + h + " just applied");
      return call("dualoutput_getState");
    }).then(function (s) {
      checkOk(s, "getState after resize");
      check(s.canvas.width === w && s.canvas.height === h,
        "getState reports " + s.canvas.width + "x" + s.canvas.height + ", expected " + w + "x" + h);
    });
  });

  test("stream settings round-trip", function (ctx) {
    return call("dualoutput_getStreamSettings").then(function (before) {
      ctx.settingsBefore = checkOk(before, "getStreamSettings");
      return call("dualoutput_setStreamSettings",
        "rtmp://127.0.0.1/slt", "slt-key", true, "slt-user", "slt-pass", "obs_x264", 3210, 96, 1, true);
    }).then(function (r) {
      checkOk(r, "setStreamSettings");
      return call("dualoutput_getStreamSettings");
    }).then(function (got) {
      checkOk(got, "getStreamSettings after set");
      check(got.server === "rtmp://127.0.0.1/slt", "server not stored, got: " + got.server);
      check(got.key === "slt-key", "key not stored");
      check(got.username === "slt-user", "username not stored, got: " + got.username);
      check(got.password === "slt-pass", "password not stored");
      check(got.use_auth === true, "use_auth not stored");
      check(got.auto_start === true, "auto_start not stored");
      check(got.video_bitrate === 3210, "video_bitrate not stored, got: " + got.video_bitrate);
      check(got.audio_bitrate === 96, "audio_bitrate not stored");
    });
  });

  /* Every field, not just the one that happened to be guarded. The previous version passed a
     non-empty server and key, so it never exercised the case where an empty string was assigned
     straight over a stored credential. */
  test("a partial update keeps the values it omits", function () {
    // Only the video bitrate is supplied. Everything else is left out entirely - an empty string
    // is now a request to clear, which is the next test.
    return call("dualoutput_setStreamSettings",
      undefined, undefined, undefined, undefined, undefined, undefined, 4200).then(function (r) {
      checkOk(r, "partial setStreamSettings");
      return call("dualoutput_getStreamSettings");
    }).then(function (got) {
      checkOk(got, "getStreamSettings after partial");
      check(got.video_bitrate === 4200, "video_bitrate did not update, got: " + got.video_bitrate);
      check(got.server === "rtmp://127.0.0.1/slt", "server was cleared by an empty string, got: " + got.server);
      check(got.key === "slt-key", "key was cleared by an empty string");
      check(got.username === "slt-user", "username was cleared by an empty string");
      check(got.password === "slt-pass", "password was cleared by an empty string");
      check(got.use_auth === true, "use_auth was switched off by an omitted boolean");
      check(got.auto_start === true, "auto_start was switched off by an omitted boolean");
      check(got.audio_bitrate === 96, "audio_bitrate was clobbered by 0");
      check(got.encoder_id && got.encoder_id.length > 0, "encoder_id was cleared by an empty string");
    });
  });

  /* The other half of presence-means-intent, and the reason it exists: with an empty string
     indistinguishable from an omitted argument there was no way to remove a stored stream key. */
  test("an empty string clears a stored value", function () {
    return call("dualoutput_setStreamSettings", undefined, "").then(function (r) {
      checkOk(r, "setStreamSettings clearing the key");
      return call("dualoutput_getStreamSettings");
    }).then(function (got) {
      checkOk(got, "getStreamSettings after clearing");
      check(got.key === "", "key was not cleared, got: " + JSON.stringify(got.key));
      check(got.server === "rtmp://127.0.0.1/slt", "clearing the key also disturbed the server");
      return call("dualoutput_setStreamSettings", undefined, "slt-key");
    }).then(function (r) {
      checkOk(r, "restoring the key");
    });
  });

  test("a bitrate of zero is refused, not ignored", function () {
    return call("dualoutput_setStreamSettings", undefined, undefined, undefined, undefined, undefined, undefined, 0)
      .then(function (r) {
        check(!!r.error, "expected an error for video_bitrate 0, got: " + JSON.stringify(r));
        return call("dualoutput_getStreamSettings");
      }).then(function (got) {
        checkOk(got, "getStreamSettings after a refused bitrate");
        check(got.video_bitrate === 4200, "a refused bitrate still changed the stored one, got: " + got.video_bitrate);
      });
  });

  test("an out of range audio track is refused, not clamped", function () {
    return call("dualoutput_setStreamSettings", undefined, undefined, undefined, undefined, undefined, undefined,
                undefined, undefined, 99)
      .then(function (r) {
        check(!!r.error, "expected an error for audio track 99, got: " + JSON.stringify(r));
        return call("dualoutput_getStreamSettings");
      }).then(function (got) {
        checkOk(got, "getStreamSettings after a refused track");
        // The refusal has to leave the stored track alone, or it is a clamp with extra steps.
        check(got.audio_track === 1, "a refused track still changed the stored one, got: " + got.audio_track);
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
      if (!ctx.sizeBefore) ctx.sizeBefore = s.canvas;
      // 1079 is not 4-aligned; width should come back aligned to a multiple of 4.
      return call("dualoutput_setCanvasSize", 1079, 1920);
    }).then(function (applied) {
      checkOk(applied, "setCanvasSize");
      check(typeof applied.width === "number", "no width returned");
      check(applied.width % 4 === 0, "returned width is not 4-aligned: " + applied.width);
      check(applied.height % 2 === 0, "returned height is not 2-aligned: " + applied.height);
    });
  });

  /* Last, because it briefly starts an output. The size guard has no other coverage: every other
     test runs with the stream idle, which is the branch that was already correct. */
  test("a resize is refused while the stream is live", function (ctx) {
    return call("dualoutput_getState").then(function (s) {
      checkOk(s, "getState");
      if (!ctx.sizeBefore) ctx.sizeBefore = s.canvas;
      if (!ctx.modeBefore) ctx.modeBefore = s.output_mode;
      // Nothing is listening there. startStream returns once the output is accepted, not once it
      // connects, and "starting" is already not "idle" - which is all the guard looks at.
      // Server and key only - everything else keeps whatever the earlier tests left.
      return call("dualoutput_setStreamSettings", "rtmp://127.0.0.1:1/none", "k");
    }).then(function (r) {
      checkOk(r, "setStreamSettings for the live check");
      return call("dualoutput_startStream");
    }).then(function (started) {
      checkOk(started, "startStream");
      check(started.stream_state !== "idle", "the output never left idle, nothing to guard against");
      return call("dualoutput_setCanvasSize", 720, 1280);
    }).then(function (res) {
      check(!!res.error, "a resize was accepted while the stream was live: " + JSON.stringify(res));
      return call("dualoutput_getState");
    }).then(function (s) {
      // The refusal has to leave the stored size alone: reporting a size the canvas never took is
      // the actual defect, and it outlives the stream.
      check(!(s.canvas.width === 720 && s.canvas.height === 1280),
        "the refused size was stored anyway, getState reports " + s.canvas.width + "x" + s.canvas.height);
      return call("dualoutput_stopStream");
    });
  });

  /* ----------------------------------------------------------- cleanup --- */

  /* Resolves true once the output reports idle, false if the budget runs out. Cleanup carries on
     either way - a stuck output is not a reason to abandon tidying - but the caller has to know,
     because everything after this depends on the output being down and would otherwise fail
     silently while the run still reported green. */
  function waitForIdle(budgetMs) {
    var deadline = Date.now() + budgetMs;

    function poll() {
      return call("dualoutput_getState").then(function (s) {
        if (s.error) return false;
        if (s.stream_state === "idle") return true;
        if (Date.now() >= deadline) return false;
        return new Promise(function (r) { setTimeout(r, 100); }).then(poll);
      });
    }

    return poll();
  }

  function cleanup(ctx) {
    var removed = [];
    var problems = [];
    ctx = ctx || {};

    // A restore that answered with an error left the setting where the tests put it, which is
    // exactly the thing cleanup claims not to do. Recorded rather than thrown, so the rest still runs.
    function restore(what, promise) {
      return promise.then(function (r) {
        if (r && r.error) problems.push(what + ": " + r.error);
        return r;
      });
    }

    // Before anything else: a live output would make the setCanvasSize restore below fail, and a
    // test that threw part way through its live section may have left one running. stopStream
    // returns when stopping is *requested*, so the wait matters - without it the restore races the
    // teardown and its error is swallowed.
    return call("dualoutput_stopStream").then(function () {
      return waitForIdle(8000);
    }).then(function (idle) {
      if (!idle) problems.push("the output never reached idle, so the restores below were made against a live one");
      return call("obs_enum_scenes", V);
    }).then(function (list) {
      var doomed = names(list).filter(function (n) { return n.indexOf(PREFIX) === 0; });

      return doomed.reduce(function (chain, n) {
        return chain.then(function () {
          return call("dualoutput_removeScene", n).then(function (r) {
            if (!r.error) removed.push("vertical:" + n);
          });
        });
      }, Promise.resolve());
    }).then(function () {
      return call("obs_enum_scenes");
    }).then(function (list) {
      // Main-canvas scenes go through obs_source_destroy; scenes are sources.
      var doomed = names(list).filter(function (n) { return n.indexOf(PREFIX) === 0; });

      return doomed.reduce(function (chain, n) {
        return chain.then(function () {
          return call("obs_source_destroy", n).then(function (r) {
            if (!r.error) removed.push("main:" + n);
          });
        });
      }, Promise.resolve());
    }).then(function () {
      return ctx.sourceName ? call("obs_source_destroy", ctx.sourceName) : null;
    }).then(function () {
      return ctx.sizeBefore
        ? restore("canvas size", call("dualoutput_setCanvasSize", ctx.sizeBefore.width, ctx.sizeBefore.height))
        : null;
    }).then(function () {
      return ctx.modeBefore ? restore("output mode", call("dualoutput_setOutputMode", ctx.modeBefore)) : null;
    }).then(function () {
      if (!ctx.settingsBefore) return null;
      var b = ctx.settingsBefore;
      // Exact, including fields that started empty: "" clears, so the originals go back whatever
      // they were. Bitrates and the track are only sent if they were valid, since 0 is now refused
      // and would abort the whole restore.
      return restore("stream settings", call("dualoutput_setStreamSettings",
        b.server || "", b.key || "", !!b.use_auth,
        b.username || "", b.password || "", b.encoder_id || "",
        b.video_bitrate > 0 ? b.video_bitrate : undefined,
        b.audio_bitrate > 0 ? b.audio_bitrate : undefined,
        b.audio_track > 0 ? b.audio_track : undefined,
        !!b.auto_start));
    }).then(function () { return { removed: removed, problems: problems }; });
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
      return cleanup(ctx).then(function (r) {
        var what = r.removed.length ? "removed: " + r.removed.join(", ") : "nothing to remove";

        // A cleanup that could not restore is a failure, not a footnote: the next run starts from
        // whatever this one left behind.
        if (r.problems.length) record("fail", "cleanup", r.problems.join("; ") + " (" + what + ")");
        else record("info", "cleanup", what);
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
