"""Native-test child: Unix control protocol only, no networking."""
import os
from pathlib import Path
import signal
import socket
import sys
import time

base=Path('/root/wifi-fixture')
ctrl=Path('/run/nt98513-wifi/lo/wpa/lo')
s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);s.bind(str(ctrl));s.settimeout(.05)
monitor=None;seen=''
(base/'wpa.pid').write_text(str(os.getpid()))
while True:
    try:
        command,peer=s.recvfrom(4096)
        if command==b'ATTACH':
            monitor=peer;s.sendto(b'OK\n',peer)
        elif command==b'STATUS':
            if (base/'control-fail').exists():continue
            value=(base/'status').read_text() if (base/'status').exists() else 'COMPLETED 0 00:11:22:33:44:55'
            state,identity,bssid=value.split()
            s.sendto(f'wpa_state={state}\nid={identity}\nbssid={bssid}\n'.encode(),peer)
        elif command==b'PING':s.sendto(b'PONG\n',peer)
    except socket.timeout:pass
    if monitor and (base/'event').exists():
        event=(base/'event').read_text()
        if event!=seen:
            seen=event
            for item in event.splitlines():s.sendto(('<3>'+item+'\n').encode(),monitor)
