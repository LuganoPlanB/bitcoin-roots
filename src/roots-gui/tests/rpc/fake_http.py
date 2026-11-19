# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Scripted loopback HTTP fixture and reference client; no production RPC claim."""

import base64
from dataclasses import dataclass, field
from decimal import Decimal
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import secrets
import socket
import threading
from urllib.parse import quote


class TransportError(Exception):
    def __init__(self, kind, status=None):
        self.kind = kind
        self.status = status
        super().__init__(kind)


class RpcError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__("RPC error code " + str(code))


def strict_json(raw):
    def pairs(items):
        result = {}
        for k, v in items:
            if k in result:
                raise ValueError("duplicate JSON member")
            result[k] = v
        return result

    return json.loads(
        raw,
        parse_float=Decimal,
        parse_constant=lambda _: (_ for _ in ()).throw(
            ValueError("invalid JSON number")
        ),
        object_pairs_hook=pairs,
    )


def request(port, auth, method, params=None, *, wallet=None, request_id=1, timeout=2):
    # Fixed loopback host; never inherit proxy environment or put credentials in URLs.
    path = "/wallet/" + quote(wallet, safe="") if wallet is not None else "/"
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    try:
        conn.request(
            "POST",
            path,
            json.dumps(
                dict(
                    jsonrpc="2.0",
                    id=request_id,
                    method=method,
                    params=[] if params is None else params,
                )
            ),
            {"Authorization": auth, "Content-Type": "application/json"},
        )
        response = conn.getresponse()
        status = response.status
        raw = response.read(2 * 1024 * 1024 + 1)
        if status != 200:
            raise TransportError("authentication" if status == 401 else "http", status)
        if len(raw) > 2 * 1024 * 1024:
            raise TransportError("oversized-response")
        try:
            result = strict_json(raw)
        except (ValueError, UnicodeDecodeError):
            raise TransportError("json") from None
        if (
            not isinstance(result, dict)
            or result.get("jsonrpc") != "2.0"
            or type(result.get("id")) is not type(request_id)
            or result.get("id") != request_id
        ):
            raise TransportError("response-identity")
        if "error" in result:
            error = result["error"]
            if not isinstance(error, dict) or type(error.get("code")) is not int:
                raise TransportError("jsonrpc-shape")
            raise RpcError(error["code"])
        if "result" not in result:
            raise TransportError("jsonrpc-shape")
        return result["result"]
    except (TimeoutError, socket.timeout):
        raise TransportError("timeout") from None
    except (OSError, http.client.HTTPException):
        raise TransportError("connection") from None
    finally:
        conn.close()


@dataclass
class Step:
    method: str
    result: object = None
    status: int = 200
    raw: bytes | None = None
    error: int | None = None
    wrong_id: bool = False
    disconnect: bool = False
    gate: threading.Event | None = None
    entered: threading.Event = field(default_factory=threading.Event)
    headers: tuple = ()
    declared_length: int | None = None
    request_check: object = None
    gate_timeout: float = 2


class FakeHttp:
    def __init__(self, steps):
        self.steps = list(steps)
        self.records = []
        self.active = 0
        self.completed = []
        self.lock = threading.Lock()
        self.accepted = threading.Event()
        self.read_timeouts = 0
        self.check_failures = []
        self.secret = secrets.token_urlsafe(24)
        self.auth = (
            "Basic " + base64.b64encode(("fixture:" + self.secret).encode()).decode()
        )
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def setup(self):
                super().setup()
                self.connection.settimeout(2)
                owner.accepted.set()

            def log_message(self, *args):
                if any(isinstance(x, TimeoutError) for x in args):
                    with owner.lock:
                        owner.read_timeouts += 1

            def do_POST(self):
                with owner.lock:
                    owner.active += 1
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if not 0 < length < 65536:
                        raise ValueError("invalid fixture body size")
                    body = strict_json(self.rfile.read(length))
                    method = body.get("method")
                    rid = body.get("id")
                    if not secrets.compare_digest(
                        self.headers.get("Authorization", ""), owner.auth
                    ):
                        status = 401
                        payload = b"Authentication required"
                        step = None
                    else:
                        with owner.lock:
                            step = next(
                                (
                                    x
                                    for x in owner.steps
                                    if x.method == method and not x.entered.is_set()
                                ),
                                None,
                            )
                            if step:
                                step.entered.set()
                        if not step:
                            status = 500
                            payload = b"Unscripted fixture request"
                        else:
                            if step.request_check:
                                try:
                                    step.request_check(self.path, body, self.headers)
                                except AssertionError:
                                    owner.check_failures.append(
                                        "request contract mismatch"
                                    )
                            if step.gate and not step.gate.wait(step.gate_timeout):
                                raise TimeoutError("fixture gate deadline")
                            if step.disconnect:
                                self.connection.shutdown(socket.SHUT_RDWR)
                                return
                            status = step.status
                            data = dict(
                                jsonrpc="2.0", id="mismatch" if step.wrong_id else rid
                            )
                            if step.error is not None:
                                data["error"] = dict(
                                    code=step.error, message="redacted fixture error"
                                )
                            else:
                                data["result"] = step.result
                            payload = (
                                step.raw
                                if step.raw is not None
                                else json.dumps(data).encode()
                            )
                    with owner.lock:
                        # No headers, raw request/response body, passwords, cookies or params.
                        owner.records.append(
                            dict(
                                method=method,
                                request_id=rid,
                                wallet=self.path.startswith("/wallet/"),
                                status=status,
                            )
                        )
                        owner.completed.append(method)
                    self.send_response(status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header(
                        "Content-Length",
                        str(
                            step.declared_length
                            if step and step.declared_length is not None
                            else len(payload)
                        ),
                    )
                    if step:
                        for key, value in step.headers:
                            self.send_header(key, value)
                    self.end_headers()
                    self.wfile.write(payload)
                except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError):
                    pass
                finally:
                    with owner.lock:
                        owner.active -= 1

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = False
        self.server.block_on_close = True
        self.port = self.server.server_port
        self.thread = threading.Thread(
            target=self.server.serve_forever,
            kwargs={"poll_interval": 0.01},
            name="owned-fake-http",
        )

    def __enter__(self):
        self.thread.start()
        return self

    def call(self, method, params=None, **kwargs):
        return request(self.port, self.auth, method, params, **kwargs)

    def close(self):
        for step in self.steps:
            if step.gate:
                step.gate.set()
        self.server.shutdown()
        self.thread.join(timeout=3)
        self.server.server_close()
        if self.thread.is_alive() or self.active:
            raise RuntimeError("owned HTTP fixture failed settlement")

    def __exit__(self, *_):
        self.close()
