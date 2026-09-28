# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the C++ transport with real descriptor passing and partial writes."""

import array
import concurrent.futures
import contextlib
import os
import pathlib
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

binary = pathlib.Path(os.environ.get("UPSCALE_X11_TRANSPORT_DRIVER", ""))


@unittest.skipUnless(binary.is_file(), "Requires the built C++ transport driver")
class TransportTests(unittest.TestCase):
    """Check forwarding, descriptor ownership and stream shutdown against real sockets."""

    def setUp(self) -> None:
        """Start a relay with small socket buffers to force partial writes."""
        self.client, frontend = socket.socketpair()
        self.backend, server = socket.socketpair()
        for endpoint in (self.client, frontend, self.backend, server):
            endpoint.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024)
            endpoint.settimeout(10)
        self.process = subprocess.Popen(
            [str(binary), str(frontend.fileno()), str(server.fileno())],
            pass_fds=(frontend.fileno(), server.fileno()),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env={**os.environ, "QT_LOGGING_TO_CONSOLE": "1", "QT_FORCE_STDERR_LOGGING": "1"},
        )
        frontend.close()
        server.close()

    def tearDown(self) -> None:
        """Reap the relay and close both test endpoints."""
        self.client.close()
        self.backend.close()
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate()

    def receive(self, endpoint: socket.socket, size: int, expected_file: bytes) -> bytes:
        """Read a stream and verify that its one descriptor retains its contents."""
        content = bytearray()
        descriptors: list[int] = []
        while len(content) < size:
            data, ancillary, flags, _ = endpoint.recvmsg(2048, socket.CMSG_SPACE(256 * 4))
            self.assertTrue(data)
            self.assertEqual(flags & socket.MSG_CTRUNC, 0)
            content.extend(data)
            for level, kind, value in ancillary:
                self.assertEqual((level, kind), (socket.SOL_SOCKET, socket.SCM_RIGHTS))
                numbers = array.array("i")
                numbers.frombytes(value[: len(value) - len(value) % numbers.itemsize])
                descriptors.extend(numbers)
        self.assertEqual(len(descriptors), 1)
        for descriptor in descriptors:
            try:
                self.assertEqual(os.pread(descriptor, 128, 0), expected_file)
            finally:
                os.close(descriptor)
        return bytes(content)

    @staticmethod
    def send(endpoint: socket.socket, data: bytes, descriptor: int) -> None:
        """Pass a descriptor once and finish any partially accepted stream."""
        count = endpoint.sendmsg(
            [data], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [descriptor]))]
        )
        endpoint.sendall(data[count:])

    def test_simultaneous_partial_writes_and_rights(self) -> None:
        """Exercise backpressure, both directions, rights and a half-closed stream."""
        first = bytes(range(256)) * 4096
        second = bytes(reversed(range(256))) * 4096
        with (
            tempfile.TemporaryFile(dir=binary.parent) as left,
            tempfile.TemporaryFile(dir=binary.parent) as right,
        ):
            left.write(b"client descriptor")
            right.write(b"backend descriptor")
            left.flush()
            right.flush()
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                writers = [
                    pool.submit(self.send, self.client, first, left.fileno()),
                    pool.submit(self.send, self.backend, second, right.fileno()),
                ]
                readers = [
                    pool.submit(self.receive, self.backend, len(first), b"client descriptor"),
                    pool.submit(self.receive, self.client, len(second), b"backend descriptor"),
                ]
                self.assertEqual(readers[0].result(timeout=15), first)
                self.assertEqual(readers[1].result(timeout=15), second)
                writers[0].result(timeout=15)
                writers[1].result(timeout=15)
        self.client.shutdown(socket.SHUT_WR)
        self.assertEqual(self.backend.recv(1), b"")
        self.backend.sendall(b"after client half-close")
        self.assertEqual(self.client.recv(128), b"after client half-close")
        self.backend.shutdown(socket.SHUT_WR)
        self.assertEqual(self.client.recv(1), b"")
        log = self.process.communicate(timeout=3)[0].decode()
        self.assertEqual(self.process.returncode, 0, log)
        self.assertIn("received-fds= 2 sent-fds= 2", log)

    def test_disconnected_destination_does_not_sigpipe(self) -> None:
        """A disappearing destination must not kill the relay with SIGPIPE."""
        self.backend.close()
        with contextlib.suppress(BrokenPipeError):
            self.client.sendall(b"unread traffic" * 1000)
        self.client.close()
        log = self.process.communicate(timeout=3)[0].decode()
        self.assertEqual(self.process.returncode, 0, log)


