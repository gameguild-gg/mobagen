/*
 * MoBaGEn web variant picker (dynamic-loading-all-platforms todo 6).
 *
 * Cross-origin-isolation capability probe + engine bundle chooser. Runs BEFORE
 * the engine script tag loads; the engine <script src> is rewritten in place.
 *
 * DEPLOYMENT LAYOUT (assumed by the picker; pure static hosting, no server
 * config needed):
 *
 *     /            -> the ISOLATED tree (build-web-isolated/bin)  [always bootable]
 *     /isolated/   -> the ISOLATED tree again (self-reference, so the isolated
 *                     tree serves its own bundles when deployed standalone)
 *     /shared/     -> the SHARED tree (build-web/bin; SAB/pthreads, needs
 *                     crossOriginIsolated)
 *
 * Pages exist in BOTH trees with identical names. When the page itself is in
 * the shared tree (served from /) the picker keeps relative srcs untouched;
 * when the page is in the isolated tree it rewrites relative engine srcs to
 * /shared/... ONLY if crossOriginIsolated is true. Non-isolated contexts
 * (Safari, first load pre-reload on GitHub Pages, headerless static servers)
 * always boot the isolated bundle from the tree the page lives in.
 *
 * HONEST SAB STORY: coi-serviceworker (loaded synchronously before this file
 * in every page) gives Chromium/Firefox isolation after ONE automatic reload
 * on header-less static hosts (GitHub Pages). It NEVER works on Safari, and
 * the very first load is always non-isolated. The probe is the guaranteed-
 * correct path on every browser; the SW is best-effort uplift only.
 */
(function (global) {
  "use strict";

  // Pure: capability -> variant name. "shared" only when the context is
  // actually cross-origin isolated; "isolated" is the always-safe fallback.
  function mobagenPickVariant(crossOriginIsolated) {
    return crossOriginIsolated === true ? "shared" : "isolated";
  }

  // Pure: resolve the engine script src for the picked variant.
  //   pageTree: "shared" | "isolated" — the tree this page is deployed in.
  //   src: the relative engine src as authored in the page (<name>.js).
  // In the shared tree everything stays relative (the page IS beside its
  // bundles). In the isolated tree the shared sibling is at /shared/.
  function mobagenEngineSrc(picked, pageTree, src) {
    if (picked === "shared" && pageTree === "isolated") {
      return "/shared/" + src;
    }
    return src;
  }

  global.mobagenPickVariant = mobagenPickVariant;
  global.mobagenEngineSrc = mobagenEngineSrc;

  // Which tree are we in? /shared/... pages are the shared tree; everything
  // else is the isolated tree. The dev server (build.py --run) serves the
  // shared tree at :8000 with COOP/COEP headers, so the picker keeps relative
  // paths there — but a headerless re-serve of the same tree still resolves
  // the relative (shared) bundle: the picker never blocks the boot.
  var pageTree =
    global.location &&
    global.location.pathname &&
    global.location.pathname.indexOf("/shared/") === 0
      ? "shared"
      : "isolated";

  var picked = mobagenPickVariant(global.crossOriginIsolated);
  global.MOBAGEN_WEB_VARIANT = picked;
  if (picked !== "shared" && global.console && global.console.log) {
    global.console.log(
      "MoBaGEn: crossOriginIsolated=false -> isolated web variant" +
        " (SharedArrayBuffer unavailable; Safari/static-host first load never" +
        " isolates — coi-serviceworker uplift is Chromium/Firefox only)."
    );
  }

  // Rewrite engine bundle srcs in the document. Runs during <head> parsing,
  // before the engine <script> tags in <body> are fetched.
  if (global.document) {
    var scripts = global.document.querySelectorAll(
      "script[data-mobagen-engine]"
    );
    for (var i = 0; i < scripts.length; i++) {
      var s = scripts[i];
      var src = s.getAttribute("src");
      if (!src || src.indexOf("/") === 0) continue; // absolute: authored intent
      s.setAttribute("src", mobagenEngineSrc(picked, pageTree, src));
    }
  }
})(typeof window !== "undefined" ? window : globalThis);
