// Local-only texture inspection; output contains retail art, never commit it.
const fs = require('fs'), zlib = require('zlib');
const [file, out, ...names] = process.argv.slice(2);
const d = fs.readFileSync(file);
function crc(b) { let c = -1; for (const v of b) { c ^= v; for (let i=0;i<8;i++) c=(c>>>1)^((c&1)?0xedb88320:0); } return (c^-1)>>>0; }
function chunk(n,b) { const t=Buffer.from(n), h=Buffer.alloc(4), c=Buffer.alloc(4); h.writeUInt32BE(b.length); c.writeUInt32BE(crc(Buffer.concat([t,b]))); return Buffer.concat([h,t,b,c]); }
const rgb = c => [((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31];
for (const name of names) {
 const i=d.indexOf(Buffer.from(name+'\0')); if(i<0) continue;
 const w=d.readUInt16LE(i+56), h=d.readUInt16LE(i+58), fmt=d[i+62];
 let p=d.indexOf(Buffer.from([2,0,0x32,0x33]),i)+8; while(d[p]===17) p++;
 const off=d.readUInt32LE(i+36), pal=d.readUInt32LE(i+40), ps=d.readUInt32LE(i+48);
 const pixels=Buffer.alloc(w*h*4);
 if(ps>=1024) { for(let n=0;n<w*h;n++) d.copy(pixels,n*4,p+pal+d[p+off+n]*4,p+pal+d[p+off+n]*4+4); }
 else { let b=p+off; const bc=fmt===36?16:8;
 for(let y=0;y<h;y+=4) for(let x=0;x<w;x+=4,b+=bc) {
 const q=b+(bc===16?8:0), c0=d.readUInt16LE(q),c1=d.readUInt16LE(q+2),a=rgb(c0),v=rgb(c1);
 const colors=[a,v,a.map((n,k)=>(2*n+v[k])/3),a.map((n,k)=>(n+2*v[k])/3)];
 if(bc===8&&c0<=c1) { colors[2]=a.map((n,k)=>(n+v[k])/2); colors[3]=[0,0,0]; }
 const bits=d.readUInt32LE(q+4);
 for(let yy=0;yy<4;yy++) for(let xx=0;xx<4;xx++) { if(x+xx>=w||y+yy>=h) continue; const n=yy*4+xx,k=(bits>>>(n*2))&3,o=((y+yy)*w+x+xx)*4;
 colors[k].forEach((c,j)=>pixels[o+j]=Math.round(c)); pixels[o+3]=bc===16?((d[b+(n>>1)]>>>((n&1)*4))&15)*17:(c0<=c1&&k===3?0:255); }
 }}
 const head=Buffer.alloc(13); head.writeUInt32BE(w);head.writeUInt32BE(h,4);head[8]=8;head[9]=6;
 const rows=Buffer.alloc(h*(w*4+1));for(let y=0;y<h;y++) pixels.copy(rows,y*(w*4+1)+1,y*w*4,(y+1)*w*4);
 fs.mkdirSync(out,{recursive:true});fs.writeFileSync(out+'/'+name+'.png',Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',head),chunk('IDAT',zlib.deflateSync(rows)),chunk('IEND',Buffer.alloc(0))]));
 console.log(name,w,h,'hash='+d.readUInt32LE(i+24).toString(16));
}
