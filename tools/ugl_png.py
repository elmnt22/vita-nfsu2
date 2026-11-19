"""Loading_Controller logo art: img/uga_load_logo.png (512x64, any PNG mode,
transparent background; drawn 384x48 in place of the game's UG_load_logo.tga)
-> vita/assets/ug_load_logo.raw (512x64 straight-alpha RGBA, read by shop.c
ls_logo_tex) and the simulator's copy. Run by vita/build_vita.sh; does
nothing without the PNG (the game's own logo is drawn).
  python tools/ugl_png.py [png] [out.raw ...]"""
import os, sys
from PIL import Image

W, H = 512, 64
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, '..', 'img', 'uga_load_logo.png')
outs = sys.argv[2:] or [os.path.join(root, 'vita', 'assets', 'ug_load_logo.raw'),
                        os.path.join(root, '..', 'simroot', 'app0', 'assets', 'ug_load_logo.raw')]
if not os.path.exists(src):
    print('ugl_png: no', src, '- the game logo is used')
    sys.exit(0)
im = Image.open(src).convert('RGBA')
if im.size != (W, H):
    print('ugl_png: %s is %dx%d, scaled to %dx%d' % (src, im.size[0], im.size[1], W, H))
    im = im.resize((W, H), Image.LANCZOS)
data = im.tobytes()
for o in outs:
    os.makedirs(os.path.dirname(o), exist_ok=True)
    with open(o, 'wb') as f:
        f.write(data)
    print('ugl_png:', o)
