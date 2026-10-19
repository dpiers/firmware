"""Native Linux subprocess/control tests; only network backend and child executables are fake."""
from pathlib import Path
import os
import json
import signal
import socket
import struct
import shutil
import subprocess
import time
import unittest

HERE=Path(__file__).resolve().parent
SRC=HERE.parent/'package/nt98513-wifi-control/src'
@unittest.skipUnless(os.environ.get('NT98513_WIFI_NATIVE_TESTS')=='1','run in isolated Linux native container')
class NativeProcesses(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-O2','-I',str(SRC),
            str(HERE/'wifi_process_native.c'),str(SRC/'state.c'),str(SRC/'config.c'),str(SRC/'lease.c'),'-o','/out/wifi-process-test'],check=True)
    def setUp(self):
        # Disposable container-only fixture. No production source/output is removed.
        for path in ('/root/wifi-fixture','/run/nt98513-wifi'):
            if Path(path).exists():shutil.rmtree(path)
        self.fixture=Path('/root/wifi-fixture');self.fixture.mkdir(mode=0o700)
        self.proc=None
    def tearDown(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait(timeout=5)
    def wait_for(self,predicate,timeout=5):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if predicate():return
            time.sleep(.03)
        self.fail('condition timed out')
    def starts(self):
        p=self.fixture/'dhcp-starts'
        return p.read_text().splitlines() if p.exists() else []
    def start(self):
        self.proc=subprocess.Popen(['/out/wifi-process-test','run'])
        self.wait_for(lambda:len(self.starts())==1)
        self.wait_for(lambda:'apply' in (self.fixture/'trace').read_text())
    def test_same_identity_disconnect_retires_generation(self):
        self.start();first=self.starts()[0]
        (self.fixture/'event').write_text('CTRL-EVENT-DISCONNECTED\nCTRL-EVENT-CONNECTED\n')
        self.wait_for(lambda:len(self.starts())==2)
        self.assertNotEqual(first,self.starts()[1])
        stop=subprocess.run(['/out/wifi-process-test','stop'],capture_output=True,timeout=7)
        self.assertEqual(stop.returncode,0,stop.stdout)
        self.assertEqual(self.proc.wait(timeout=5),0)
    def test_failed_cleanup_stop_reports_failure(self):
        self.start();(self.fixture/'clear-fail').touch()
        stop=subprocess.run(['/out/wifi-process-test','stop'],capture_output=True,timeout=7)
        self.assertNotEqual(stop.returncode,0,stop.stdout)
        self.assertNotEqual(self.proc.wait(timeout=5),0)
        self.assertTrue(Path('/run/nt98513-wifi/lo/active').exists())
    def test_status_identity_and_duplicate_events(self):
        self.start()
        (self.fixture/'event').write_text('CTRL-EVENT-CONNECTED\n')
        time.sleep(.3);self.assertEqual(len(self.starts()),1)
        (self.fixture/'status').write_text('COMPLETED 0 00:11:22:33:44:66')
        self.wait_for(lambda:len(self.starts())==2)
        self.assertEqual(subprocess.run(['/out/wifi-process-test','stop'],capture_output=True,timeout=7).returncode,0)
        self.assertEqual(self.proc.wait(timeout=5),0)

    def stop(self):
        result=subprocess.run(['/out/wifi-process-test','stop'],capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertEqual(result.stdout,b'STOPPED\n')
        self.assertEqual(self.proc.wait(timeout=5),0)
    def packet(self,env,kind=1):
        return (b'NTW1'+struct.pack('!Q',int(env['NTW_GENERATION']))+bytes.fromhex(env['NTW_TOKEN'])+
                struct.pack('!I',kind)+socket.inet_aton('192.168.3.5')+socket.inet_aton('255.255.255.0')+
                socket.inet_aton('192.168.3.1')+socket.inet_aton('1.1.1.1')+b'\0'*8+struct.pack('!III',1,30,1))
    def send_packet(self,p):
        with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as s:
            s.settimeout(4);s.connect('/run/nt98513-wifi/lo/lease');s.send(p);return s.recv(8)
    def test_lease_sender_length_token_and_generation(self):
        self.start();env=json.loads((self.fixture/'lease-env').read_text());p=self.packet(env)
        count=(self.fixture/'trace').read_text().count('apply')
        self.assertEqual(self.send_packet(p+b'x'),b'NO')
        self.assertEqual(self.send_packet(p[:12]+b'x'*16+p[28:]),b'NO')
        self.assertEqual(self.send_packet(p[:64]+struct.pack('!I',99)),b'NO')
        self.assertEqual(self.send_packet(p[:28]+struct.pack('!I',99)+p[32:]),b'NO')
        self.assertEqual((self.fixture/'trace').read_text().count('apply'),count)
        (self.fixture/'event').write_text('CTRL-EVENT-DISCONNECTED\nCTRL-EVENT-CONNECTED\n')
        self.wait_for(lambda:len(self.starts())==2)
        self.assertEqual(self.send_packet(p),b'NO');self.stop()
    def test_lease_ack_waits_for_bounded_network_work(self):
        (self.fixture/'apply-delay').touch();self.start()
        self.wait_for(lambda:(self.fixture/'helper-result').exists())
        self.assertEqual((self.fixture/'helper-result').read_text(),'0');self.stop()
    def test_silent_lease_expiry(self):
        (self.fixture/'lease-seconds').write_text('1');self.start()
        self.wait_for(lambda:len(self.starts())>=2)
        self.assertGreaterEqual((self.fixture/'trace').read_text().count('clear'),1);self.stop()
    def test_supplicant_death(self):
        self.start();pid=int((self.fixture/'wpa.pid').read_text());os.kill(pid,signal.SIGKILL)
        self.assertNotEqual(self.proc.wait(timeout=5),0)
        self.assertIn('clear',(self.fixture/'trace').read_text())
        self.assertFalse(Path('/run/nt98513-wifi/lo/active').exists())
    def test_control_socket_failure(self):
        self.start();(self.fixture/'control-fail').touch()
        self.assertNotEqual(self.proc.wait(timeout=5),0)
        self.assertIn('clear',(self.fixture/'trace').read_text())
    def test_exec_failure(self):
        (self.fixture/'exec-fail').touch()
        result=subprocess.run(['/out/wifi-process-test','run'],timeout=5)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(Path('/run/nt98513-wifi/lo/active').exists())
    def test_term_resistant_dhcp_and_restart(self):
        (self.fixture/'dhcp-mode').write_text('resist');self.start()
        pid=int((self.fixture/'dhcp.pid').read_text());self.stop()
        with self.assertRaises(ProcessLookupError):os.kill(pid,0)
        (self.fixture/'dhcp-mode').write_text('lease')
        # Real second invocation reuses clean private runtime and locks.
        self.proc=subprocess.Popen(['/out/wifi-process-test','run'])
        self.wait_for(lambda:len(self.starts())==2);self.stop()
    def test_waitpid_interruption_preserves_ownership(self):
        result=subprocess.run(['/out/wifi-process-test','wait-eintr'],env={**os.environ,'TEST_WAIT_EINTR':'1'},timeout=5)
        self.assertEqual(result.returncode,0)
    def test_entropy_no_fallback_and_deadline(self):
        for flag in ('TEST_ENTROPY_AGAIN','TEST_ENTROPY_FAIL'):
            before=time.monotonic()
            result=subprocess.run(['/out/wifi-process-test','entropy'],env={**os.environ,flag:'1'},timeout=2)
            self.assertNotEqual(result.returncode,0);self.assertLess(time.monotonic()-before,1)
        self.assertEqual(subprocess.run(['/out/wifi-process-test','entropy']).returncode,0)
    def test_owner_config_file_admission(self):
        p=self.fixture/'owner.conf'
        content='interface=lo\ncountry=US\nssid=74\npsk='+'01'*32+'\nresolver_owner=exclusive\n'
        p.write_text(content);p.chmod(0o600)
        def check(path,ok):
            result=subprocess.run(['/out/wifi-process-test','config',str(path)],timeout=2)
            self.assertEqual(result.returncode==0,ok)
        check(p,True);p.chmod(0o644);check(p,False);p.chmod(0o600)
        link=self.fixture/'link';link.symlink_to(p);check(link,False)
        fifo=self.fixture/'fifo';os.mkfifo(fifo,0o600);check(fifo,False)
        p.write_text(content+'network={\n');check(p,False)
        p.write_text(content);self.fixture.chmod(0o777);check(p,False);self.fixture.chmod(0o700)
    def test_silent_acquisition_has_deadline_and_backoff(self):
        (self.fixture/'dhcp-mode').write_text('silent')
        self.proc=subprocess.Popen(['/out/wifi-process-test','run'])
        self.wait_for(lambda:len(self.starts())==1)
        # Exercise real monotonic acquisition deadline, not a policy stub.
        self.wait_for(lambda:len(self.starts())==2,timeout=39)
        self.assertNotIn('apply',(self.fixture/'trace').read_text());self.stop()

if __name__=='__main__':unittest.main()
