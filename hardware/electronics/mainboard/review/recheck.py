import contextlib,io,collections,math
with contextlib.redirect_stdout(io.StringIO()):
 import inspect_design as d
def padnets(board):
 result={}
 for fp in d.children(board,'footprint'):
  ref=next((p[2] for p in d.children(fp,'property') if p[1]=='Reference'),'')
  for pad in d.children(fp,'pad'):
   net=d.one(pad,'net')
   if pad[1] and net: result[(ref,pad[1])]=net[-1]
 return result
main=padnets(d.b)
for suffix in ['A','B']:
 path=d.P/f'Ceilometer-Mainboard-{suffix}.kicad_pcb'
 if not path.exists():
  print(suffix,'board no longer present; previous check is historical')
  continue
 board=d.parse(path)
 nets=padnets(board)
 diff=[(key,net,main.get(key)) for key,net in nets.items() if key in main and net!=main[key]]
 print(suffix,'pad-net differences from latest source:',diff)
for net in ['Net-(U3-AGND1)','Net-(D1-A)','Net-(D1-K)','Net-(U3-VDD)','/Receiver/PULSE2+','/Receiver/PULSE2-']:
 segments=[s for s in d.children(d.b,'segment') if d.one(s,'net',['',''])[-1]==net]
 if segments:
  widths=sorted(set(float(d.one(s,'width')[1]) for s in segments))
  length=sum(math.dist(list(map(float,d.one(s,'start')[1:])),list(map(float,d.one(s,'end')[1:]))) for s in segments)
  print(net,'widths mm',widths,'total track length mm',round(length,3))
print('Zone counts by layer:')
for z in d.children(d.b,'zone'):
 print(d.one(z,'net'),d.one(z,'layers',d.one(z,'layer')),len(d.children(z,'filled_polygon')))
