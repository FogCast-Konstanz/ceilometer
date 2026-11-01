import unittest
import sys
import struct
import zlib
import threading
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from fogcast.ceilometer import Ceilometer,CommandCancelled,DeviceError
from fogcast.prn_processing import sequence,process

class Serial:
    def __init__(self,corrupt=False,truncate=False):
        self.payload=b'';self.closed=False;self.corrupt=corrupt;self.truncate=truncate;self.release=False
    def read(self,n):
        part=self.payload[:min(n,17)];self.payload=self.payload[len(part):];return part
    def close(self): self.closed=True

class Client(Ceilometer):
    def __init__(self,**kwargs): self.serial=Serial(**kwargs);self._lock=threading.RLock()
    def _command(self,request):
        parts=request.split(',');name=parts[0]
        if name=='stream_result': return list(map(str,[7,1023,9775,10000000,1000000,2046,10001871,999982500,12,99,123]))
        if name=='stream_timing': return ['7','finite_burst','1500000000','500017500']
        if name=='stream_status': return ['1','0','100','60','3','7']
        if name=='stream_release': self.serial.release=True;return []
        if name=='stream_binary':
            identity,offset,count=map(int,parts[1:])
            payload=struct.pack('<6I',0x314c4943,1,identity,offset,count,1023)
            payload+=struct.pack(f'<{count}Q',*range(offset,offset+count))
            payload+=struct.pack('<I',zlib.crc32(payload))
            if self.serial.corrupt: payload=bytes([payload[0]^1])+payload[1:]
            size=len(payload)
            self.serial.payload=payload[:-1] if self.serial.truncate else payload
            return [str(size)]
        raise AssertionError(request)

class Tests(unittest.TestCase):
    def test_auto_port_selects_only_ceilometer_firmware(self):
        import types
        from unittest.mock import patch,Mock
        serial=types.ModuleType('serial');tools=types.ModuleType('serial.tools')
        ports=Mock();tools.list_ports=ports;serial.Serial=Mock()
        pico=lambda name,manufacturer='Fogcast',product='Fogcast Ceilometer':types.SimpleNamespace(vid=0x2e8a,pid=0x0009,device=name,manufacturer=manufacturer,product=product)
        with patch.dict(sys.modules,{'serial':serial,'serial.tools':tools}),patch('time.sleep'):
            other=pico('/dev/other','Raspberry Pi','Pico')
            wrong_vendor=pico('/dev/wrong','Another project','Fogcast Ceilometer')
            for candidates in ([],[other],[wrong_vendor],[other,pico('/dev/a'),pico('/dev/b')]):
                ports.comports.return_value=candidates
                with self.assertRaises(ConnectionError): Ceilometer('auto')
                serial.Serial.assert_not_called()
            ports.comports.return_value=[other,wrong_vendor,pico('/dev/ttyACM1')]
            Ceilometer('auto')
            self.assertEqual(serial.Serial.call_args.args[0],'/dev/ttyACM1')
    def test_download(self):
        client=Client();result=client.stream_histogram()
        self.assertEqual(result['histogram'],list(range(1023)))
        self.assertEqual(result['wall_ns'],1500000000)
        self.assertEqual(result['dead_time_ns'],500017500)
        self.assertEqual(result['boot_id'],99);self.assertEqual(result['dropped_results'],3)
        self.assertTrue(client.serial.release);self.assertFalse(client.serial.closed)
    def test_invalid_exposure_accounting(self):
        class InvalidTiming(Client):
            def _command(self,request):
                if request=='stream_timing': return ['7','finite_burst','1','0']
                return super()._command(request)
        client=InvalidTiming()
        with self.assertRaises(RuntimeError): client.stream_histogram()
        self.assertTrue(client.serial.closed);self.assertFalse(client.serial.release)
    def test_corruption(self):
        client=Client(corrupt=True)
        with self.assertRaises(RuntimeError): client.stream_histogram()
        self.assertTrue(client.serial.closed);self.assertFalse(client.serial.release)
    def test_truncation(self):
        client=Client(truncate=True)
        with self.assertRaises(TimeoutError): client.stream_histogram()
        self.assertTrue(client.serial.closed);self.assertFalse(client.serial.release)
    def test_cancel_between_pages_keeps_transport_synchronized(self):
        client=Client();checks=0
        def cancelled():
            nonlocal checks
            checks+=1
            if checks==2: raise CommandCancelled()
        with self.assertRaises(CommandCancelled): client.stream_histogram(cancel_check=cancelled)
        self.assertFalse(client.serial.closed);self.assertFalse(client.serial.release)
    def test_incomplete_ascii_ack_poison_closes_session(self):
        client=Client();client.serial.write=lambda data:len(data)
        client.serial.readline=lambda:b'$ACK,heartbeat'
        with self.assertRaises(TimeoutError): Ceilometer._command(client,'heartbeat')
        self.assertTrue(client.serial.closed)
    def test_matching_nack_keeps_session(self):
        client=Client();client.serial.write=lambda data:len(data)
        client.serial.readline=lambda:b'$NACK,heartbeat,FAULT\r\n'
        with self.assertRaises(DeviceError): Ceilometer._command(client,'heartbeat')
        self.assertFalse(client.serial.closed)
    def test_sequences(self):
        for n in (1023,4095):
            self.assertEqual(sum(sequence(n,1023)),(n+1)//2)
    def test_fft(self):
        try: import numpy as np
        except ImportError: self.skipTest('NumPy not installed in this test interpreter')
        for n in (1023,4095):
            code=np.asarray(sequence(n,1023));hist=7+3*np.roll(code,37)
            result=process(hist.tolist(),1023,10000000,1000000000,37)
            expected=n*3/2-3/(2*n) # (N^2-1)/(2N)*amplitude
            self.assertEqual(int(np.argmax(result['correlation_counts'])),37)
            self.assertAlmostEqual(result['correlation_counts'][37],expected,places=7)
            self.assertEqual(result['range_m'][37],0)
            self.assertEqual(result['correlation_counts_per_second'],result['correlation_counts'])
            self.assertTrue(np.max(np.abs(process([7]*n,1023,10000000,1000000000)['correlation_counts']))<1e-8)

if __name__=='__main__': unittest.main()
