import math, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
W,H,K=240,196,4
w,h=W*K,H*K
img=Image.new('RGBA',(w,h),(0,0,0,0))
# body: vertical gradient inside a rounded rect
grad=Image.new('RGBA',(w,h))
gd=ImageDraw.Draw(grad)
for y in range(h):
    t=y/(h-1)
    c=(int(46+(14-46)*t),int(54+(18-54)*t),int(66+(24-66)*t),255)
    gd.line([(0,y),(w,y)],fill=c)
mask=Image.new('L',(w,h),0)
md=ImageDraw.Draw(mask)
r=26*K; inset=3*K
md.rounded_rectangle([inset,inset,w-inset-1,h-inset-1],radius=r,fill=255)
img.paste(grad,(0,0),mask)
d=ImageDraw.Draw(img)
# glossy top highlight
gl=Image.new('RGBA',(w,h),(0,0,0,0)); gld=ImageDraw.Draw(gl)
gld.rounded_rectangle([inset+2*K,inset+2*K,w-inset-2*K,int(h*0.46)],radius=r-2*K,fill=(255,255,255,26))
img=Image.alpha_composite(img,Image.composite(gl,Image.new('RGBA',(w,h),(0,0,0,0)),mask))
d=ImageDraw.Draw(img)
# borders: dark outer, NFSU2 green inner glow line
d.rounded_rectangle([inset,inset,w-inset-1,h-inset-1],radius=r,outline=(8,10,14,255),width=2*K)
d.rounded_rectangle([inset+2*K,inset+2*K,w-inset-1-2*K,h-inset-1-2*K],radius=r-2*K,outline=(120,220,40,255),width=int(2.5*K))
# gear
cx,cy=w//2,int(h*0.40); R=40*K; Ri=31*K; teeth=8
pts=[]
for i in range(teeth*4):
    a=(i/(teeth*4))*2*math.pi - math.pi/2
    rr=R if (i%4) in (1,2) else Ri
    pts.append((cx+rr*math.cos(a),cy+rr*math.sin(a)))
# shadow
sh=Image.new('RGBA',(w,h),(0,0,0,0)); sd=ImageDraw.Draw(sh)
sd.polygon([(x+2*K,y+3*K) for x,y in pts],fill=(0,0,0,150))
sh=sh.filter(ImageFilter.GaussianBlur(3*K))
img=Image.alpha_composite(img,sh); d=ImageDraw.Draw(img)
d.polygon(pts,fill=(232,236,240,255))
d.ellipse([cx-Ri+1,cy-Ri+1,cx+Ri-1,cy+Ri-1],fill=(232,236,240,255))
hole=14*K
d.ellipse([cx-hole,cy-hole,cx+hole,cy+hole],fill=(30,36,44,255))
# label
font=ImageFont.truetype(r'C:/Windows/Fonts/segoeuib.ttf',32*K)
t='Settings'
bb=d.textbbox((0,0),t,font=font)
tx=(w-(bb[2]-bb[0]))//2-bb[0]; ty=int(h*0.70)-bb[1]
d.text((tx+K*2,ty+K*2),t,font=font,fill=(0,0,0,160))
d.text((tx,ty),t,font=font,fill=(255,255,255,255))
out=img.resize((W,H),Image.LANCZOS)
out.save(sys.argv[1])
print(out.size,out.mode)
