import asyncio
from pathlib import Path
import socket
import sys
import tempfile
import unittest
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import gtos_content_https_proxy as proxy


class ConnectRequestTest(unittest.TestCase):
    def test_only_https_connect_to_listed_domains(self):
        domains = {'example.com'}
        for hostname in ('example.com', 'MEDIA.EXAMPLE.COM'):
            request = f'CONNECT {hostname}:443 HTTP/1.1\r\n\r\n'.encode()
            self.assertEqual(proxy.allowed_host(request, domains), hostname.lower())
        for invalid in [
                b'CONNECT example.com.invalid.test:443 HTTP/1.1',
                b'CONNECT unrelated-example.com:443 HTTP/1.1',
                b'CONNECT example.com:80 HTTP/1.1',
                b'CONNECT 127.0.0.1:443 HTTP/1.1',
                b'GET https://example.com/ HTTP/1.1']:
            with self.subTest(request=invalid):
                self.assertIsNone(proxy.allowed_host(invalid, domains))

    def test_host_mappings_are_explicit_and_do_not_allow_private_peers(self):
        for address in ('198.18.3.165', '2001:2::3a2'):
            self.assertFalse(proxy.public_ip(address))
            self.assertTrue(proxy.public_ip(address, host_mapped=True))
        for address in ('10.0.0.1', '127.0.0.1', '169.254.169.254',
                        '::ffff:127.0.0.1', '::1', 'ff02::1'):
            self.assertFalse(proxy.public_ip(address, host_mapped=True))


class ConnectProxyTest(unittest.IsolatedAsyncioTestCase):
    async def test_private_dns_answer_never_connects(self):
        loop = asyncio.get_running_loop()
        addresses = [(socket.AF_INET, socket.SOCK_STREAM, 6, '', ('127.0.0.1', 443)),
                     (socket.AF_INET, socket.SOCK_STREAM, 6, '', ('93.184.216.34', 443))]
        with mock.patch.object(loop, 'getaddrinfo', return_value=addresses), \
                mock.patch('asyncio.open_connection') as connect:
            with self.assertRaises(OSError):
                await proxy.connect_public('example.com')
            connect.assert_not_called()

    async def test_proxy_relays_both_directions_and_refuses_other_destinations(self):
        relay = proxy.ConnectProxy({'example.com'})

        async def respond(reader, writer):
            try:
                request = await reader.readexactly(4)
                writer.write(request[::-1])
                await writer.drain()
            finally:
                writer.close()

        with tempfile.TemporaryDirectory(prefix='cx') as directory:
            origin_path = str(Path(directory) / 'o')
            relay_path = str(Path(directory) / 'r')
            origin = await asyncio.start_unix_server(respond, origin_path)
            listener = await asyncio.start_unix_server(relay.handle, relay_path)

            async def connect_local(name, host_mapped):
                return await asyncio.open_unix_connection(origin_path)

            async with origin, listener:
                with mock.patch.object(proxy, 'connect_public',
                                       side_effect=connect_local) as connect:
                    reply = await self.exchange(relay_path, 'outside.example', b'')
                    self.assertEqual(reply, b'HTTP/1.1 403 Forbidden\r\n\r\n')
                    connect.assert_not_called()
                    reply = await self.exchange(relay_path, 'example.com', b'\x01abc')
                    self.assertEqual(reply, b'HTTP/1.1 200 Connection Established\r\n\r\ncba\x01')
                    connect.assert_awaited_once_with('example.com', False)

    async def exchange(self, path, name, payload):
        reader, writer = await asyncio.open_unix_connection(path)
        try:
            writer.write(f'CONNECT {name}:443 HTTP/1.1\r\n\r\n'.encode() + payload)
            await writer.drain()
            writer.write_eof()
            return await asyncio.wait_for(reader.read(), 4)
        finally:
            writer.close()
            await writer.wait_closed()

    async def test_continuous_download_survives_an_idle_upload(self):
        relay = proxy.ConnectProxy({'example.com'})

        async def stream_video(reader, writer):
            try:
                await reader.readexactly(3)
                for _ in range(12):
                    writer.write(b'frame')
                    await writer.drain()
                    await asyncio.sleep(.08)
            finally:
                writer.close()

        with tempfile.TemporaryDirectory(prefix='cx') as directory:
            origin_path = str(Path(directory) / 'o')
            relay_path = str(Path(directory) / 'r')
            origin = await asyncio.start_unix_server(stream_video, origin_path)
            listener = await asyncio.start_unix_server(relay.handle, relay_path)

            async def connect_local(name, host_mapped):
                return await asyncio.open_unix_connection(origin_path)

            async with origin, listener:
                with mock.patch.object(proxy, 'connect_public', side_effect=connect_local), \
                        mock.patch.object(proxy, 'IDLE_TIMEOUT', .4):
                    reader, writer = await asyncio.open_unix_connection(relay_path)
                    try:
                        writer.write(b'CONNECT example.com:443 HTTP/1.1\r\n\r\nGET')
                        await writer.drain()
                        self.assertEqual(await reader.readuntil(b'\r\n\r\n'),
                                         b'HTTP/1.1 200 Connection Established\r\n\r\n')
                        data = await asyncio.wait_for(reader.readexactly(60), 4)
                        self.assertEqual(data, b'frame' * 12)
                    finally:
                        writer.close()
                        await writer.wait_closed()


if __name__ == '__main__':
    unittest.main()
