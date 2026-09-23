#!/usr/bin/env node
// MoBaGEn web boot smoke runner (dynamic-loading-all-platforms todo 5).
//
// Boots one web build variant's engine bundle headlessly in node and exits
// 0/1. Boot = the emscripten module initializes far enough to prove the
// variant's flags/worker wiring don't break startup (for `headless` this
// includes a full 5s fixed-step sim run to completion).
//
//   node scripts/smoke_web.mjs --variant isolated   # plain node, no isolation
//   node scripts/smoke_web.mjs --variant shared     # boots inside a
//                                                   # worker_threads Worker
//                                                   # (SAB is legal there)
//
// Variants (todo 5): shared = -sUSE_PTHREADS=1 -sSHARED_MEMORY=1 (SAB-backed
// wasm memory constructed at startup -> cannot boot in a non-isolated
// browser context; node allows SAB in dedicated workers only, hence the
// worker_threads host); isolated = single-threaded flags, boots anywhere.
//
// Todo 8 extension: after the headless boot, the runner ALSO boots the
// MobagenBrowserBackendSmoke bundle of the same variant. That bundle runs the
// real loader pipeline through BrowserWasmBackend (sync WebAssembly.Module +
// Instance over the reference guest bytes) and asserts an exported function's
// return value; its stdout marker `browser-backend-smoke-ok` is required
// evidence. Same --variant CLI, same exit contract.
//
// Node shims (verified against generated bundles): SDL3's emscripten port
// reads `window.location.search` inside an EM_ASM during SDL_Init, so a
// minimal `window.location` must exist although the app never touches the
// DOM. Stdout evidence is captured by wrapping fs.writeSync(1|2, ...) — the
// bundle binds console.log/fs.writeSync at require time, so both are patched
// before the bundle loads.

import { Worker } from "node:worker_threads";
import { fileURLToPath } from "node:url";
import path from "node:path";
import fs from "node:fs";
import process from "node:process";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");

const BOOT_TARGET = "headless";
const MODULE_SMOKE_TARGET = "MobagenBrowserBackendSmoke";
const BOOT_TIMEOUT_MS = 120_000;

// Engine-side markers proving real startup progress (headless app output):
// world construction through the full fixed-step sim run to completion.
const BOOT_EVIDENCE = [
  "Creating Headless World",
  "Headless simulation completed successfully!",
];

// Todo 8: browser-backend module-load markers (MobagenBrowserBackendSmoke
// output). The value-assert line proves a guest export was invoked through
// the backend and returned the expected number, not just a boot.
// Todo 10 extends the same smoke with the descriptor-driven dispatcher:
// mobagen_smoke_mul(6,7) must return 42 through the generic marshaller, and
// a span-taking export must marshal its (offset,size) pair correctly.
//
// Todo 14 adds the memory-shim evidence (variant-aware): the smoke's section
// 3 drives MemoryManager over the WebShim — shared variant = SAB+Atomics
// path inside the worker agent (with the main-thread rule proven by the
// counter line), isolated variant = the single-context fallback (counters
// show zero waits; the degradation trace line is printed).
const MEMORY_SMOKE_EVIDENCE_SHARED = [
  "[browser-smoke] PASS: memory manager initialized over the web shim",
  "[browser-smoke] PASS: web shim: stop-the-world collect ran through the shim",
  "[browser-smoke] PASS: web shim: main-thread Atomics.wait NEVER called (threading rule, counter==0)",
  "[browser-smoke] PASS: web shim: main-thread wait took the bounded-poll fallback path",
  "[browser-smoke] memory-shim-counters variant=shared",
];

