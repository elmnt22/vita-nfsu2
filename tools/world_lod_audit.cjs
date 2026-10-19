// Raw authored LOD inventory. Counts source buffers, not validated/rendered
// triangles; runtime parsing remains the authority for usable geometry.
const fs = require('fs');
const d = fs.readFileSync(process.argv[2]);
const models = new Map(), sections = [];
function chunks(a,b,visit) {
  for(let p=a;p+8<=b;) {
    const id=d.readUInt32LE(p), size=d.readUInt32LE(p+4), q=p+8, e=q+size;
    if(e>b) throw Error(`Invalid chunk at ${p}`);
    visit(id,q,e); p=e;
  }
}
function walk(a,b,visit) {chunks(a,b,(id,q,e)=>{visit(id,q,e); if((id>>>28)===8)walk(q,e,visit);});}
function filler(a,b) {while(a<b&&d[a]===0x11)a++; return a;}
walk(0,d.length,(id,a,b)=>{
  if(id===0x80034100)sections.push([a,b]);
  if(id!==0x80134010)return;
  let key=0,name='',verts=0,tris=0;
  walk(a,b,(leaf,q,e)=>{
    const s=filler(q,e);
    if(leaf===0x134011&&e-s>0xa4){key=d.readUInt32LE(s+0x10); const z=d.indexOf(0,s+0xa4); if(z>=0&&z<e)name=d.toString('ascii',s+0xa4,z);}
    if(leaf===0x134b01)verts+=(e-s)/24;
    if(leaf===0x134b03){let p=q;while(p+2<=e&&d.readUInt16LE(p)===0x1111)p+=2;tris+=Math.floor((e-p)/6);}
  });
  if(key&&!models.has(key))models.set(key,{key,name,verts,tris});
});
const rows=[]; const counts={placements:0,buildings:0,alternatives:0,buildingAlternatives:0,baseTriangles:0,lowestTriangles:0};
for(const [a,b] of sections){
  let types,placements;
  chunks(a,b,(id,q,e)=>{if(id===0x34102)types=[q,e];if(id===0x34103)placements=[q,e];});
  if(!types||!placements)continue;
  const ta=(types[1]-types[0])%68?filler(...types):types[0];
  const pa=(placements[1]-placements[0])%64?filler(...placements):placements[0];
  for(let p=pa;p+64<=placements[1];p+=64){
    const t=ta+d.readUInt16LE(p+0x18)*68;if(t+68>types[1])continue;
    const options=[0,1,2].map(i=>models.get(d.readUInt32LE(t+0x20+i*4))).filter(Boolean);
    if(!options.length)continue;
    const base=options[0], low=options.reduce((a,b)=>b.tris<a.tris?b:a);
    counts.placements++;counts.baseTriangles+=base.tris;counts.lowestTriangles+=low.tris;
    const building=base.name.startsWith('XB');counts.buildings+=building;
    if(low.tris<base.tris){counts.alternatives++;counts.buildingAlternatives+=building;
      rows.push({base:base.name,low:low.name,baseTris:base.tris,lowTris:low.tris,x:d.readFloatLE(p),y:d.readFloatLE(p+4)});}
  }
}
console.log(JSON.stringify({modelCount:models.size,counts,examples:rows.slice(0,20)},null,2));
