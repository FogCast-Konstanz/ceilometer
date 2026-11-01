"""Explicit offline dashboard simulator. Never opens a USB port."""
import json
import math
import time
from pathlib import Path
from .prn_processing import sequence
from .ceilometer import DeviceError
class Serial:
    is_open=True
    def close(self): self.is_open=False
class SimulatedPico:
    def __init__(self,port):
        self.serial=Serial();self.tec=self.bias=self.armed=self.streaming=False;self.laser=0
        self.fault=False;self.id=0;self.started=time.monotonic();self.next_result=0;self.commands=[]
        metadata=json.loads((Path(__file__).resolve().parent/'demo-metadata.json').read_text())
        self.config={v['name']:dict(v,value=v['default']) for v in metadata['parameters']}
        self.config['tec_polarity']['value']=1;self.config['laser_dac_code']['value']=32505
    def parameters(self): return self.config
    def command(self,request):
        if not self.serial.is_open: raise ConnectionError('Simulated USB disconnected')
        self.commands.append(request);name,*args=request.split(',');now=time.monotonic()
        if name=='stop': self.tec=self.bias=self.armed=self.streaming=False;self.laser=0;return []
        if name=='get_info': return ['ceilometer-demo','0.5.0','pico2','500000000','10000000','1023','1','1','1']
        if name=='get_state': return ['FAULT' if self.fault else 'ACQUIRING' if self.streaming else 'IDLE','0','0','0','0',str(int(self.armed)),str(int(self.bias)),'0']
        if name=='get_temperature_control': return list(map(str,[int(self.tec),24000,1,24000,int(20*math.sin(now)),1,int(self.fault)]))
        if name=='get_bias': return list(map(str,[int(self.bias),int(self.fault),0,1,25000,0,36500000,46500000,46500000,56373 if self.bias else 0]))
        if name=='parameter_count': return [str(len(self.config)),'4']
        if name=='parameter_info':
            key=list(self.config)[int(args[0])];v=self.config[key]
            return list(map(str,[args[0],key,v['unit'],v['min'],v['max'],v['default']]))
        if name=='get_param':
            v=self.config[args[0]];return list(map(str,[args[0],v['value'],v['unit'],v['min'],v['max'],v['default']]))
        if name=='telemetry_count': return ['6']
        if name=='get_telemetry':
            values=[('receiver_temp_mc','mC',25000),('laser_temp_mc','mC',24000),('laser_current_ma','mA',500 if self.streaming and self.laser else 0),
                    ('dac0_readback','code',self.laser),('dac1_readback','code',56373 if self.bias else 0),('lt_output_uv','uV',20000 if self.tec else 0)]
            key,unit,value=values[int(args[0])];return list(map(str,[key,unit,value,1,int(now*1e6)%2**32,0,int(now*10)%2**32]))
        if name=='heartbeat': return []
        if name=='clear_fault': self.fault=False;return []
        if name=='tec_enable': self.tec=args[0]=='1';return []
        if name=='bias_enable': self.bias=args[0]=='1';return []
        if name=='arm': self.armed=args[0]=='1';return []
        if name=='laser_dac': self.laser=int(args[0]) if args else self.config['laser_dac_code']['value'];return []
        if name in ('stream_start','stream_capture'):
            self.streaming=True;self.next_result=now+self.config['measurement_period_ms']['value']/1000;return []
        if name=='stream_status': return list(map(str,[int(self.streaming),int(self.fault),int((now-self.started)*2441),70,0,self.id+1 if self.streaming and now>=self.next_result else 0,0]))
        raise DeviceError('Unknown simulated command: '+request)
    def stream_histogram(self,cancel_check=None):
        if cancel_check: cancel_check()
        if not self.streaming: raise RuntimeError('No simulated acquisition')
        self.id+=1;self.next_result=time.monotonic()+self.config['measurement_period_ms']['value']/1000
        n=self.config['prn_length']['value'];hz=self.config['chip_hz']['value']
        frames=int(hz*self.config['measurement_period_ms']['value']/1000/n)
        code=sequence(n,self.config['prn_seed']['value'])
        # Shifted PRN return plus deterministic background variation, all simulated.
        histogram=[100+int(4*math.sin(i*.7))+20*code[(i-110)%n] for i in range(n)]
        return dict(id=self.id,length=n,frames=frames,chip_hz=hz,period_us=self.config['measurement_period_ms']['value']*1000,
                    first_chip=(self.id-1)*frames*n,end_chip=self.id*frames*n,exposure_ns=frames*n*1000000000//hz,
                    armed_monotonic_us=0,boot_id=12345,configuration_crc32=0,histogram=histogram,dropped_results=0)
