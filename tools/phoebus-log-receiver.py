#!/usr/bin/env python3
"""Unprivileged, bounded off-device receiver. Configure addresses at deployment."""
import argparse
import base64
import datetime
import fcntl
import json
import logging
import logging.handlers
import os
import re
from pathlib import Path
import selectors
import socket
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--listen', required=True)
    p.add_argument('--router', required=True)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--link-interface', help='Optionally record collector Ethernet carrier transitions')
    p.add_argument('--syslog-port', type=int, default=1514)
    p.add_argument('--kernel-port', type=int, default=6666)
    p.add_argument('--netconsole-port', type=int, default=6667)
    p.add_argument('--max-mib', type=int, default=32)
    p.add_argument('--backups', type=int, default=16)
    a = p.parse_args()
    if a.link_interface and ('/' in a.link_interface or a.link_interface in ('.', '..')):
        p.error('invalid link interface')
    try:
        socket.inet_pton(socket.AF_INET, a.listen)
        socket.inet_pton(socket.AF_INET, a.router)
    except OSError:
        p.error('listen/router must be numeric IPv4 addresses')
    if any(not 1024 <= port <= 65535 for port in (a.syslog_port, a.kernel_port, a.netconsole_port)):
        p.error('use unprivileged UDP ports in 1024..65535')
    if not 1 <= a.max_mib <= 512 or not 1 <= a.backups <= 32:
        p.error('invalid retention bounds')
    if len({a.syslog_port, a.kernel_port, a.netconsole_port}) != 3:
        p.error('ports must be distinct')
    os.umask(0o077)
    a.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    lock = (a.directory / 'receiver.lock').open('a+')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return 0  # Cron may invoke this while the existing daemon is healthy.
    lock.seek(0); lock.truncate(); lock.write(str(os.getpid()) + '\n'); lock.flush()
    handlers = {}
    for name in ('syslog', 'telemetry', 'kernel', 'netconsole', 'receiver'):
        h = logging.handlers.RotatingFileHandler(a.directory / (name + '.jsonl'),
                maxBytes=a.max_mib * 1024 * 1024, backupCount=a.backups,
                encoding='utf-8', errors='backslashreplace')
        h.setFormatter(logging.Formatter('%(message)s'))
        handlers[name] = h

    def record(channel, **fields):
        fields['received_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        fields['received_monotonic_ns'] = time.monotonic_ns()
        entry = logging.LogRecord(channel, logging.INFO, '', 0,
                                  json.dumps(fields, ensure_ascii=True), (), None)
        handlers[channel].handle(entry)

    selector = selectors.DefaultSelector()
    for port, channel in ((a.syslog_port, 'syslog'), (a.kernel_port, 'kernel'),
                          (a.netconsole_port, 'netconsole')):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        s.bind((a.listen, port))
        s.setblocking(False)
        selector.register(s, selectors.EVENT_READ, channel)
    record('receiver', event='started', ports=[a.syslog_port, a.kernel_port, a.netconsole_port])
    last_packet = None
    link_state = None
    seen = {'syslog': 0, 'kernel': 0, 'netconsole': 0}
    rejected = 0
    last_fsync = time.monotonic()
    last_status = last_fsync
    while True:
        for key, _ in selector.select(timeout=1):
            # Bound each batch so a noisy channel cannot starve the others.
            for _ in range(256):
                try:
                    data, peer = key.fileobj.recvfrom(65535)
                except BlockingIOError:
                    break
                seen[key.data] += 1
                if peer[0] not in (a.router, a.listen):
                    rejected += 1
                    continue
                extra = {}
                try:
                    text = data.decode('utf-8')
                except UnicodeDecodeError:
                    text = data.decode('utf-8', 'replace')
                    extra['raw_base64'] = base64.b64encode(data).decode('ascii')
                channel = key.data
                if channel == 'syslog' and re.search(r'(?:^|\s)phoebus-[a-z-]+(?:\[\d+\])?:\s', text):
                    channel = 'telemetry'
                record(channel, source=peer[0], source_port=peer[1],
                       bytes=len(data), message=text, **extra)
                if peer[0] == a.router:
                    last_packet = time.monotonic()
        now = time.monotonic()
        if a.link_interface:
            path = Path('/sys/class/net') / a.link_interface
            try:
                current = ((path / 'carrier').read_text().strip(),
                           (path / 'operstate').read_text().strip())
            except OSError:
                current = ('unavailable', 'unavailable')
            if current != link_state:
                record('receiver', event='collector_link', interface=a.link_interface,
                       carrier=current[0], operstate=current[1])
                link_state = current
        if now - last_fsync >= 1:
            for handler in handlers.values():
                handler.flush()
                os.fsync(handler.stream.fileno())
            last_fsync = now
        if now - last_status >= 30:
            record('receiver', event='sender_status',
                   seconds_since_router_packet=None if last_packet is None else round(now - last_packet, 3),
                   received_datagrams=seen.copy(), rejected_source_datagrams=rejected)
            last_status = now


if __name__ == '__main__':
    raise SystemExit(main())
