from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import subprocess
import threading
import time
import pytest


def test_native_http_cancellation_timeout_and_response_integrity():
    root = Path(__file__).resolve().parents[1]
    executable = Path(os.environ.get('S3D_HTTP_TEST', str(root/'build/native/premiere/Release/s3d_http_tests.exe')))
    if not executable.is_file():
        pytest.skip('Build the Windows native HTTP tests first')
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_POST(self):
            body = self.rfile.read(int(self.headers['Content-Length']))
            if self.path == '/slow-headers': time.sleep(2)
            try:
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)+(20 if self.path == '/truncated' else 0)))
                self.end_headers()
                if self.path == '/slow-body':
                    self.wfile.write(body[:1]);self.wfile.flush();time.sleep(2)
                    self.wfile.write(body[1:])
                else:
                    self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True);thread.start()
    try:
        result = subprocess.run([str(executable), str(server.server_port)], capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, result.stdout + result.stderr
    finally:
        server.shutdown();server.server_close();thread.join(timeout=5)