const MEMORY_SMOKE_EVIDENCE_ISOLATED = [
  "[browser-smoke] PASS: memory manager initialized over the web shim",
  "[browser-smoke] PASS: web shim: stop-the-world collect ran through the shim",
  "[browser-smoke] PASS: web shim: main-thread Atomics.wait NEVER called (threading rule, counter==0)",
  "[browser-smoke] PASS: web shim: isolated fallback never calls Atomics.wait (degraded no-op-yield path)",
  "[browser-smoke] PASS: web shim: isolated degradation note traced exactly once (loud)",
  "[browser-smoke] memory-shim-counters variant=isolated",
];const MODULE_SMOKE_EVIDENCE = [
  "[browser-smoke] PASS: reference guest loaded through BrowserWasmBackend (sync instantiate + descriptor query)",
  "[browser-smoke] PASS: mobagen_smoke_add(20, 22) returned 42",
  "[browser-smoke] PASS: mobagen_smoke_mul(6, 7) returned 42 through the descriptor path",
  "[browser-smoke] PASS: mobagen_smoke_span_sum({2,2,2,2,2}) returned 10 through the descriptor path",
  "[browser-smoke] PASS: out-of-bounds span cells rejected before the call",
  "[browser-smoke] PASS: invalid wasm bytes mapped to the BackendFailure issue code",
  // Todo 16: QuickJS scripting guest through the same backend — 1+1 == 2
  // through the typed descriptor dispatcher, plus the syntax-error envelope.
  "[browser-smoke] PASS: QuickJS guest loaded through BrowserWasmBackend (sync instantiate + descriptor query)",
  "[browser-smoke] PASS: QuickJS eval(1+1) returned 2",
  "[browser-smoke] PASS: QuickJS syntax error surfaced as ok:false in the envelope",
  // Todo 17: Lua scripting guest — 2*21 == 42 plus the trap + drain recovery
  // (a Lua error traps the guest; mobagen_scripting_error_v1 writes the
  // ok:false envelope and the guest keeps evaluating afterwards).
  "[browser-smoke] PASS: Lua guest loaded through BrowserWasmBackend (sync instantiate + descriptor query)",
  "[browser-smoke] PASS: Lua eval(2*21) returned 42",
  "[browser-smoke] PASS: Lua runtime error surfaced as ok:false in the envelope",
  "browser-backend-smoke-ok",
];

function parseArgs(argv) {
  const args = { variant: null };
  for (let i = 2; i < argv.length; ++i) {
    const a = argv[i];
    if (a === "--variant") args.variant = argv[++i];
    else if (a.startsWith("--variant=")) args.variant = a.slice("--variant=".length);
    else if (a === "--help" || a === "-h") {
      console.log("usage: node scripts/smoke_web.mjs --variant isolated|shared");
      process.exit(0);
    } else {
      console.error(`unknown argument: ${a}`);
      process.exit(2);
    }
  }
  if (args.variant !== "isolated" && args.variant !== "shared") {
    console.error("usage: node scripts/smoke_web.mjs --variant isolated|shared");
    process.exit(2);
  }
  return args;
}

function variantBinDir(variant) {
  // build.py layout (todo 5): shared -> build-web/bin, isolated ->
  // build-web-isolated/bin. Todo 6's probe/variant-picker serves the isolated
  // tree when crossOriginIsolated is false.
  return variant === "shared"
    ? path.join(repoRoot, "build-web", "bin")
    : path.join(repoRoot, "build-web-isolated", "bin");
}

