# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
"""Exercise production Qt and Sunshine tunnels with simulated USB/IP endpoints.
No physical devices, driver commands, or installed profiles are used.
"""
from pathlib import Path
import concurrent.futures, json, os, socket, ssl, struct, subprocess, tempfile, time

root = Path(__file__).resolve().parent.parent
config = json.loads((root/'build/environment.json').read_text(encoding='utf-8-sig'))
def configured(key): return (root/'build'/config[key]).resolve()
qt = configured('qtRoot')/'bin'
msys = configured('msysRoot')/'ucrt64/bin'
env = dict(os.environ, PATH=str(qt) + os.pathsep + str(msys) + os.pathsep + os.environ['PATH'])
client = root/'out/tests/usb-client/release/test-usb-client.exe'
host = root/'out/tests/test-usb-host.exe'
pool = concurrent.futures.ThreadPoolExecutor(6)

def listener(port=0):
    s = socket.socket(); s.bind(('127.0.0.1', port)); s.listen(4); s.settimeout(10); return s
def exact(s, n):
    data = b''
    while len(data) < n:
        chunk = s.recv(n-len(data))
        if not chunk: raise EOFError((len(data), n))
        data += chunk
    return data
def line(s):
    data=b''
    while not data.endswith(b'\n'):
        data += exact(s, 1)
        assert len(data)<4096
    return json.loads(data)

with tempfile.TemporaryDirectory(prefix='usb-test-', dir=root/'out/tests') as tmp:
    tmp=Path(tmp)
    for name in ['server','client','wrong']:
        subprocess.run([str(msys/'openssl.exe'),'req','-x509','-newkey','rsa:2048','-nodes',
            '-keyout',str(tmp/f'{name}.key'),'-out',str(tmp/f'{name}.pem'),'-days','1',
            '-subj',f'/CN=usb-test-{name}'],env=env,check=True,capture_output=True)
    (tmp/'token').write_text('a'*64)
    serverctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    serverctx.load_cert_chain(tmp/'server.pem',tmp/'server.key')
    serverctx.load_verify_locations(tmp/'client.pem'); serverctx.verify_mode=ssl.CERT_REQUIRED
    clientctx=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT); clientctx.check_hostname=False
    clientctx.load_verify_locations(tmp/'server.pem'); clientctx.load_cert_chain(tmp/'client.pem',tmp/'client.key')
    payload=bytes(range(256))*40000  # > high-water mark, forces backpressure/resume
    reply=b'USB-response'+bytes(range(256))*40000
    index=0
    def probe(port, localport, direction, wrong=False):
        global index
        index+=1; stop=tmp/f'stop-{index}'
        p=subprocess.Popen([str(client),'127.0.0.1',str(port),str(tmp/('wrong.pem' if wrong else 'server.pem')),
            str(tmp/'client.pem'),str(tmp/'client.key'),str(tmp/'token'),'1-2',str(localport),str(stop),direction],
            env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        return p,stop
    # Qt local-export and local-import both pump beyond the bounded queue size.
    for direction in ['forward','import']:
        tls_listener=listener(); usb_listener=listener() if direction=='forward' else None
        def tls_peer():
            raw,_=tls_listener.accept()
            with serverctx.wrap_socket(raw,server_side=True) as s:
                s.settimeout(15); request=line(s)
                assert request == {'op':direction,'token':'a'*64,'busid':'1-2'}
                s.sendall(b'{"op":"ready"}\n')
                if direction=='forward':
                    s.sendall(payload); assert exact(s,len(reply))==reply
                else:
                    assert exact(s,len(payload))==payload; s.sendall(reply)
                time.sleep(.2)
        future=pool.submit(tls_peer)
        p,stop=probe(tls_listener.getsockname()[1],usb_listener.getsockname()[1] if usb_listener else 3240,direction)
        try:
            if direction=='forward': usb,_=usb_listener.accept()
            else:
                value=p.stdout.readline().strip(); assert value.startswith('LISTENING '),value
                usb=socket.create_connection(('127.0.0.1',int(value.split()[1])),timeout=10)
            with usb:
                usb.settimeout(15)
                if direction=='forward': assert exact(usb,len(payload))==payload; usb.sendall(reply)
                else: usb.sendall(payload); assert exact(usb,len(reply))==reply
            future.result(20)
            out,err=p.communicate(timeout=10); assert p.returncode==0,(out,err)
            print('PASS Qt',direction,'10 MiB each direction, backpressure and disconnect',flush=True)
        finally:
            if p.poll() is None: p.kill(); p.wait()
            tls_listener.close()
            if usb_listener: usb_listener.close()
    # A TLS certificate mismatch must prevent sending the token/USB handshake.
    tls_listener=listener(); usb_listener=listener()
    def wrong_peer():
        raw,_=tls_listener.accept()
        try:
            with serverctx.wrap_socket(raw,server_side=True) as s:
                s.settimeout(5); return s.recv(1)
        except (ssl.SSLError,ConnectionError): return b''
    future=pool.submit(wrong_peer)
    p,stop=probe(tls_listener.getsockname()[1],usb_listener.getsockname()[1],'forward',True)
    usb,_=usb_listener.accept()
    out,err=p.communicate(timeout=10); usb.close(); tls_listener.close(); usb_listener.close()
    assert p.returncode==1 and future.result(5)==b'',(out,err)
    print('PASS Qt rejects a mismatched certificate before disclosing token',flush=True)
    # Actual Sunshine service: selected-device USB/IP handshake enforcement.
    usb_listener=listener()
    hoststop=tmp/'host-stop'
    p=subprocess.Popen([str(host),str(tmp/'server.pem'),str(tmp/'server.key'),str(tmp/'client.pem'),str(hoststop),str(usb_listener.getsockname()[1])],
        env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        value=p.stdout.readline().strip()
        while value and not value.startswith('PORT '): value=p.stdout.readline().strip()
        assert value.startswith('PORT '),value
        port=int(value.split()[1])
        packet=struct.pack('!HHI',0x111,0x8003,0)+b'1-2'.ljust(32,b'\0')
        for malformed in [False,True]:
            with clientctx.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=5),server_hostname='usb-test-server') as tls:
                tls.sendall(json.dumps({'op':'import','token':'a'*64,'busid':'1-2'}).encode()+b'\n')
                usb,_=usb_listener.accept(); usb.settimeout(5)
                with usb:
                    assert line(tls)=={'op':'ready'}
                    candidate=packet if not malformed else packet.replace(b'1-2',b'1-3')
                    # Fragment the wire header to exercise accumulation.
                    for part in [candidate[:3],candidate[3:27],candidate[27:]]: tls.sendall(part)
                    if malformed:
                        assert usb.recv(100)==b''
                    else:
                        assert exact(usb,40)==packet
                        usb.sendall(b'test-reply'); assert exact(tls,10)==b'test-reply'
            time.sleep(.2)
        print('PASS Sunshine accepts selected device, rejects substitution, releases/reuses slot',flush=True)
        with clientctx.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=5),server_hostname='usb-test-server') as tls:
            tls.sendall(b'{"op":"import","token":"wrong","busid":"1-2"}\n')
            assert line(tls)['op']=='error'
        print('PASS Sunshine rejects invalid token',flush=True)
    finally:
        hoststop.touch()
        try: out,err=p.communicate(timeout=10)
        except subprocess.TimeoutExpired: p.kill(); out,err=p.communicate(); raise
        usb_listener.close()
        assert p.returncode==0,(out,err)
pool.shutdown(wait=True)
print('PASS bidirectional transport regression suite')
