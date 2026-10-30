"""Bit-exact check of t_movie.exe --raw / --pcm against ffmpeg (PC reference only).
usage: python movie_cmp.py t_movie.exe file.vp6"""
import os, subprocess, sys, struct

exe, mov = os.path.abspath(sys.argv[1]), sys.argv[2]
probe = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                        'stream=width,height', '-of', 'csv=p=0', mov], capture_output=True, text=True).stdout
w, h = map(int, probe.strip().split(','))
fs = w * h * 3 // 2
ff = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', mov, '-map', '0:v', '-f', 'rawvideo',
                       '-pix_fmt', 'yuv420p', '-'], stdout=subprocess.PIPE)
me = subprocess.Popen([exe, mov, '--raw'], stdout=subprocess.PIPE)
n = bad = 0
while True:
    a = ff.stdout.read(fs); b = me.stdout.read(fs)
    if len(a) < fs or len(b) < fs:
        if len(a) != len(b): print('frame count differs at', n, len(a), len(b))
        break
    if a != b:
        diff = sum(1 for i in range(0, fs, 7) if a[i] != b[i])
        if bad < 10: print('frame %d differs (~%d sampled px)' % (n, diff))
        bad += 1
    n += 1
print('video: %d frames compared, %d differ' % (n, bad))

af = subprocess.run(['ffmpeg', '-v', 'error', '-i', mov, '-map', '0:a?', '-f', 's16le', '-'], capture_output=True).stdout
am = subprocess.run([exe, mov, '--pcm'], capture_output=True).stdout
if af or am:
    k = min(len(af), len(am)) // 2
    A = struct.unpack('<%dh' % k, af[:k*2]); B = struct.unpack('<%dh' % k, am[:k*2])
    d = [abs(x - y) for x, y in zip(A, B)]
    print('audio: ffmpeg %d samples, ours %d, max diff %d, mean diff %.3f, exact %.1f%%' %
          (len(af)//2, len(am)//2, max(d), sum(d)/len(d), 100.0*sum(1 for x in d if x == 0)/len(d)))