// Discriminates the variants by bundle content: a shared-variant bundle
// eagerly constructs shared wasm memory (`new WebAssembly.Memory({...,shared:true})`)
// at startup — the exact thing that breaks boot in non-isolated contexts.
// (The `em-pthread` string appears in both variants' pthread scaffolding and
// is NOT a discriminator on this emcc.)
function bundleIsSharedVariant(src) {
  return /new WebAssembly\.Memory\(\{[^}]*shared:\s*true/.test(src);
}

// Runs inside a worker_threads Worker for BOTH variants: uniform boot host,
// and the shared variant's SharedArrayBuffer-backed memory is only legal in a
// dedicated worker (node forbids SAB on the main thread).
const BOOT_WORKER_SOURCE = `
const { parentPort, workerData } = require("node:worker_threads");
const { createRequire } = require("node:module");
const nodeFs = require("node:fs");
const nodePath = require("node:path");

// eval'd workers have no meaningful __filename anchor; anchor require at the
// bundle's own directory.
const require2 = createRequire(nodePath.dirname(workerData.jsPath) + "/");
const { jsPath, bootEvidence, timeoutMs, extraModuleConfig } = workerData;

// SDL3 EM_ASM shim (see header comment).
globalThis.window = { location: { search: "", href: "file:///" + jsPath } };

const seen = new Set();
let timer = null;
function proven() {
  return bootEvidence.every((m) => seen.has(m));
}
function note(line) {
  if (bootEvidence.includes(line)) {
    seen.add(line);
    if (proven()) {
      clearTimeout(timer);
      parentPort.postMessage({ ok: true, seen: [...seen] });
    }
  }
}
// The bundle binds console.log (web) / fs.writeSync (node) at require time;
// patch both before loading it.
const origLog = console.log;
console.log = (...a) => { const s = a.join(" "); origLog("[boot]", s); note(s); };
const origWriteSync = nodeFs.writeSync;
nodeFs.writeSync = function patchedWriteSync(fd, data, ...rest) {
  if (fd === 1) note(String(data).trimEnd());
  return origWriteSync.call(nodeFs, fd, data, ...rest);
};

globalThis.Module = { arguments: ["--mobagen-headless"], ...extraModuleConfig };

function fail(error) {
  clearTimeout(timer);
  parentPort.postMessage({ ok: false, error });
}
timer = setTimeout(() => {
  fail("boot evidence timeout; missing: " + bootEvidence.filter((m) => !seen.has(m)).join(", "));
}, timeoutMs);
timer.unref?.();
process.on("uncaughtException", (e) => fail("uncaught: " + ((e && e.stack) || e)));

try {
  require2(jsPath);
} catch (e) {
  fail("bundle load threw: " + ((e && e.stack) || e));
}
`;

// Extension hook for todo 8: extra Module config (module-loading probes plug
// in here without touching the boot flow).
function extraModuleConfig() {
  return {};
}

function boot(jsPath, timeoutMs, bootEvidence = BOOT_EVIDENCE) {
  return new Promise((resolve) => {
    let settled = false;
    const settle = (m) => {
      if (!settled) {
        settled = true;
        resolve(m);
      }
    };
    const child = new Worker(BOOT_WORKER_SOURCE, {
      eval: true,
      workerData: { jsPath, bootEvidence, timeoutMs, extraModuleConfig: extraModuleConfig() },
    });
    child.on("message", (m) => settle(m));
    child.on("error", (e) => settle({ ok: false, error: "worker error: " + e }));
    child.on("exit", (code) => settle({ ok: false, error: "boot worker exited early (code " + code + ")" }));
  });
}

async function main() {
  const args = parseArgs(process.argv);
  const binDir = variantBinDir(args.variant);
  const jsPath = path.join(binDir, `${BOOT_TARGET}.js`);
  const moduleSmokePath = path.join(binDir, `${MODULE_SMOKE_TARGET}.js`);

  console.log(`[smoke_web] variant=${args.variant} bin=${binDir} target=${BOOT_TARGET}`);

  if (!fs.existsSync(jsPath)) {
    console.error(`[smoke_web] FAIL: ${jsPath} not found — run 'python3 scripts/build.py web' first`);
    process.exit(1);
  }

  const src = fs.readFileSync(jsPath, "utf8");
  const isSharedBundle = bundleIsSharedVariant(src);
  if (args.variant === "shared" && !isSharedBundle) {
    console.error("[smoke_web] FAIL: shared variant bundle does not construct shared wasm memory (built with isolated flags?)");
    process.exit(1);
  }
  if (args.variant === "isolated" && isSharedBundle) {
    console.error("[smoke_web] FAIL: isolated variant bundle constructs shared wasm memory (built with shared flags?)");
    process.exit(1);
  }

  const bootResult = await boot(jsPath, BOOT_TIMEOUT_MS);
  if (!bootResult.ok) {
    console.error(`[smoke_web] FAIL: ${args.variant} variant boot failed: ${bootResult.error}`);
    process.exit(1);
  }
  console.log(`[smoke_web] PASS: ${args.variant} variant booted (evidence: ${bootResult.seen.join(" | ")})`);

  // Todo 8: module loading through BrowserWasmBackend, same variant.
  if (!fs.existsSync(moduleSmokePath)) {
    console.error(`[smoke_web] FAIL: ${moduleSmokePath} not found — run 'python3 scripts/build.py web' first`);
    process.exit(1);
  }
  const moduleSmokeResult = await boot(moduleSmokePath, BOOT_TIMEOUT_MS, [
    ...MODULE_SMOKE_EVIDENCE,
    ...(args.variant === "shared" ? MEMORY_SMOKE_EVIDENCE_SHARED : MEMORY_SMOKE_EVIDENCE_ISOLATED),
  ]);
  if (moduleSmokeResult.ok) {
    console.log(`[smoke_web] PASS: ${args.variant} variant browser-backend module load (evidence: ${moduleSmokeResult.seen.join(" | ")})`);
    process.exit(0);
  }
  console.error(`[smoke_web] FAIL: ${args.variant} variant browser-backend module load failed: ${moduleSmokeResult.error}`);
  process.exit(1);
}

main().catch((e) => {
  console.error("[smoke_web] FAIL:", e);
  process.exit(1);
});
