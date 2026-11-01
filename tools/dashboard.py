"""Pi-hosted dashboard. One instrument owner; no shell commands from HTTP."""
import argparse
import hmac
import json
import os
import signal
import threading
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
from instrument import Instrument
ROOT=Path(__file__).resolve().parent.parent
ASSETS={
    '/':('pi-dashboard/index.html','text/html; charset=utf-8'),
    '/dashboard.css':('pi-dashboard/dashboard.css','text/css; charset=utf-8'),
    '/dashboard.js':('pi-dashboard/dashboard.js','text/javascript; charset=utf-8'),
    '/webglplot.js':('gui/vendor/webgl-plot/webglplot.umd.js','text/javascript; charset=utf-8'),
}

def json_safe(value):
    if isinstance(value,int) and not isinstance(value,bool) and abs(value)>9007199254740991: return str(value)
    if isinstance(value,dict): return {k:json_safe(v) for k,v in value.items()}
    if isinstance(value,list): return [json_safe(v) for v in value]
    return value

class Server(ThreadingHTTPServer):
    daemon_threads=True
    def __init__(self,address,instrument,token=None):
        self.instrument=instrument;self.token=token
        super().__init__(address,Handler)

class Handler(BaseHTTPRequestHandler):
    server_version='CeilometerDashboard/1.0'
    def setup(self):
        super().setup();self.connection.settimeout(5)
    def _send(self,status,body,mime='application/json; charset=utf-8'):
        if not isinstance(body,bytes): body=json.dumps(json_safe(body),allow_nan=False).encode()
        self.send_response(status)
        self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)))
        self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        self.end_headers()
        try: self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError): pass
    def _authorized(self):
        token=self.server.token
        if token and not hmac.compare_digest(self.headers.get('Authorization','').encode(),f'Bearer {token}'.encode()):
            self._send(401,{'error':'Enter the dashboard access token'});return False
        # Loopback-only operation without a token must reject DNS-rebinding hosts.
        if not token and self.headers.get('Host','').split(':')[0] not in ('localhost','127.0.0.1','[','::1'):
            self._send(403,{'error':'Host is not permitted'});return False
        return True
    def do_GET(self):
        path=urlsplit(self.path).path
        if path in ASSETS:
            file,mime=ASSETS[path];self._send(200,(ROOT/file).read_bytes(),mime);return
        if not self._authorized(): return
        if path=='/api/status': self._send(200,self.server.instrument.snapshot())
        elif path=='/api/profile': self._send(200,self.server.instrument.profile())
        else: self._send(404,{'error':'Not found'})
    def do_POST(self):
        if not self._authorized(): return
        origin=self.headers.get('Origin')
        if origin and origin!=f'http://{self.headers.get("Host")}' and origin!=f'https://{self.headers.get("Host")}':
            self._send(403,{'error':'Cross-origin control is not permitted'});return
        try:
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<=4096 or self.headers.get('Content-Type','').split(';')[0]!='application/json':
                raise ValueError('Expected a bounded JSON request')
            data=json.loads(self.rfile.read(size));path=urlsplit(self.path).path
            if not isinstance(data,dict): raise ValueError('Expected a JSON object')
            if path=='/api/disable': self.server.instrument.disable()
            elif path=='/api/enable': self.server.instrument.enable(data.get('mode','laser'))
            elif path=='/api/reset-fault': self.server.instrument.reset_fault()
            else: self._send(404,{'error':'Not found'});return
            self._send(202,self.server.instrument.snapshot())
        except (ValueError,RuntimeError,ConnectionError) as error:
            self._send(409,{'error':str(error)})
    def log_message(self,*args): pass

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo',action='store_true',help='Simulated instrument; no USB or real outputs')
    parser.add_argument('port',nargs='?',default=None,help='Pico USB CDC device, preferably /dev/serial/by-id/...')
    parser.add_argument('--listen',default='127.0.0.1')
    parser.add_argument('--http-port',type=int,default=8080)
    parser.add_argument('--output',default='measurements')
    parser.add_argument('--settle-seconds',type=float,default=10)
    parser.add_argument('--token-env',default='CEILOMETER_WEB_TOKEN')
    args=parser.parse_args();token=os.environ.get(args.token_env)
    if token and token.startswith('REPLACE_'): parser.error('Replace the example access token')
    if args.listen not in ('127.0.0.1','localhost') and (not token or len(token)<24):
        parser.error(f'Set {args.token_env} to an access token of at least 24 characters for network access')
    if not args.demo and not args.port: parser.error('Specify a USB port or --demo')
    import numpy # Load before connecting/enabling hardware; fail early if dependency is missing.
    if args.demo:
        from simulated_instrument import SimulatedPico
        instrument=Instrument('SIMULATED',str(Path(args.output)/'simulation'),0,device_factory=SimulatedPico,simulation=True)
    else: instrument=Instrument(args.port,args.output,args.settle_seconds,enable_inhibit=os.environ.get('CEILOMETER_INHIBIT_ENABLE'))
    server=Server((args.listen,args.http_port),instrument,token)
    def shutdown(*_): threading.Thread(target=server.shutdown,daemon=True).start()
    signal.signal(signal.SIGTERM,shutdown);signal.signal(signal.SIGINT,shutdown)
    instrument.start()
    print(f'Dashboard http://{args.listen}:{server.server_port} — starts disabled',flush=True)
    try: server.serve_forever(poll_interval=.2)
    finally: server.server_close();instrument.close()

if __name__=='__main__': main()
