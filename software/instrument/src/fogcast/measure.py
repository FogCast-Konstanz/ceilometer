"""Command-line recorder using the same instrument controller as the Pi dashboard."""
import argparse
import time
from pathlib import Path
from .instrument import Instrument

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port')
    parser.add_argument('--laser',action='store_true',help='Explicitly enable commissioned TEC, bias and laser')
    parser.add_argument('--bias',action='store_true',help='Enable receiver bias for laser-disabled measurements')
    parser.add_argument('--settle-seconds',type=float,default=10)
    parser.add_argument('--output',type=Path,default=Path('measurements'))
    parser.add_argument('--profiles',type=int,default=0,help='0: run until interrupted')
    args=parser.parse_args()
    import numpy
    instrument=Instrument(args.port,args.output,args.settle_seconds);instrument.start()
    try:
        deadline=time.monotonic()+10
        while not instrument.snapshot()['stop_confirmed']:
            if time.monotonic()>deadline: raise TimeoutError(instrument.snapshot()['message'])
            time.sleep(.1)
        instrument.enable('laser' if args.laser else 'bias' if args.bias else 'dark')
        received=0
        while not args.profiles or received<args.profiles:
            state=instrument.snapshot()
            if state['fault'] or not state['enabled']: raise RuntimeError(state['fault'] or state['message'])
            if state['profiles']!=received:
                received=state['profiles'];profile=instrument.profile()
                print(f"{profile['id']}: {profile['frames']} periods, {profile['exposure_s']:.7f} s, dropped {state['dropped_results']}",flush=True)
            time.sleep(.1)
    finally: instrument.close()

if __name__=='__main__': main()
