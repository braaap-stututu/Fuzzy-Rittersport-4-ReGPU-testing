# Joins REGPU_SHADER_STATS lines (log) with a drawstats JSON: the vertex shaders by index share, with RADV statistics.
# python3 vsjoin.py fm2.log drawstats.json
import json,re,sys,collections
log,ds=sys.argv[1],sys.argv[2]
st={}
for line in open(log,errors='replace'):
    m=re.search(r'shader stats: ([0-9a-f]{16})/([0-9a-f]{16}) \[vertex\] (.*)',line)
    if not m: continue
    d=dict(kv.split('=',1) for kv in m.group(3).split(', '))
    st.setdefault(m.group(1),{})[m.group(2)]=d
pairs=json.load(open(ds))['pairs']
tot=sum(p['indices'] for p in pairs)
byvs=collections.defaultdict(lambda:[0,0,set()])
for p in pairs:
    b=byvs[p['vs']]; b[0]+=p['indices']; b[1]+=p['draws']; b[2].add(p['ps'])
keys=['VGPRs','SGPRs','Spilled VGPRs','Scratch size','Subgroups per SIMD','Instructions','VMEM','SMEM','VALU','Latency','Inverse Throughput','Branches','Combined outputs']
print('vs               idx%  draws '+' '.join(k.replace(' ','')[:9].rjust(9) for k in keys))
acc=0;miss=0
for vs,(n,dr,pss) in sorted(byvs.items(),key=lambda x:-x[1][0])[:25]:
    s=st.get(vs,{})
    d=next((s[p] for p in pss if p in s), next(iter(s.values()),None))
    acc+=n
    row=' '.join((d.get(k,'?') if d else '-').rjust(9) for k in keys)
    print(f'{vs} {100*n/tot:5.1f} {dr:5d} {row}')
# weighted means
w=collections.Counter();wn=0
for vs,(n,dr,pss) in byvs.items():
    s=st.get(vs)
    if not s: miss+=n; continue
    d=next(iter(s.values()))
    for k in ['VGPRs','Instructions','VMEM','Latency','Inverse Throughput','Subgroups per SIMD']: w[k]+=n*float(d[k])
    wn+=n
print('index-weighted:',{k:round(v/wn,1) for k,v in w.items()},'no stats for %.1f%% of indices'%(100*miss/tot))
