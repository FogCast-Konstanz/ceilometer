import re,json,pathlib,collections
P=pathlib.Path(__file__).resolve().parent.parent
def parse(path):
 t=re.findall(r'"(?:\\.|[^"\\])*"|[^\s()]+|[()]',path.read_text(encoding='utf-8')); stack=[]; root=[]; cur=root
 for x in t:
  if x=='(': a=[];cur.append(a);stack.append(cur);cur=a
  elif x==')':cur=stack.pop()
  else:cur.append(json.loads(x) if x.startswith('"') else x)
 return root[0]
def children(a,k):return [x for x in a if isinstance(x,list) and x and x[0]==k]
def one(a,k,d=None):return next(iter(children(a,k)),d)
n=parse(P/'review/netlist.xml'); comps={one(c,'ref')[1]:c for c in one(n,'components')[1:]}; nets=one(n,'nets')[1:]; cn=collections.defaultdict(list)
for net in nets:
 for nd in children(net,'node'):cn[one(nd,'ref')[1]].append((one(nd,'pin')[1],one(nd,'pinfunction',['',''])[1],one(net,'name')[1]))
with (P/'review/connectivity.txt').open('w',encoding='utf-8') as f:
 for ref,c in comps.items():
  f.write(f'{ref} {one(c,"value")[1]} {one(c,"footprint",["",""])[1]}\n')
  for pin,fn,net in cn[ref]:f.write(f'  {pin} {fn}: {net}\n')
b=parse(P/'Ceilometer-Mainboard.kicad_pcb'); out=[]
for fp in children(b,'footprint'):
 props={x[1]:x[2] for x in children(fp,'property')}; ref=props.get('Reference',''); pos=one(fp,'at'); pads=[]
 for p in children(fp,'pad'):pads.append({'pin':p[1],'at':one(p,'at'),'net':one(p,'net'),'size':one(p,'size')})
 out.append({'ref':ref,'value':props.get('Value'),'footprint':fp[1],'at':pos,'pads':pads})
(P/'review/footprints.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print((P/'review/connectivity.txt').read_text())