@unittest.skipUnless(binary.is_file(), "Requires the built C++ transport driver")
class PolicyTransportTests(unittest.TestCase):
    """Check the relay with its protocol policy in front, as a selected connection has it."""

    def setUp(self) -> None:
        """Start a relay that reads the protocol it forwards."""
        self.client, frontend = socket.socketpair()
        self.backend, server = socket.socketpair()
        for endpoint in (self.client, self.backend):
            endpoint.settimeout(30)
        self.process = subprocess.Popen(
            [str(binary), str(frontend.fileno()), str(server.fileno()), "2560x1440"],
            pass_fds=(frontend.fileno(), server.fileno()),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env={**os.environ, "QT_LOGGING_TO_CONSOLE": "1", "QT_FORCE_STDERR_LOGGING": "1"},
        )
        frontend.close()
        server.close()

    def tearDown(self) -> None:
        """Reap the relay and close both test endpoints."""
        self.client.close()
        self.backend.close()
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate()

    @staticmethod
    def read(endpoint: socket.socket, size: int) -> bytes:
        """Read exactly this much, failing if the connection ends first."""
        data = bytearray()
        while len(data) < size:
            chunk = endpoint.recv(min(size - len(data), 1 << 20))
            if not chunk:
                message = f"connection ended after {len(data)} of {size} bytes"
                raise AssertionError(message)
            data.extend(chunk)
        return bytes(data)

    def exchange(self, request: bytes, reply: bytes) -> None:
        """Send a request through, then its reply back."""
        self.client.sendall(request)
        self.assertEqual(self.read(self.backend, len(request)), request)
        self.backend.sendall(reply)
        self.read(self.client, len(reply))

    def connect(self) -> None:
        """Set up the connection the way a client and the server do."""
        setup = struct.pack("<BxHHHHxx", ord("l"), 11, 0, 0, 0)
        reply = bytearray(80)
        reply[0] = 1
        struct.pack_into("<H", reply, 2, 11)
        struct.pack_into("<H", reply, 6, 18)
        reply[28] = 1
        struct.pack_into("<I", reply, 40, 42)
        struct.pack_into("<HH", reply, 60, 3840, 2160)
        self.exchange(setup, bytes(reply))

    def test_malformed_requests_are_forwarded(self) -> None:
        """Requests of the wrong length reach the server as sent, and the link stays.

        They are the server's to answer with BadLength. The relay used to
        read past their end and end the connection instead, which the X Test
        Suite's protocol cases met as a server that had died.
        """
        self.connect()
        short_geometry = struct.pack("<BxH", 14, 1)
        long_name = struct.pack("<BxHHxx", 98, 2, 200)
        valid = struct.pack("<BxHI", 14, 2, 42)
        sent = short_geometry + long_name + valid
        self.client.sendall(sent)
        self.assertEqual(self.read(self.backend, len(sent)), sent)
        self.assertIsNone(self.process.poll(), "the relay ended the connection")

    def test_large_requests_to_a_slow_server(self) -> None:
        """Whole frames queued toward a server not yet reading all arrive, and the link stays.

        The relay hands on a frame it reads only once all of it arrived. A
        6 MiB request completing while 3 MiB still waited for the server
        used to exceed the output queue and end the connection.
        """
        self.connect()
        query = struct.pack("<BxHHxx", 98, 5, 12) + b"BIG-REQUESTS"
        present = struct.pack("<BxHIBBBB", 1, 1, 0, 1, 133, 0, 0).ljust(32, b"\0")
        self.exchange(query, present)
        enable = struct.pack("<BBH", 133, 0, 1)
        enabled = struct.pack("<BxHII", 1, 2, 0, 4194303).ljust(32, b"\0")
        self.exchange(enable, enabled)

        def request(size: int, fill: int) -> bytes:
            return struct.pack("<BBHI", 127, 0, 0, size // 4) + bytes([fill]) * (size - 8)

        first = request(3 << 20, 0x5A)
        second = request(6 << 20, 0xA5)
        sender = threading.Thread(target=self.client.sendall, args=(first + second,))
        sender.start()
        # Bounded, not awaited: the relay is meant to meet a full buffer while
        # nothing reads the backend, and no portable interface says when a
        # socket's buffer has filled. Correctness does not depend on it - the
        # reads below check every byte - only whether that path was taken.
        time.sleep(2)
        self.assertEqual(self.read(self.backend, len(first)), first)
        self.assertEqual(self.read(self.backend, len(second)), second)
        sender.join(timeout=30)
        self.client.sendall(struct.pack("<BxH", 127, 1))
        self.assertEqual(self.read(self.backend, 4), struct.pack("<BxH", 127, 1))
        self.assertIsNone(self.process.poll(), "the relay ended the connection")


if __name__ == "__main__":
    unittest.main()
