#!/usr/bin/env python3
"""Local UDP/retention and service-graph regression tests; no router access."""
import json
import base64
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    services = ROOT / 'sdk/rootfs/etc/s6/source'
    graph = {}
    for directory in services.iterdir():
        if not directory.is_dir():
            continue
        dep = directory / 'dependencies'
        graph[directory.name] = dep.read_text().split() if dep.exists() else []
    visiting, done = set(), set()

    def visit(name):
        assert name in graph and name not in visiting, name
        if name in done:
            return
        visiting.add(name)
        for dep in graph[name]:
            visit(dep)
        visiting.remove(name)
        done.add(name)

    for name in graph:
        visit(name)
    assert 'log-policy' not in graph['syslogd']
    assert 'log-policy' in graph['kmsg-relay']
    bundle = (services / 'ok-all/contents').read_text().split()
    assert 'telemetry' not in bundle and 'kmsg-relay' not in bundle

    with tempfile.TemporaryDirectory(prefix='phoebus-log-test-') as temporary:
        directory = Path(temporary)
        reservations = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(3)]
        for s in reservations:
            s.bind(('127.0.0.1', 0))
        ports = [s.getsockname()[1] for s in reservations]
        for s in reservations:
            s.close()
        command = [sys.executable, str(ROOT / 'tools/phoebus-log-receiver.py'),
                   '--listen', '127.0.0.1', '--router', '127.0.0.3',
                   '--directory', temporary, '--max-mib', '1', '--backups', '1',
                   '--syslog-port', str(ports[0]), '--kernel-port', str(ports[1]),
                   '--netconsole-port', str(ports[2])]
        process = subprocess.Popen(command)
        try:
            time.sleep(.4)
            assert process.poll() is None
            assert subprocess.run(command, timeout=3).returncode == 0
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as rejected:
                rejected.bind(('127.0.0.2', 0))
                rejected.sendto(b'UNTRUSTED_SOURCE_MARKER', ('127.0.0.1', ports[1]))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                binary = b'non-UTF8 kernel fragment: \xff\xfe'
                s.sendto(binary, ('127.0.0.1', ports[1]))
                s.sendto(b'<30>Oct 19 12:00:00 phoebus-router app: SYSTEM_ONLY_MARKER',
                         ('127.0.0.1', ports[0]))
                for port in ports:
                    s.sendto(b'phoebus-health: VALID_TEST_MARKER', ('127.0.0.1', port))
                time.sleep(.3)
                for channel in ('telemetry', 'kernel', 'netconsole'):
                    records = [json.loads(line) for line in (directory / (channel + '.jsonl')).read_text().splitlines()]
                    assert any('VALID_TEST_MARKER' in record['message'] for record in records)
                    assert not any('UNTRUSTED_SOURCE_MARKER' in record['message'] for record in records)
                    if channel == 'kernel':
                        assert any(base64.b64decode(record.get('raw_base64', '')) == binary for record in records)
                assert 'SYSTEM_ONLY_MARKER' in (directory / 'syslog.jsonl').read_text()
                assert 'SYSTEM_ONLY_MARKER' not in (directory / 'telemetry.jsonl').read_text()
                for _ in range(3000):
                    s.sendto(b'x' * 1000, ('127.0.0.1', ports[1]))
                    time.sleep(.001)
                time.sleep(.5)
            assert (directory / 'kernel.jsonl.1').exists()
            for file in directory.glob('kernel.jsonl*'):
                assert file.stat().st_size <= 1024 * 1024 + 4096
                assert not file.stat().st_mode & 0o077
            assert not (directory / 'kernel.jsonl.2').exists()
        finally:
            process.terminate()
            process.wait(timeout=3)
    print('PASS: dependency graph, UDP channels, source filtering, duplicate lock, private files, bounded rotation')


if __name__ == '__main__':
    main()
