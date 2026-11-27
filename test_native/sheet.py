import sys, zlib, struct, os, glob
d = sys.argv[1]; out = sys.argv[2]
faces = sorted(glob.glob(os.path.join(d, "face[0-9]*.ppm")),
               key=lambda p: int(os.path.basename(p)[4:-4]))
names = [os.path.basename(f) for f in faces] + ["face_setup.ppm", "face_update.ppm"]
def ppm(p):
    b = open(p, 'rb').read(); return b.split(b'\n', 3)[3]
G = 16; cols = 5; N = len(names); rows = (N + cols - 1) // cols
W = cols * 240 + (cols + 1) * G; H = rows * 240 + (rows + 1) * G
img = bytearray([24, 26, 31] * W * H)
for k, n in enumerate(names):
    px = ppm(os.path.join(d, n)); ox = G + (k % cols) * (240 + G); oy = G + (k // cols) * (240 + G)
    for y in range(240):
        for x in range(240):
            dx, dy = x - 119.5, y - 119.5
            if dx*dx + dy*dy > 120*120: continue
            i = (y * 240 + x) * 3; j = ((oy + y) * W + ox + x) * 3
            img[j:j+3] = px[i:i+3]
raw = b''.join(b'\x00' + bytes(img[y*W*3:(y+1)*W*3]) for y in range(H))
def chunk(t, data): c = struct.pack('>I', len(data)) + t + data; return c + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
open(out, 'wb').write(png); print(out, W, H)
