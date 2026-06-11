#!/usr/bin/env python3
"""Static dev server that sends the COOP/COEP headers required for
SharedArrayBuffer — and therefore for Emscripten pthreads (the job system's web
worker threads). Plain `python -m http.server` does NOT send these, so threaded
WASM silently falls back / fails to start its workers.

Usage:  python scripts/serve.py [port] [directory]
    python scripts/serve.py 8090 build/wasm-webgl/bin
Then open the page in a recent Chrome/Edge/Firefox and check
`crossOriginIsolated === true` in the console (required for threads).
"""
import http.server
import os
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Cross-origin isolation -> SharedArrayBuffer enabled -> pthreads work.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        # Always fetch fresh: WASM/.data change every rebuild (cache-bust).
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8090
    directory = sys.argv[2] if len(sys.argv) > 2 else "."
    os.chdir(directory)
    print(f"serving {os.getcwd()} on http://localhost:{port}  (COOP/COEP on; crossOriginIsolated)")
    http.server.test(HandlerClass=Handler, port=port, bind="127.0.0.1")


if __name__ == "__main__":
    main()
