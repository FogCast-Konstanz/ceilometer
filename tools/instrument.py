"""Single USB owner shared by the Pi dashboard and command-line recorder."""
import copy
import json
import threading
import time
import shutil
import logging
from datetime import datetime, timezone
from pathlib import Path
from ceilometer import Ceilometer,CommandCancelled
from prn_processing import process

class Cancelled(CommandCancelled):
    pass

class Recorder:
    def __init__(self, directory):
        self.directory=Path(directory)
        self.directory.mkdir(parents=True,exist_ok=True)
    def write(self, result, processed):
        directory=self.directory/result['received_utc'][:10]
        directory.mkdir(exist_ok=True)
        name=f"{result['boot_id']:016x}-{result['id']:010d}"
        # Publish complete files atomically; keep original counts even if processing fails.
        for suffix,data in (('.json',result),('.profile.json',processed)):
            if data is None: continue
            path=directory/(name+suffix)
            temporary=path.with_suffix(path.suffix+'.tmp')
            temporary.write_text(json.dumps(data,allow_nan=False)+'\n',encoding='utf-8')
            temporary.replace(path)
        return str(directory/(name+'.json'))

class Instrument:
    def __init__(self,port,output='measurements',settle_seconds=10,device_factory=Ceilometer,
                 processor=process,recorder=None,simulation=False,enable_inhibit=None):
        self.enable_inhibit=enable_inhibit
        self.port=port;self.output=str(output);self.settle_seconds=max(0,settle_seconds)
        self.factory=device_factory;self.processor=processor
        self.recorder=recorder if recorder is not None else Recorder(output)
        self.lock=threading.RLock();self.io=threading.RLock()
        self.closing=threading.Event();self.halt=threading.Event();self.cancel=threading.Event();self.cancel.set()
        self.device=None;self.epoch=0;self.fault=None;self.stop_thread=None
        self.config={};self.telemetry_index=0;self.telemetry_count=0
        self.latest=None;self.profile_serial=0
        self.runtime_supported=False;self.runtime_poll=0;self.last_boot=None;self.disconnect_count=0;self.reboot_count=0
        manifest=Path(__file__).resolve().parents[1]/'release.json'
        self.release=json.loads(manifest.read_text()) if manifest.exists() else None
        self.data={'state':'DISCONNECTED','enabled':False,'mode':'laser','connected':False,
                   'stop_confirmed':False,'stop_confirmed_utc':None,'message':'Connecting to Pico',
                   'runtime':None,'usb_disconnects':0,'pico_reboots':0,
                   'fault':None,'profiles':0,'dropped_results':0,'last_received_utc':None,
                   'stream':None,'thermal':None,'bias':None,'telemetry':{},'configuration':{},
                   'deployment':{'release_id':self.release['release_id'],'source_revision':self.release['source_revision']} if self.release else None,'enable_inhibit':enable_inhibit,'simulation':simulation,'board':None,'port':port,'output':self.output,'firmware':None,'last_file':None}
    def start(self):
        self.worker=threading.Thread(target=self._loop,name='instrument',daemon=True)
        self.keeper=threading.Thread(target=self._heartbeat,name='heartbeat',daemon=True)
        self.worker.start();self.keeper.start()
    def snapshot(self):
        with self.lock:
            data=copy.deepcopy(self.data)
            data['fault']=self.fault;data['profile_serial']=self.profile_serial
            data['server_utc']=datetime.now(timezone.utc).isoformat()
        # Filesystem statistics must never hold the lock needed to request Stop.
        try: data['disk_free_bytes']=shutil.disk_usage(self.output).free
        except OSError: data['disk_free_bytes']=None
        return data
    def profile(self):
        with self.lock: return copy.deepcopy(self.latest)
    def _check(self,epoch):
        with self.lock:
            if self.halt.is_set() or not self.data['enabled'] or epoch!=self.epoch: raise Cancelled()
    def _call(self,command,epoch=None):
        # Disable changes intent before waiting on this lock. Every startup command
        # rechecks its epoch under the same lock used to send Stop.
        with self.io:
            if epoch is not None: self._check(epoch)
            if self.device is None: raise ConnectionError('Pico is disconnected')
            return self.device.command(command)
    def _device_call(self,method,epoch=None):
        with self.io:
            if epoch is not None: self._check(epoch)
            if self.device is None: raise ConnectionError('Pico is disconnected')
            if method=='stream_histogram' and epoch is not None:
                return self.device.stream_histogram(cancel_check=lambda:self._check(epoch))
            return getattr(self.device,method)()
    def enable(self,mode='laser'):
        if mode not in ('laser','bias','dark'): raise ValueError('Unknown measurement mode')
        with self.lock:
            if self.closing.is_set(): raise ValueError('Instrument service is shutting down')
            if self.enable_inhibit: raise ValueError('Measurements locked: '+self.enable_inhibit)
            if self.data['enabled']: return
            if (self.stop_thread and self.stop_thread.is_alive()) or not self.data['connected'] or not self.data['stop_confirmed']:
                raise ValueError('Wait for the board to confirm disabled outputs')
            if self.fault: raise ValueError('Reset the fault while disabled before enabling')
            self.epoch+=1;self.cancel.clear()
            self.data.update(enabled=True,mode=mode,state='STARTING',stop_confirmed=False,
                             stop_confirmed_utc=None,message='Starting measurement')
    def disable(self):
        with self.lock:
            self.epoch+=1;self.cancel.set()
            self.data.update(enabled=False,state='STOPPING',stop_confirmed=False,
                             stop_confirmed_utc=None,message='Disable requested; awaiting board confirmation')
            if self.stop_thread is None or not self.stop_thread.is_alive():
                self.stop_thread=threading.Thread(target=self._stop_and_verify,name='priority-stop',daemon=True)
                self.stop_thread.start()
    def _stop_and_verify(self):
        try:
            with self.io:
                self._call('stop')
                state=self._call('get_state');thermal=self._call('get_temperature_control');bias=self._call('get_bias')
                if len(state)!=8 or len(thermal)!=7 or len(bias)!=10:
                    raise RuntimeError('Malformed shutdown verification')
                if state[0] not in ('IDLE','FAULT') or state[5]!='0' or state[6]!='0' or thermal[0]!='0' or bias[0]!='0':
                    raise RuntimeError('Board has not confirmed all outputs disabled')
            with self.lock:
                self.data.update(stop_confirmed=True,stop_confirmed_utc=datetime.now(timezone.utc).isoformat(),
                                 state='FAULT' if self.fault else 'DISABLED',
                                 message='Disabled: board acknowledged laser, bias and TEC off',
                                 thermal=self._thermal(thermal),bias=self._bias(bias),board={'state':state[0],'armed':False})
                if state[0]=='FAULT' and not self.fault:
                    self.fault='Board fault is latched; reset explicitly while disabled'
                    self.data['state']='FAULT'
        except Exception as error:
            with self.lock:
                self.data.update(stop_confirmed=False,state='DISCONNECTED' if not self.data['connected'] else 'FAULT',
                                 message=f'Shutdown unconfirmed: {error}')
    def reset_fault(self):
        with self.lock:
            if self.data['enabled'] or not self.data['stop_confirmed']:
                raise ValueError('Disable and wait for board confirmation first')
        self._call('clear_fault')
        with self.lock:
            self.fault=None;self.data.update(state='DISABLED',message='Fault cleared; outputs remain disabled')
    def _fail(self,error):
        with self.lock:
            self.fault=str(error)
        self.disable()
    @staticmethod
    def _thermal(v):
        if len(v)!=7: raise RuntimeError('Malformed temperature status')
        return dict(enabled=v[0]=='1',target_c=int(v[1])/1000,valid=v[2]=='1',
                    temperature_c=int(v[3])/1000,output_mv=int(v[4]),polarity=int(v[5]),fault=v[6]=='1')
    @staticmethod
    def _bias(v):
        if len(v)!=10: raise RuntimeError('Malformed bias status')
        return dict(enabled=v[0]=='1',fault=v[1]=='1',reason=int(v[2]),valid=v[3]=='1',
                    temperature_c=int(v[4])/1000,age_us=int(v[5]),breakdown_v=int(v[6])/1e6,
                    target_v=int(v[7])/1e6,estimated_v=int(v[8])/1e6,code=int(v[9]))
    def _publish_phase(self,epoch,state,message):
        with self.lock:
            self._check(epoch);self.data.update(state=state,message=message)
    def _connect(self):
        device=self.factory(self.port)
        with self.io: self.device=device
        with self.lock: self.data.update(connected=True,message='Verifying disabled outputs')
        self._stop_and_verify()
        with self.lock:
            if not self.data['stop_confirmed']: raise RuntimeError(self.data['message'])
        info=self._call('get_info')
        if len(info)<6 or tuple(map(int,info[1].split('.'))) < (0,5,0):
            raise RuntimeError('Continuous measurement requires firmware 0.5.0 or later')
        self.runtime_supported=tuple(map(int,info[1].split('.'))) >= (0,6,1)
        self.config=self._load_parameters()
        self.telemetry_count=int(self._call('telemetry_count')[0]);self.telemetry_index=0
        if not 0<=self.telemetry_count<=512: raise RuntimeError('Invalid telemetry inventory')
        with self.lock:
            self.data.update(firmware=info,configuration=copy.deepcopy(self.config))
    def _disconnect(self,error):
        logging.warning('Pico USB disconnected: %s',error)
        with self.lock:
            if self.data['connected']: self.disconnect_count+=1
            self.data['usb_disconnects']=self.disconnect_count
            was_enabled=self.data['enabled']
            if was_enabled: self.fault=str(error)
            self.epoch+=1;self.cancel.set()
            self.data.update(connected=False,enabled=False,state='DISCONNECTED',stop_confirmed=False,
                             message=f'USB unavailable; shutdown unconfirmed: {error}')
        with self.io:
            if self.device:
                try: self.device.serial.close()
                except Exception: pass
            self.device=None
    def _load_parameters(self,epoch=None):
        # Release IO between responses so priority Stop is never blocked by a
        # complete inventory scan. Startup cancellation is checked per request.
        count=int(self._call('parameter_count',epoch)[0])
        if not 1<=count<=128: raise RuntimeError('Invalid parameter inventory')
        result={}
        for index in range(count):
            info=self._call(f'parameter_info,{index}',epoch)
            value=self._call(f'get_param,{info[1]}',epoch)
            if len(info)!=6 or len(value)!=6 or info[1]!=value[0]: raise RuntimeError('Malformed parameter inventory')
            result[info[1]]=dict(value=int(value[1]),unit=value[2],min=int(info[3]),max=int(info[4]),default=int(info[5]))
        return result
    def _startup(self,epoch,mode):
        self.config=self._load_parameters(epoch)
        with self.lock: self.data['configuration']=copy.deepcopy(self.config)
        def p(name): return self.config[name]['value']
        if mode=='laser':
            if p('tec_polarity')==0 or not 0<p('laser_dac_code')<=p('laser_max_code'):
                raise ValueError('Commission TEC polarity and laser DAC/ceiling first')
            deadline=time.monotonic()+300
            self._publish_phase(epoch,'WARMING','Waiting for valid laser temperature')
            while True:
                v=self._thermal(self._call('get_temperature_control',epoch))
                with self.lock: self.data['thermal']=v
                if v['valid']: break
                if time.monotonic()>deadline: raise TimeoutError('No valid laser temperature')
                self.cancel.wait(.1);self._check(epoch)
            self._call('tec_enable,1',epoch);stable=None
            while True:
                v=self._thermal(self._call('get_temperature_control',epoch))
                with self.lock: self.data['thermal']=v
                if v['fault']: raise RuntimeError('Thermal fault')
                if v['valid'] and abs(v['temperature_c']-v['target_c'])<=.5:
                    stable=stable if stable is not None else time.monotonic()
                    if time.monotonic()-stable>=self.settle_seconds: break
                else: stable=None
                self._publish_phase(epoch,'WARMING','Stabilizing laser temperature')
                if time.monotonic()>deadline: raise TimeoutError('Temperature did not stabilize')
                self.cancel.wait(.1);self._check(epoch)
        if mode in ('bias','laser'):
            self._publish_phase(epoch,'STARTING','Enabling temperature-compensated receiver bias')
            self._call('bias_enable,1',epoch)
        if mode=='laser':
            self._call('arm,1',epoch);self._call('laser_dac',epoch)
        self._call('stream_start' if mode=='laser' else 'stream_capture',epoch)
        self._publish_phase(epoch,'MEASURING','Acquiring PRN profiles in finite bursts')
    def _poll(self):
        if self.runtime_supported and time.monotonic()-self.runtime_poll>=1:
            v=self._call('get_runtime')
            if len(v) not in (3,4): raise RuntimeError('Malformed runtime diagnostics')
            boot,uptime,watchdog=map(int,v[:3])
            stage=int(v[3]) if len(v)==4 else 0
            if self.last_boot is not None and boot!=self.last_boot:
                self.reboot_count+=1
                logging.warning('Pico reboot detected; watchdog=%s previous_stage=%s uptime_us=%s',bool(watchdog),stage,uptime)
            self.last_boot=boot;self.runtime_poll=time.monotonic()
            with self.lock:
                self.data.update(runtime=dict(boot_id=str(boot),uptime_us=uptime,watchdog_reboot=bool(watchdog),previous_stage=stage),pico_reboots=self.reboot_count)
        thermal=self._thermal(self._call('get_temperature_control'));bias=self._bias(self._call('get_bias'))
        state=self._call('get_state')
        with self.lock:
            self.data.update(thermal=thermal,bias=bias,board={'state':state[0],'armed':state[5]=='1'})
            enabled=self.data['enabled'];confirmed=self.data['stop_confirmed']
        if enabled and (state[0]=='FAULT' or thermal['fault'] or bias['fault']):
            raise RuntimeError('Board fault; measurement disabled')
        if not enabled and confirmed and (state[5]!='0' or state[6]!='0' or thermal['enabled'] or bias['enabled']):
            raise RuntimeError('Unexpected active output while disabled')
        for _ in range(min(4,self.telemetry_count)):
            v=self._call(f'get_telemetry,{self.telemetry_index}')
            if len(v)!=7: raise RuntimeError('Malformed telemetry')
            value=dict(unit=v[1],value=int(v[2]),valid=v[3]=='1',sampled_us=int(v[4]),
                       age_us=int(v[5]),sequence=int(v[6]),received_utc=datetime.now(timezone.utc).isoformat())
            with self.lock: self.data['telemetry'][v[0]]=value
            self.telemetry_index=(self.telemetry_index+1)%self.telemetry_count
    def _measure(self,epoch):
        v=self._call('stream_status',epoch)
        if len(v)!=7 or v[0]!='1' or v[1]!='0': raise RuntimeError(f'Acquisition failed: {v}')
        status=dict(active=True,failed=False,blocks=int(v[2]),max_processing_us=int(v[3]),
                    dropped_results=int(v[4]),pending_id=int(v[5]),error_flags=int(v[6]))
        with self.lock: self.data.update(stream=status,dropped_results=int(v[4]))
        if v[5]=='0': return
        result=self._device_call('stream_histogram',epoch)
        result['received_utc']=datetime.now(timezone.utc).isoformat()
        result['simulation']=self.data['simulation']
        with self.lock:
            result['configuration']=copy.deepcopy(self.config)
            result['telemetry']=copy.deepcopy(self.data['telemetry'])
        # Preserve raw data before attempting calibrated/derived processing.
        path=self.recorder.write(result,None)
        derived=self.processor(result['histogram'],self.config['prn_seed']['value'],
                               result['chip_hz'],result['exposure_ns'],self.config['range_offset_bins']['value'])
        self.recorder.write(result,derived)
        latest=dict(simulation=result['simulation'],id=result['id'],boot_id=str(result['boot_id']),received_utc=result['received_utc'],
                    exposure_s=result['exposure_ns']/1e9,wall_s=result.get('wall_ns',result['exposure_ns'])/1e9,
                    dead_time_s=result.get('dead_time_ns',0)/1e9,acquisition_mode=result.get('acquisition_mode','simulation'),
                    length=result['length'],frames=result['frames'],
                    chip_hz=result['chip_hz'],histogram=result['histogram'],**derived)
        with self.lock:
            self.latest=latest;self.profile_serial+=1
            self.data.update(profiles=self.profile_serial,last_received_utc=result['received_utc'],last_file=path)
    def _loop(self):
        started_epoch=None
        while not self.halt.is_set():
            if self.device is None:
                try: self._connect()
                except Exception as error:
                    self._disconnect(error);self.halt.wait(2);continue
            try:
                with self.lock:
                    enabled=self.data['enabled'];epoch=self.epoch;mode=self.data['mode'];confirmed=self.data['stop_confirmed']
                if enabled and started_epoch!=epoch:
                    self._startup(epoch,mode);started_epoch=epoch
                if enabled: self._measure(epoch)
                # Priority Stop owns the connection until verification completes.
                if enabled or confirmed: self._poll()
            except Cancelled: pass
            except Exception as error:
                if self.device is None or not self.device.serial.is_open:
                    self._disconnect(error)
                else: self._fail(error)
            self.halt.wait(.05)
    def _heartbeat(self):
        while not self.halt.wait(.1):
            with self.lock: enabled=self.data['enabled']
            if enabled:
                try: self._call('heartbeat')
                except Exception as error:
                    self._fail(error)
    def close(self):
        self.closing.set();self.disable()
        if self.stop_thread: self.stop_thread.join(timeout=3)
        self.halt.set();self.cancel.set()
        if hasattr(self,'worker'): self.worker.join(timeout=3);self.keeper.join(timeout=2)
        with self.io:
            if self.device:
                self.device.serial.close();self.device=None
