"""Native-test DHCP child: emits lease helper calls, never opens IP sockets."""
import os
import json
from pathlib import Path
import signal
import subprocess
import time
base=Path('/root/wifi-fixture')
with (base/'dhcp-starts').open('a') as f:f.write(os.environ['NTW_GENERATION']+'\n')
(base/'dhcp.pid').write_text(str(os.getpid()))
(base/'lease-env').write_text(json.dumps({k:v for k,v in os.environ.items() if k.startswith('NTW_')}))
os.environ.update(interface='lo',ip='192.168.3.5',subnet='255.255.255.0',router='192.168.3.1',dns='1.1.1.1 8.8.8.8',lease='30')
if (base/'lease-seconds').exists():os.environ['lease']=(base/'lease-seconds').read_text()
mode=(base/'dhcp-mode').read_text() if (base/'dhcp-mode').exists() else 'lease'
if mode=='resist':signal.signal(signal.SIGTERM,signal.SIG_IGN)
if mode!='silent':
    result=subprocess.run(['/out/wifi-process-test','lease','bound'],check=False)
    (base/'helper-result').write_text(str(result.returncode))
if mode=='exit':raise SystemExit(0)
while True:time.sleep(.05)
