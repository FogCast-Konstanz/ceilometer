import sys,csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
root=Path(__file__).resolve().parents[1]/'bench-tests/legacy'
out=Path(__file__).resolve().parents[2]/'build/figures'
out.mkdir(parents=True,exist_ok=True)
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.titlesize':13,'axes.labelsize':12,'axes.linewidth':.8,'axes.spines.top':False,'axes.spines.right':False,'xtick.direction':'out','ytick.direction':'out','savefig.bbox':'tight','svg.fonttype':'none'})
def fig():
 f,a=plt.subplots(figsize=(6,4.3),layout='constrained');a.grid(axis='y',color='#e4e4e4',lw=.6);a.set_axisbelow(True);return f,a
def save(f,name):
 for ext in ('png','svg'):f.savefig(out/f'{name}.{ext}',dpi=400)
 plt.close(f)
data=list(csv.DictReader((root/'bias-off-idle-repeat.csv').open(encoding='utf-8-sig')))
x=np.array([float(v['threshold_mv']) for v in data]);y=np.array([float(v['rate_cps']) for v in data]);
f,a=fig();a.plot(x,y/1e6,'o-',color='#234f72',lw=1.5,ms=4);a.axvline(1809.4,color='#b34d35',ls='--',lw=1.2);a.set(xlim=(1804,1812),xlabel='Comparator threshold voltage (mV)',ylabel='Bias-off count rate (Mcps)',title='Noise-floor threshold selection: 1809.4 mV');save(f,'publication-noise-threshold')
data=list(csv.DictReader((root/'high-threshold-dark-sweep.csv').open(encoding='utf-8-sig')))
f,a=fig()
for mode,color,marker in [('0','#777777','s'),('1','#234f72','o')]:
 rows=[v for v in data if v['bias_enabled']==mode];x=np.array([float(v['threshold_mv']) for v in rows]);y=np.array([float(v['rate_cps']) for v in rows]);duration=np.array([int(v['duration_us'])/1e6 for v in rows]);positive=y>0
 a.plot(x,y,'o-',color=color,lw=1.5,ms=4)
a.axvline(1809.4,color='#b34d35',ls='--',lw=1.2)
a.set_yscale('symlog',linthresh=1,linscale=1)
a.set(xlim=(1809.05,1811.1),ylim=(-.15,2e6),xlabel='Comparator threshold voltage (mV)',ylabel='Count rate (s⁻¹)',title='Threshold selection at 1809.4 mV\nBias on (blue), bias off (gray)');a.set_xticks([1809.2,1809.6,1810.0,1810.4,1810.8]);save(f,'publication-threshold-response')
data=[v for v in csv.DictReader((root/'bias-series-1809p4.csv').open(encoding='utf-8-sig')) if v['mode']=='on']
x=np.array([(int(v['target_start_uv'])+int(v['target_end_uv']))/2e6 for v in data]);ov=np.array([int(v['ov_mv'])/1000 for v in data]);y=np.array([float(v['rate_cps']) for v in data]);counts=np.array([int(v['counts']) for v in data]);positive=y>0;duration=np.array([int(v['duration_us'])/1e6 for v in data])
for axis,values,xlabel,name in [('bias',x,'SiPM bias target voltage (V)','publication-dark-count-bias'),('ov',ov,'SiPM overvoltage (V)','publication-dark-count-overvoltage')]:
 f,a=fig();a.plot(values,y,'o-',color='#234f72',lw=1.5,ms=4)
 a.set_yscale('symlog',linthresh=1,linscale=1)
 a.set(ylim=(-.15,1e6),xlabel=xlabel,ylabel='Dark-condition count rate (s⁻¹)',title='Dark counts at 57 °C and 1809.4 mV threshold')
 save(f,name)
print('Created four individual figures, PNG and SVG.')
