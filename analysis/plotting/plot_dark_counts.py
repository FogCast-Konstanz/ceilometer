import csv
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

root=Path(__file__).resolve().parents[2]
out=Path(__file__).resolve().parents[2]/'build/figures'
out.mkdir(parents=True,exist_ok=True)
rows=list(csv.DictReader((root/'analysis/bench-tests/legacy/dark-overvoltage-sweep.csv').open(encoding='utf-8-sig')))
groups={t:sorted((int(r['ov_mv'])/1000,float(r['rate_cps'])/1e6) for r in rows if int(r['threshold_mv'])==t) for t in sorted({int(r['threshold_mv']) for r in rows})}
plt.rcParams.update({'font.size':11,'axes.spines.top':False,'axes.spines.right':False,'svg.fonttype':'none'})
fig,(ax,bx)=plt.subplots(1,2,figsize=(14,6.8),layout='constrained')
fig.suptitle('Receiver dark-condition counts versus SiPM overvoltage',fontsize=18,fontweight='bold')
colors=plt.cm.viridis(np.linspace(.05,.95,len(groups)))
for (t,p),color in zip(groups.items(),colors):
 x,y=np.array(p).T
 if not np.any(y):continue
 ax.plot(x,np.where(y>0,y,np.nan),'o-',color=color,label=f'{t} mV',lw=1.5,ms=4)
ax.plot([5,10],[.75,.75+5/12],'--',color='#555555',lw=2,label='Datasheet, 25 °C (approx.)')
ax.set(yscale='log',xlabel='Overvoltage above breakdown (V)',ylabel='Measured crossing rate (Mcps)',title='Threshold sweep · logarithmic rate axis',xticks=range(5,11))
ax.grid(True,which='major',alpha=.22);ax.legend(ncol=2,fontsize=9,loc='lower right')
ax.text(.02,.97,'Receiver: 55.625–55.750 °C\nZero-count points omitted from log axis\n1798 and 1812 mV: zero throughout',transform=ax.transAxes,va='top',fontsize=9,bbox=dict(facecolor='white',edgecolor='none',alpha=.85))
for t,color in [(1806,'#1769aa'),(1807,'#d55e00')]:
 x,y=np.array(groups[t]).T
 slope,intercept=np.polyfit(x,y,1);pred=slope*x+intercept
 r2=1-sum((y-pred)**2)/sum((y-y.mean())**2)
 bx.plot(x,pred,'--',color=color,lw=1.6)
 bx.errorbar(x,y,yerr=np.sqrt(y*1e6*.016368)/.016368/1e6,fmt='o',color=color,capsize=3,label=f'{t} mV: R² = {r2:.4f}, slope = {slope:.3f} Mcps/V')
bx.axhline(3.282197,color='#d55e00',alpha=.6,ls=':',label='1807 mV bias-off background: 3.28 Mcps')
bx.axhline(0,color='#1769aa',alpha=.6,ls=':',label='1806 mV bias-off background: zero observed')
bx.set(xlabel='Overvoltage above breakdown (V)',ylabel='Measured crossing rate (Mcps)',title='Most linear thresholds · raw counts, no background subtraction',xticks=range(5,11),ylim=(-1,51))
bx.grid(alpha=.22);bx.legend(fontsize=9,loc='upper left')
fig.supxlabel('160 frames per point · 16.368 ms sampled duration · laser off · bias targets 43.65–48.65 V\nTemperature differs from datasheet; crossings are not yet qualified as single-photoelectron DCR. Error bars: counting statistics only.',fontsize=10)
for ext in ('png','svg'):
 fig.savefig(out/f'dark-overvoltage-sweep.{ext}',dpi=180)
print(out/'dark-overvoltage-sweep.png')
