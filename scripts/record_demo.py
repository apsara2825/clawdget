#!/usr/bin/env python3
"""Record an SSH terminal session (running clawdget on the router) into an
asciinema v2 .cast file, using a local pty. The router runs nothing extra."""
import os, pty, select, time, json, sys, subprocess

import os as _os
_user, _host, _pw = _os.environ.get("DEMO_SSH_USER", "root"), \
    _os.environ["DEMO_SSH_HOST"], _os.environ["DEMO_SSH_PASS"]
HOST_CMD = ["sshpass", "-p", _pw, "ssh", "-tt",
	    "-o", "StrictHostKeyChecking=no",
	    "-o", "KexAlgorithms=+diffie-hellman-group1-sha1,diffie-hellman-group14-sha1",
	    "-o", "HostKeyAlgorithms=+ssh-rsa,ssh-dss",
	    "-o", "Ciphers=+aes128-cbc,3des-cbc,aes256-cbc",
	    f"{_user}@{_host}"]
COMMAND = os.environ.get("DEMO_CMD",
    'clawdget -n "帮我检查一下路由器现在有没有异常"')

def read_until(fd, deadline, markers, max_wait):
    """read chunks until one of markers seen or deadline; returns (buf, marker)"""
    buf = b""
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.5)
        if not r:
            continue
        try:
            data = os.read(fd, 4096)
        except OSError:
            return buf, "eof"
        if not data:
            return buf, "eof"
        buf += data
        for m in markers:
            if m in buf:
                return buf, m
    return buf, "deadline"

def main():
    cast_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/demo.cast"
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execvp(HOST_CMD[0], HOST_CMD)
    t0 = time.time()
    events = []
    def emit(t, data):
        events.append((t - t0, "o", data.decode("utf-8", "replace")))

    # 1. wait for shell prompt
    buf, _ = read_until(fd, t0 + 30, (b"root@", b"# "), None)
    emit(time.time(), buf)
    time.sleep(0.6)

    # 2. type the command (echoed by pty)
    os.write(fd, COMMAND.encode() + b"\r")
    # capture the echo
    time.sleep(0.4)
    buf, _ = read_until(fd, time.time() + 2, (b"$ ", b"# ", b"?"), None)
    emit(time.time(), buf)

    # 3. run until clawdget finishes (next shell prompt appears)
    t_start = time.time()
    full = b""
    while time.time() - t_start < 240:
        buf, marker = read_until(fd, time.time() + 5, (), None)
        emit(time.time(), buf)
        full += buf
        # crude finish detection: prompt reappears after the report
        if full.count(b"admin@router") >= 2:
            break
    time.sleep(0.8)
    buf, _ = read_until(fd, time.time() + 2, (), None)
    emit(time.time(), buf)
    os.write(fd, b"exit\r")
    time.sleep(0.5)
    try:
        buf, _ = read_until(fd, time.time() + 3, (), None)
        emit(time.time(), buf)
    except Exception:
        pass
    os.close(fd)

    # write cast
    header = {"version": 2, "width": 110, "height": 34, "timestamp": int(t0),
              "env": {"SHELL": "/bin/sh", "TERM": "xterm-256color"}}
    with open(cast_path, "w") as f:
        f.write(json.dumps(header) + "\n")
        for t, etype, data in events:
            f.write(json.dumps([round(t, 6), etype, data]) + "\n")
    dur = events[-1][0] if events else 0
    print(f"cast written: {cast_path}, duration {dur:.1f}s, {len(events)} events")

if __name__ == "__main__":
    main()
