// Retail asset audit: compare the previous name scan with authored table rows.
// Usage: node tools/world_texture_inventory_audit.cjs DATAROOT
const fs=require('fs');
const root=process.argv[2];
if(!root)throw Error('Usage: world_texture_inventory_audit.cjs DATAROOT');
for(const f of ['GLOBAL/GlobalTexturesNA.bin','GLOBAL/GlobalTexturesROW.bin','GLOBAL/InGameCommon.bun','TRACKS/STREAML4RA.BUN','TRACKS/STREAML4RD.BUN']){
 const d=fs.readFileSync(root+'/'+f),old=new Set(),actual=new Map();let ph=-1,hs=0;
 for(let i=0;i+8<d.length;i++){
  const v=d.readUInt32LE(i);if(v===0xb3310000){ph=i+8;hs=d.readUInt32LE(i+4);}
  else if(ph>=0&&v===0x33320002){const end=ph+hs;
   for(let p=ph;p+64<end;p++){if(d[p]<65||d[p]>90||d[p+23]!==0)continue;old.add(d.readUInt32LE(p+24));p+=123;}
   for(let p=ph;p+8<=end;){let id=d.readUInt32LE(p),sz=d.readUInt32LE(p+4),q=p+8;if(q+sz>end)break;const e=q+sz;if(id===0x33310004){while(q<e&&d[q]===17)q++;for(;q+124<=e;q+=124)actual.set(d.readUInt32LE(q+36),d.toString('ascii',q+12,q+36).split('\0')[0]);break;}p=e;}
   ph=-1;
  }
 }
 const missed=[...actual].filter(([k])=>!old.has(k)); console.log(JSON.stringify({file:f,actualUnique:actual.size,oldUnique:old.size,missing:missed.length,samples:missed.slice(0,8).map(([key,name])=>({key:key.toString(16),name}))}));
 if(/GlobalTextures/.test(f)&&(!actual.has(0xde510e5f)||old.has(0xde510e5f)))throw Error('Expected Best Buy inventory regression not reproduced');
}

