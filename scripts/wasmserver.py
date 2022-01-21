#!/usr/bin/env python
# Serves up the Emscripten files
import sys
if sys.version_info[0] >= 3:
    from http.server import SimpleHTTPRequestHandler, HTTPServer
else:
    from SimpleHTTPServer import SimpleHTTPRequestHandler
    from BaseHTTPServer import HTTPServer

# Feel free to change the port if it is already being used
port = 8000
print("Running on port %d" % port)
server_address = ('localhost', port)

SimpleHTTPRequestHandler.extensions_map['.wasm'] = 'application/wasm'
print("Mapping \".wasm\" to \"%s\"" % SimpleHTTPRequestHandler.extensions_map['.wasm'])
httpd = HTTPServer(server_address, SimpleHTTPRequestHandler)

httpd.serve_forever()
