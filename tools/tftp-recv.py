#!/usr/bin/env python3
"""Receive a file uploaded by U-Boot's `tftpput` (TFTP WRQ).

Separate from tftpd.py on purpose. tftpd.py serves images to the board and
refuses WRQ outright -- that read-only property is worth keeping on the tool
used for flashing, so accepting uploads lives here instead.

Used to pull NAND dumps off the board:

    nand read  0x84000000 0x200000 0x3300000        (on the board, read-only)
    tftpput    0x84000000 0x3300000 <tftp-host>:ubi_device.bin

Writes only into the directory given on the command line, refuses absolute
paths and anything containing "..", and will not overwrite an existing file --
a NAND dump is expensive to retake and silently clobbering one would be worse
than failing.

  RFC 1350 base, RFC 2347/2348/2349 options (blksize, timeout, tsize).

Usage:  tftp-recv.py <dest-dir> [bind-addr] [port]
"""

import os
import socket
import struct
import sys
import time

OP_RRQ, OP_WRQ, OP_DATA, OP_ACK, OP_ERROR, OP_OACK = 1, 2, 3, 4, 5, 6

ERR_ACCESS = 2
ERR_EXISTS = 6
ERR_ILLEGAL = 4

DEFAULT_BLKSIZE = 512
RETRIES = 6


def log(msg):
    print("%s  %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


def parse_rq(payload):
    parts = [p.decode("latin-1") for p in payload.split(b"\x00")]
    parts = [p for p in parts if p != ""]
    if len(parts) < 2:
        return None, None, {}
    opts = {}
    rest = parts[2:]
    for i in range(0, len(rest) - 1, 2):
        opts[rest[i].lower()] = rest[i + 1]
    return parts[0], parts[1].lower(), opts


def send_error(sock, peer, code, msg):
    sock.sendto(struct.pack("!HH", OP_ERROR, code) + msg.encode() + b"\x00", peer)
    log("  -> ERROR %d %s" % (code, msg))


def recv_file(destdir, peer, filename, opts):
    name = os.path.basename(filename)
    if not name or name.startswith(".") or "/" in filename and filename != name:
        # basename() already strips paths; this catches the odd/hostile cases.
        log("  !! rejecting suspicious name %r" % filename)
        name = None

    xfer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    xfer.bind(("", 0))
    try:
        if name is None:
            send_error(xfer, peer, ERR_ILLEGAL, "Bad filename")
            return
        path = os.path.join(destdir, name)
        if os.path.exists(path):
            send_error(xfer, peer, ERR_EXISTS, "File already exists")
            log("  !! %s exists; refusing to overwrite" % path)
            return

        blksize = DEFAULT_BLKSIZE
        timeout = 5.0
        ack = {}
        if "blksize" in opts:
            blksize = max(8, min(int(opts["blksize"]), 65464))
            ack["blksize"] = str(blksize)
        if "timeout" in opts:
            timeout = float(int(opts["timeout"]))
            ack["timeout"] = opts["timeout"]
        expect = None
        if "tsize" in opts:
            expect = int(opts["tsize"])
            ack["tsize"] = opts["tsize"]

        xfer.settimeout(timeout)
        log("  receiving %s blksize=%d%s" %
            (name, blksize, " expect=%d bytes" % expect if expect else ""))

        # With options: OACK. Without: ACK block 0. Either way the client then
        # starts at DATA block 1.
        if ack:
            pkt = struct.pack("!H", OP_OACK)
            for k, v in ack.items():
                pkt += k.encode() + b"\x00" + v.encode() + b"\x00"
        else:
            pkt = struct.pack("!HH", OP_ACK, 0)

        t0 = time.time()
        got = 0
        expected_block = 1
        with open(path, "wb") as fh:
            xfer.sendto(pkt, peer)
            retries = 0
            while True:
                try:
                    data, raddr = xfer.recvfrom(blksize + 4)
                except socket.timeout:
                    retries += 1
                    if retries >= RETRIES:
                        log("  !! timed out after %d bytes" % got)
                        return
                    xfer.sendto(pkt, peer)          # retransmit last ACK
                    continue
                if raddr[0] != peer[0] or len(data) < 4:
                    continue
                op, blk = struct.unpack("!HH", data[:4])
                if op == OP_ERROR:
                    log("  <- ERROR from client: %s"
                        % data[4:].rstrip(b"\x00").decode("latin-1"))
                    return
                if op != OP_DATA:
                    continue
                retries = 0
                if blk == (expected_block & 0xFFFF):
                    body = data[4:]
                    fh.write(body)
                    got += len(body)
                    pkt = struct.pack("!HH", OP_ACK, blk)
                    xfer.sendto(pkt, peer)
                    expected_block += 1
                    if len(body) < blksize:
                        dt = time.time() - t0
                        log("  DONE %s -- %d bytes in %.1fs (%.0f KB/s)"
                            % (name, got, dt, got / 1024.0 / max(dt, 0.001)))
                        if expect is not None and got != expect:
                            log("  !! WARNING: expected %d bytes, got %d" % (expect, got))
                        return
                else:
                    # Duplicate or out-of-order: re-ACK what we last accepted.
                    xfer.sendto(pkt, peer)
    finally:
        xfer.close()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    destdir = os.path.realpath(sys.argv[1])
    bind = sys.argv[2] if len(sys.argv) > 2 else "0.0.0.0"
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 69

    os.makedirs(destdir, exist_ok=True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind((bind, port))
    except OSError as e:
        print("cannot bind %s:%d -- %s" % (bind, port, e), file=sys.stderr)
        return 1

    log("tftp-recv listening on %s:%d, writing to %s" % (bind, port, destdir))
    log("waiting for an upload (Ctrl-C to stop)")

    while True:
        try:
            data, peer = sock.recvfrom(2048)
        except KeyboardInterrupt:
            log("stopped")
            return 0
        if len(data) < 2:
            continue
        op = struct.unpack("!H", data[:2])[0]
        if op == OP_RRQ:
            log("%s:%d RRQ refused (this tool only receives)" % peer)
            send_error(sock, peer, ERR_ACCESS, "Upload-only server")
            continue
        if op != OP_WRQ:
            continue
        filename, mode, opts = parse_rq(data[2:])
        if filename is None:
            send_error(sock, peer, ERR_ILLEGAL, "Malformed request")
            continue
        log("%s:%d WRQ %r mode=%s opts=%s" % (peer[0], peer[1], filename, mode, opts or "{}"))
        try:
            recv_file(destdir, peer, filename, opts)
        except Exception as e:
            log("  !! %s: %r" % (type(e).__name__, e))


if __name__ == "__main__":
    sys.exit(main())
