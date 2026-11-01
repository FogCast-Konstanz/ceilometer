import copy
import http.client
import json
import sys
import threading
import time
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from fogcast.instrument import Instrument
from fogcast.simulated_instrument import SimulatedPico
from fogcast.dashboard import Server

class MemoryRecorder:
    def __init__(self): self.records=[]
    def write(self,result,processed): self.records.append((copy.deepcopy(result),processed));return 'memory.json'

def processed(histogram,*args):
    return dict(range_m=list(range(len(histogram))),correlation_counts=[0]*len(histogram),correlation_counts_per_second=[0]*len(histogram))

def wait_for(test,timeout=3):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        if test(): return
        time.sleep(.01)
    raise AssertionError('Timed out waiting for condition')

class DashboardTests(unittest.TestCase):
    def setUp(self):
        self.device=SimulatedPico('test');self.device.config['measurement_period_ms']['value']=100
        self.recorder=MemoryRecorder()
        self.instrument=Instrument('test',settle_seconds=0,device_factory=lambda port:self.device,
                                   recorder=self.recorder,processor=processed,simulation=True)
        self.instrument.start();wait_for(lambda:self.instrument.snapshot()['stop_confirmed'])
    def tearDown(self): self.instrument.close()
    def test_start_disabled_measure_and_disable(self):
        self.assertFalse(self.instrument.snapshot()['enabled'])
        self.assertFalse(self.device.armed or self.device.bias or self.device.tec)
        self.instrument.enable('laser')
        wait_for(lambda:self.instrument.snapshot()['profiles']>=1)
        self.assertTrue(self.device.streaming and self.device.armed and self.device.bias and self.device.tec)
        self.instrument.disable();wait_for(lambda:self.instrument.snapshot()['stop_confirmed'])
        self.assertFalse(self.device.streaming or self.device.armed or self.device.bias or self.device.tec)
        last_stop=len(self.device.commands)-1-self.device.commands[::-1].index('stop')
        time.sleep(.2)
        self.assertFalse(any(c in ('arm,1','tec_enable,1','bias_enable,1','laser_dac','stream_start') for c in self.device.commands[last_stop+1:]))
        self.assertIsNotNone(self.instrument.profile());self.assertTrue(self.recorder.records)
    def test_maintenance_lock_blocks_all_enable_modes(self):
        self.instrument.enable_inhibit='500 MHz acquisition awaiting qualification'
        for mode in ('laser','bias','dark'):
            with self.assertRaises(ValueError): self.instrument.enable(mode)
        self.assertFalse(self.device.streaming or self.device.tec or self.device.armed)
    def test_cancel_warmup_without_later_emission(self):
        self.instrument.settle_seconds=30
        self.instrument.enable('laser');wait_for(lambda:self.device.tec)
        self.instrument.disable();wait_for(lambda:self.instrument.snapshot()['stop_confirmed'])
        self.assertNotIn('arm,1',self.device.commands);self.assertNotIn('stream_start',self.device.commands)
        self.assertFalse(self.instrument.snapshot()['enabled'])
    def test_disable_during_inflight_enable(self):
        original=self.device.command;entered=threading.Event();release=threading.Event()
        def command(request):
            if request=='tec_enable,1': entered.set();release.wait(2)
            return original(request)
        self.device.command=command
        self.instrument.enable('laser');self.assertTrue(entered.wait(2))
        self.instrument.disable()
        self.assertFalse(self.instrument.snapshot()['stop_confirmed'])
        release.set();wait_for(lambda:self.instrument.snapshot()['stop_confirmed'])
        self.assertNotIn('arm,1',self.device.commands);self.assertFalse(self.device.tec)
    def test_fault_does_not_restart(self):
        self.instrument.enable('laser');wait_for(lambda:self.device.streaming)
        self.device.fault=True
        wait_for(lambda:self.instrument.snapshot()['fault'] and self.instrument.snapshot()['stop_confirmed'])
        with self.assertRaises(ValueError): self.instrument.enable('laser')
        self.instrument.reset_fault();self.assertEqual(self.instrument.snapshot()['state'],'DISABLED')
        self.assertFalse(self.device.streaming)
    def test_disconnected_never_claims_servicing_confirmed(self):
        self.device.serial.close()
        wait_for(lambda:not self.instrument.snapshot()['connected'])
        self.instrument.disable();time.sleep(.1)
        self.assertFalse(self.instrument.snapshot()['stop_confirmed'])
        with self.assertRaises(ValueError): self.instrument.enable()
    def test_failed_shutdown_not_confirmed(self):
        original=self.device.command
        def command(request):
            if request=='stop': raise RuntimeError('No stop ACK')
            return original(request)
        self.device.command=command
        self.instrument.disable();time.sleep(.1)
        self.assertFalse(self.instrument.snapshot()['stop_confirmed'])
    def test_recording_failure_disables(self):
        def fail(*args): raise OSError('Disk full')
        self.recorder.write=fail
        self.instrument.enable('dark')
        wait_for(lambda:self.instrument.snapshot()['fault'] and self.instrument.snapshot()['stop_confirmed'])
        self.assertIn('Disk full',self.instrument.snapshot()['fault']);self.assertFalse(self.device.streaming)
    def test_http_controls_auth_origin_and_assets(self):
        server=Server(('127.0.0.1',0),self.instrument,'test-token')
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        def request(method,path,body=None,extra=None,auth=True):
            connection=http.client.HTTPConnection('127.0.0.1',server.server_port,timeout=3)
            headers={'Content-Type':'application/json'}
            if auth: headers['Authorization']='Bearer test-token'
            headers.update(extra or {})
            connection.request(method,path,None if body is None else json.dumps(body),headers)
            response=connection.getresponse();status=response.status;data=response.read();connection.close()
            return status,data
        try:
            self.assertEqual(request('GET','/',auth=False)[0],200)
            self.assertEqual(request('GET','/api/status',auth=False)[0],401)
            self.assertEqual(request('GET','/api/status')[0],200)
            self.assertEqual(request('POST','/api/enable',{'mode':'dark'},{'Origin':'http://foreign.example'})[0],403)
            self.assertEqual(request('POST','/api/enable',{'mode':'unknown'})[0],409)
            self.assertEqual(request('POST','/api/enable',{'mode':'dark'})[0],202)
            wait_for(lambda:self.device.streaming)
            self.assertEqual(request('POST','/api/disable',{})[0],202)
            wait_for(lambda:self.instrument.snapshot()['stop_confirmed'])
            self.assertFalse(self.device.streaming)
            self.assertEqual(request('GET','/../tools/instrument.py')[0],404)
        finally: server.shutdown();server.server_close();thread.join(2)

if __name__=='__main__': unittest.main()
