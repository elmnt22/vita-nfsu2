"""Loading-screen controller art: img/loading_controller.png (256x256, any
PNG mode, transparent background) -> vita/assets/loading_controller.raw
(256x256 straight-alpha RGBA, read by shop.c lsc_device_file) and the
simulator's copy. Run by vita/build_vita.sh; does nothing without the PNG.
  python tools/lsc_png.py [png] [out.raw ...]"""
import os, sys
from PIL import Image

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, '..', 'img', 'loading_controller.png')
outs = sys.argv[2:] or [os.path.join(root, 'vita', 'assets', 'loading_controller.raw'),
                        os.path.join(root, '..', 'simroot', 'app0', 'assets', 'loading_controller.raw')]
if not os.path.exists(src):
    print('lsc_png: no', src, '- the drawn controller is used')
    sys.exit(0)
im = Image.open(src).convert('RGBA')
if im.size != (256, 256):
    print('lsc_png: %s is %dx%d, scaled to 256x256' % (src, im.size[0], im.size[1]))
    im = im.resize((256, 256), Image.LANCZOS)
data = im.tobytes()
for o in outs:
    os.makedirs(os.path.dirname(o), exist_ok=True)
    with open(o, 'wb') as f:
        f.write(data)
    print('lsc_png:', o)
