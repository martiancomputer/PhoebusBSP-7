#!/usr/bin/env python3
"""Exercise startup races with real flock locks and fake firewall/sysctl state."""
import fcntl
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def mock():
    name = Path(sys.argv[0]).name
    if name == 'ip':
        print('inet 192.0.2.1/24' if sys.argv[1] == '-4' else 'inet6 2001:db8:1::1/64')
        return 0
    directory = Path(os.environ['FW_TEST_DIR'])
    args = sys.argv[1:]
    assert args[:2] == ['-w', '10'], (name, args)
    with (directory / 'xtables.lock').open('a+') as lock:
        deadline = time.monotonic() + 2
        while True:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if time.monotonic() >= deadline:
                    return 4
                time.sleep(.01)
        state_file = directory / 'state.json'
        state = json.loads(state_file.read_text()) if state_file.exists() else {}
        family = '6' if name.startswith('ip6') else '4'
        event = {'owner': os.environ['FW_TEST_OWNER'], 'tool': name, 'args': args}
        if name.endswith('-restore'):
            # Real restore owns xtables.lock while reading its input. A producer
            # that queries iptables mid-stream cannot acquire the same lock.
            payload = sys.stdin.read()
            assert payload.startswith('*filter\n') and payload.endswith('COMMIT\n')
            assert f'-A PHOEBUS_SSH{family} -i wan0 -j DROP' in payload
            assert f'-A PHOEBUS_SSH{family} -i lan0 -s ' in payload
            if state.get(family):
                assert f'-D INPUT -p tcp --dport 22 -j PHOEBUS_SSH{family}' in payload
            state[family] = True
            event['payload'] = payload
            result = 0
        elif args[2:] == ['-S']:
            if os.environ.get('FW_TEST_FAIL_LIST') == '1':
                result = 4
            else:
                if state.get(family):
                    print(f'-N PHOEBUS_SSH{family}')
                result = 0
        elif '-C' in args:
            if 'INPUT' in args:
                # A fresh legacy table cannot resolve the not-yet-created
                # PHOEBUS_SSH target. This is not the missing-rule status 1.
                result = 0 if state.get(family) else 2
            else:
                result = 1
        elif args[2:] == ['-F']:
            state[family] = False
            result = 0
        elif os.environ.get('FW_TEST_FAIL_NAT') == '1' and 'MASQUERADE' in args:
            result = 5
        else:
            result = 0
        event['result'] = result
        state_file.write_text(json.dumps(state))
        fd = os.open(directory / 'events.jsonl', os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        os.write(fd, (json.dumps(event) + '\n').encode())
        os.close(fd)
        time.sleep(.01)
        return result


def main():
    root = Path(__file__).resolve().parents[1]
    scripts = root / 'sdk/rootfs/etc/s6/scripts'
    with tempfile.TemporaryDirectory(prefix='phoebus-fw-test-') as temporary:
        directory = Path(temporary)
        binaries = directory / 'bin'
        binaries.mkdir()
        for command in ('ip', 'iptables', 'ip6tables', 'iptables-restore', 'ip6tables-restore'):
            (binaries / command).symlink_to(Path(__file__).resolve())
        sysctl = directory / 'sysctl'
        (sysctl / 'net/ipv4/conf/all').mkdir(parents=True)
        env = dict(os.environ, PATH=str(binaries) + ':' + os.environ['PATH'],
                   PHOEBUS_FIREWALL_COMMON=str(scripts / 'firewall-lock'),
                   PHOEBUS_FIREWALL_LOCK=str(directory / 'policy.lock'),
                   PHOEBUS_SSH_POLICY=str(scripts / 'ssh-policy'),
                   PHOEBUS_SYSCTL_ROOT=str(sysctl), FW_TEST_DIR=temporary,
                   LAN_IFACE='lan0', WAN_IFACE='wan0')
        env.pop('PHOEBUS_FIREWALL_LOCKED', None)
        def launch(script, owner, **extra):
            return subprocess.Popen(['sh', str(scripts / script)], env=dict(env, FW_TEST_OWNER=owner, **extra),
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        with (directory / 'policy.lock').open('a+') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            processes = [launch('nat-up', 'nat'), launch('ssh-policy', 'ssh-a'), launch('ssh-policy', 'ssh-b')]
            time.sleep(.2)
            assert all(p.poll() is None for p in processes)
            assert not (directory / 'events.jsonl').exists()
            fcntl.flock(lock, fcntl.LOCK_UN)
        for process in processes:
            output, error = process.communicate(timeout=15)
            assert process.returncode == 0, (output, error)
        events = [json.loads(line) for line in (directory / 'events.jsonl').read_text().splitlines()]
        owners = [owner for owner, _ in itertools.groupby(event['owner'] for event in events)]
        assert len(owners) == 3 and set(owners) == {'nat', 'ssh-a', 'ssh-b'}, owners
        assert any('MASQUERADE' in event['args'] for event in events)
        assert not any('-t' in event['args'] and 'mangle' in event['args'] and '-F' in event['args'] for event in events)

        with (directory / 'xtables.lock').open('a+') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            process = launch('ssh-policy', 'external-contention')
            time.sleep(.2)
            assert process.poll() is None
            fcntl.flock(lock, fcntl.LOCK_UN)
        output, error = process.communicate(timeout=10)
        assert process.returncode == 0, (output, error)
        process = launch('ssh-policy', 'listing-failure', FW_TEST_FAIL_LIST='1')
        output, error = process.communicate(timeout=10)
        assert process.returncode != 0, (output, error)

        process = launch('nat-up', 'injected-failure', FW_TEST_FAIL_NAT='1')
        output, error = process.communicate(timeout=10)
        assert process.returncode != 0 and 'nat: masquerading' not in output, (output, error)
        process = launch('ssh-policy', 'after-failure')
        output, error = process.communicate(timeout=10)
        assert process.returncode == 0, (output, error)
    print('PASS: concurrent NAT/SSH startup serialized, nested lock safe, external contention waits, no restore self-deadlock, critical failures propagate, mangle preserved')


if __name__ == '__main__':
    if Path(sys.argv[0]).name in ('ip', 'iptables', 'ip6tables', 'iptables-restore', 'ip6tables-restore'):
        raise SystemExit(mock())
    main()
