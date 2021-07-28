#!/usr/bin/env python
# Serves up the Emscripten files
import BaseHTTPServer
import SimpleHTTPServer

# Feel free to change the port if it is already being used
port = 8000
print("Running on port %d" % port)

SimpleHTTPServer.SimpleHTTPRequestHandler.extensions_map['.wasm'] = 'application/wasm'
httpd = BaseHTTPServer.HTTPServer(('localhost', port), SimpleHTTPServer.SimpleHTTPRequestHandler)
print("Mapping \".wasm\" to \"%s\"" % SimpleHTTPServer.SimpleHTTPRequestHandler.extensions_map['.wasm'])
httpd.serve_forever()
