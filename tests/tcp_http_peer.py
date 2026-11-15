"""Local byte-exact HTTP endpoint for the restricted slirp test."""

request = b'GET /network-proof HTTP/1.0\r\nHost: gtos.test\r\nX-Pad: ' + b'x' * 687 + b'\r\n\r\n'
response = (b'HTTP/1.0 200 OK\r\nContent-Length: 6917\r\n\r\n'
            + bytes((i * 17) & 255 for i in range(6917)))
