#!/usr/bin/env python3
"""Forward HTTPS from the GTOS PCnet test environment without handling TLS."""

import argparse
import asyncio
import errno
import ipaddress
import os
import re
import socket
from pathlib import Path


DOMAIN = r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)+'
REQUEST = re.compile(rb'CONNECT (' + DOMAIN.encode('ascii') + rb'):443 HTTP/1\.[01]', re.I)
MAX_HEADER = 8192
MAX_CLIENTS = 16
IDLE_TIMEOUT = 300
HOST_DNS_MAPPINGS = (ipaddress.ip_network('198.18.0.0/15'),
                     ipaddress.ip_network('2001:2::/48'))


def allowed_host(header, allowed_domains):
    first_line = header.split(b'\r\n', 1)[0]
    match = REQUEST.fullmatch(first_line)
    if not match:
        return None
    name = match[1].decode('ascii').lower()
    if len(name) <= 253 and any(
            name == domain or name.endswith('.' + domain) for domain in allowed_domains):
        return name
    return None


def public_ip(address, host_mapped=False):
    ip = ipaddress.ip_address(address)
    if ip.version == 6 and ip.ipv4_mapped is not None:
        ip = ip.ipv4_mapped
    # Some host networking environments return benchmark-range DNS mappings for
    # public domains. The relay still connects only to the selected domain via
    # that host, and the browser must validate the server's TLS certificate.
    if host_mapped and any(ip in network for network in HOST_DNS_MAPPINGS):
        return True
    return ip.is_global and not ip.is_multicast and not ip.is_reserved


async def connect_public(name, host_mapped=False):
    loop = asyncio.get_running_loop()
    entries = await asyncio.wait_for(
        loop.getaddrinfo(name, 443, family=socket.AF_UNSPEC, type=socket.SOCK_STREAM), 10)
    addresses = {entry[4][0] for entry in entries}
    if not addresses or any(not public_ip(address, host_mapped) for address in addresses):
        raise OSError('Destination resolved outside the public network')
    # Use the validated numeric addresses so the connect cannot resolve again.
    for address in sorted(addresses, key=lambda item: (':' in item, item))[:4]:
        try:
            return await asyncio.wait_for(asyncio.open_connection(address, 443), 5)
        except (OSError, asyncio.TimeoutError):
            pass
    raise OSError('No reachable public address')


async def send_reply(writer, status):
    writer.write(f'HTTP/1.1 {status}\r\n\r\n'.encode('ascii'))
    await asyncio.wait_for(writer.drain(), 5)


async def tunnel(source, destination, activity, totals, direction):
    while True:
        block = await source.read(65536)
        if not block:
            if destination.can_write_eof() and not destination.is_closing():
                try:
                    destination.write_eof()
                except OSError as error:
                    # A disconnected peer has already closed its write side.
                    # Do not cancel the other direction at this EOF.
                    if error.errno != errno.ENOTCONN:
                        raise
            return
        totals[direction] += len(block)
        activity.set()
        destination.write(block)
        await asyncio.wait_for(destination.drain(), IDLE_TIMEOUT)


async def wait_for_idle(activity):
    while True:
        activity.clear()
        await asyncio.wait_for(activity.wait(), IDLE_TIMEOUT)


class ConnectProxy:
    def __init__(self, allowed_domains, host_mapped=False):
        self.allowed_domains = allowed_domains
        self.host_mapped = host_mapped
        self.clients = set()

    async def handle(self, reader, writer):
        if len(self.clients) >= MAX_CLIENTS:
            writer.close()
            return
        task = asyncio.current_task()
        self.clients.add(task)
        name = None
        remote_writer = None
        totals = [0, 0]
        try:
            header = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 10)
            name = allowed_host(header, self.allowed_domains)
            if name is None or len(header) > MAX_HEADER:
                await send_reply(writer, '403 Forbidden')
                print('CONNECT_REFUSED', flush=True)
                return
            try:
                remote_reader, remote_writer = await connect_public(name, self.host_mapped)
            except (OSError, asyncio.TimeoutError):
                await send_reply(writer, '502 Bad Gateway')
                print('CONNECT_UNREACHABLE', name, flush=True)
                return

            await send_reply(writer, '200 Connection Established')
            print('CONNECT_OPEN', name, flush=True)
            activity = asyncio.Event()
            outgoing = asyncio.create_task(tunnel(reader, remote_writer, activity, totals, 0))
            incoming = asyncio.create_task(tunnel(remote_reader, writer, activity, totals, 1))
            transfer = asyncio.gather(outgoing, incoming)
            idle = asyncio.create_task(wait_for_idle(activity))
            try:
                done, _ = await asyncio.wait((transfer, idle),
                                             return_when=asyncio.FIRST_COMPLETED)
                if transfer not in done:
                    idle.result()
                transfer.result()
                print('CONNECT_CLOSED', name, *totals, flush=True)
            finally:
                outgoing.cancel()
                incoming.cancel()
                idle.cancel()
                await asyncio.gather(transfer, outgoing, incoming, idle, return_exceptions=True)
        except (OSError, asyncio.TimeoutError, asyncio.IncompleteReadError,
                asyncio.LimitOverrunError) as error:
            if name:
                print('CONNECT_INTERRUPTED', name, type(error).__name__,
                      getattr(error, 'errno', None), *totals, flush=True)
        finally:
            self.clients.remove(task)
            if remote_writer:
                remote_writer.close()
            writer.close()


async def serve(path, seconds, allowed_domains, host_mapped=False):
    proxy = ConnectProxy(allowed_domains, host_mapped)
    old_mask = os.umask(0o177)
    try:
        server = await asyncio.start_unix_server(
            proxy.handle, path=str(path), limit=MAX_HEADER)
    finally:
        os.umask(old_mask)
    try:
        print('CONNECT_PROXY_READY', path, flush=True)
        await asyncio.sleep(seconds)
    finally:
        server.close()
        await server.wait_closed()
        tasks = tuple(proxy.clients)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        path.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=1800, metavar='1..5400')
    parser.add_argument('--allow', action='append', required=True, metavar='DOMAIN')
    parser.add_argument('--host-mapped-dns', action='store_true',
                        help='permit benchmark-range DNS mappings provided by the host network')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 5400:
        parser.error('expected a relay lifetime from 1 to 5400 seconds')
    domains = set(domain.lower() for domain in args.allow)
    if any(not re.fullmatch(DOMAIN, domain) for domain in domains):
        parser.error('expected DNS names without schemes, ports or wildcards')
    if (not args.socket.is_absolute() or '..' in args.socket.parts or
            not args.socket.parent.is_dir() or os.path.lexists(args.socket) or
            len(os.fsencode(args.socket)) >= 108):
        parser.error('expected a new absolute Unix socket in an existing directory')
    asyncio.run(serve(args.socket, args.seconds, domains, args.host_mapped_dns))


if __name__ == '__main__':
    main()
