from pathlib import Path
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch,FancyArrowPatch

out=Path(__file__).resolve().parents[2]/'build/figures'
out.mkdir(parents=True,exist_ok=True)
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'svg.fonttype':'none'})
f,a=plt.subplots(figsize=(14,6),layout='constrained');a.set(xlim=(0,14),ylim=(0,6));a.axis('off')
a.text(.15,5.75,'Receiver counter architecture',fontsize=20,fontweight='bold',va='top')
def box(x,y,w,h,title,body,color='#edf3f8'):
 a.add_patch(FancyBboxPatch((x,y),w,h,boxstyle='round,pad=0.025,rounding_size=0.07',fc=color,ec='#52677a',lw=1.1))
 a.text(x+w/2,y+h-.22,title,ha='center',va='top',fontsize=12,fontweight='bold',color='#172b3a')
 a.text(x+w/2,y+.18,body,ha='center',va='bottom',fontsize=10.5,linespacing=1.5,color='#172b3a')
def arrow(p,q,label=None):
 a.add_patch(FancyArrowPatch(p,q,arrowstyle='-|>',mutation_scale=13,lw=1.3,color='#52677a'))
 if label:a.text((p[0]+q[0])/2,(p[1]+q[1])/2+.12,label,ha='center',va='bottom',fontsize=9,bbox=dict(fc='white',ec='none',pad=1))
box(.2,3.55,2.2,1.45,'Receiver pulse','Comparator → LVDS receiver\nGPIO5 rising edges')
box(3.05,3.55,2.3,1.45,'PWM2 counter','16-bit free-running CTR\nclkdiv mode: B_RISING\nWrap: 65,535 → 0')
box(6.05,3.55,2.2,1.45,'RX DMA','32-bit CTR reads\nFixed source address\nIncrementing destination')
box(8.95,3.55,2.15,1.45,'SRAM snapshots','C[0], C[1], …, C[M]\nM = 1023 × frames\nM + 1 samples / burst')
box(11.7,3.55,2.1,1.45,'Bin reconstruction','ΔC[k] = (C[k+1] − C[k])\nmodulo 65,536\nFold into 1023-bin histogram')
for p,q in [((2.4,4.25),(3.05,4.25)),((5.35,4.25),(6.05,4.25)),((8.25,4.25),(8.95,4.25)),((11.1,4.25),(11.7,4.25))]:arrow(p,q)
box(6.05,1.1,2.2,1.45,'PWM0 bin timer','clk_sys = 500 MHz\nDivider = 1, TOP = 49\nDREQ = 10 MHz',color='#f0edf8')
arrow((7.15,2.55),(7.15,3.55),'one read per wrap')
box(.2,1.1,5.15,1.45,'Independent live rate (outside acquisition)','PWM2 sampled locally over a 50 ms window\nAccumulate modulo differences; report elapsed time and max gap\ncps = total edges / elapsed seconds',color='#edf5ee')
arrow((4.2,3.55),(4.2,2.55))
a.text(8.95,2.1,'100 ns nominal bins\n16-frame burst: 16,369 snapshots\nNominal sampled duration: 1.6368 ms',va='center',fontsize=11,linespacing=1.6,color='#172b3a')
a.text(.2,.65,'Constraint: fewer than 65,536 edges between snapshots.  DMA completion stops the bin timer; PWM2 keeps counting.',fontsize=10.5,color='#172b3a')
a.text(.2,.28,'Pulse counting is continuous; binned acquisition occurs in finite bursts.  DMA / bus latency can shift individual snapshot timing.',fontsize=10.5,color='#172b3a')
for ext in ('png','svg'):f.savefig(out/f'receiver-counter-architecture.{ext}',dpi=300,bbox_inches='tight',facecolor='white')
