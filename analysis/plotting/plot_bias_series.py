import csv
from pathlib import Path
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
r=Path(__file__).resolve().parents[2]
out=Path(__file__).resolve().parents[2]/'build/figures'
out.mkdir(parents=True,exist_ok=True)
rows=list(csv.DictReader((r/'analysis/bench-tests/legacy/bias-series-1809p4.csv').open(encoding='utf-8-sig')))
data=[v for v in rows if v['mode']=='on']
x=np.array([(int(v['target_start_uv'])+int(v['target_end_uv']))/2e6 for v in data]); y=np.array([float(v['rate_cps']) for v in data]);n=np.array([int(v['counts']) for v in data]);dt=np.array([int(v['duration_us'])/1e6 for v in data]);e=np.sqrt(n)/dt
temps=[int(v[k])/1000 for v in data for k in ('temperature_start_mc','temperature_end_mc')]
plt.rcParams.update({'font.size':11,'axes.spines.top':False,'axes.spines.right':False,'svg.fonttype':'none'})
fig,(ax,bx)=plt.subplots(1,2,figsize=(12,6),layout='constrained')
fig.suptitle('Dark-condition count rate versus bias voltage',fontsize=18,fontweight='bold')
ax.errorbar(x,y/1000,yerr=e/1000,fmt='o-',color='#1769aa',capsize=3)
ax.set(xlabel='Temperature-compensated bias target (V)',ylabel='Count rate (kcps)',title='Linear rate axis');ax.grid(alpha=.25)
positive=y>0;zero=~positive
bx.plot(x,np.where(positive,y,np.nan),'o-',color='#1769aa')
bx.errorbar(x[positive],y[positive],yerr=e[positive],fmt='none',ecolor='#1769aa',capsize=3)
bx.scatter(x[zero],3/dt[zero],marker='v',color='#d55e00',label='Zero counts: ≈95% Poisson upper limit')
bx.set(yscale='log',xlabel='Temperature-compensated bias target (V)',ylabel='Count rate (cps)',title='Logarithmic rate axis');bx.grid(alpha=.25);bx.legend(fontsize=9,loc='upper left')
for a in (ax,bx):
 a.set_xticks(np.arange(40,51,2))
off=[v for v in rows if v['mode'].startswith('off')]
text='; '.join(f"{v['mode'].replace('_',' ')}: {v['counts']} counts / {int(v['duration_us'])/1e6:g} s" for v in off)
fig.supxlabel(f'Threshold 1809.4 mV (DAC 47432) · receiver {min(temps):.3f}–{max(temps):.3f} °C · 2 s per point\n{text}\nLaser and PRN off · bias values are model targets, not simultaneous voltage measurements · counting-error bars only',fontsize=10)
for ext in ('png','svg'):fig.savefig(out/f'bias-series-1809p4.{ext}',dpi=180)
print('Voltage_V,OV_V,count_rate_cps')
for v,rate,p in zip(x,y,data):print(f'{v:.5f},{int(p["ov_mv"])/1000:g},{rate:g}')
print('temp_range',min(temps),max(temps))
