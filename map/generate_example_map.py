# 예시 엄폐물 맵 생성기 (4비트 16색 BMP, 6400x6400, 1픽셀 = 1m x 1m)
#   검정 = 벽(이동·총알 차단), 회색 = 낮은 엄폐물(이동만 차단), 흰색 = 빈 곳
#   스폰 지점(server/GameServer/spawns.txt) 주변 반경 16m는 비워 둔다.
# 사용: python generate_example_map.py [out=obstacles.bmp] [seed=20260930]
import sys, os, random, struct
import numpy as np

N = 6400
SECTOR = 64
EMPTY, LOW, WALL = 0, 1, 2
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "obstacles.bmp")
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260930
rng = random.Random(seed)
grid = np.zeros((N, N), dtype=np.uint8)   # [z, x]

def rect(x, z, w, h, t):
    x0, z0, x1, z1 = max(0, x), max(0, z), min(N, x + w), min(N, z + h)
    if x0 < x1 and z0 < z1:
        grid[z0:z1, x0:x1] = t

def wall_seg(cx, cz, t):
    L = rng.randint(6, 22); th = rng.choice([1, 2]) if t == WALL else 1
    if rng.random() < 0.5: rect(cx - L // 2, cz, L, th, t)
    else: rect(cx, cz - L // 2, th, L, t)

def l_shape(cx, cz):
    a, b = rng.randint(6, 16), rng.randint(6, 16)
    sx, sz = rng.choice([-1, 1]), rng.choice([-1, 1])
    rect(cx if sx > 0 else cx - a, cz, a, 2, WALL)
    rect(cx, cz if sz > 0 else cz - b, 2, b, WALL)

def building(cx, cz):
    w, h = rng.randint(12, 26), rng.randint(12, 26)
    x0, z0 = cx - w // 2, cz - h // 2
    rect(x0, z0, w, 1, WALL); rect(x0, z0 + h - 1, w, 1, WALL)
    rect(x0, z0, 1, h, WALL); rect(x0 + w - 1, z0, 1, h, WALL)
    # 문 2~3개 (폭 3)
    for _ in range(rng.randint(2, 3)):
        side = rng.randint(0, 3)
        if side < 2:
            dx = rng.randint(2, w - 5); rect(x0 + dx, z0 if side == 0 else z0 + h - 1, 3, 1, EMPTY)
        else:
            dz = rng.randint(2, h - 5); rect(x0 if side == 2 else x0 + w - 1, z0 + dz, 1, 3, EMPTY)
    # 내부 낮은 엄폐물
    if rng.random() < 0.6:
        rect(cx - 1, cz - 1, 2, 2, LOW)

def crates(cx, cz):
    for _ in range(rng.randint(2, 5)):
        s = rng.randint(2, 4)
        rect(cx + rng.randint(-10, 10), cz + rng.randint(-10, 10), s, s, LOW)

def barricade(cx, cz):
    L = rng.randint(4, 12)
    if rng.random() < 0.5: rect(cx - L // 2, cz, L, 1, LOW)
    else: rect(cx, cz - L // 2, 1, L, LOW)

features = [(wall_seg, 0.26), (l_shape, 0.14), (building, 0.12), (crates, 0.22), (barricade, 0.26)]
def pick():
    r = rng.random(); acc = 0
    for f, p in features:
        acc += p
        if r < acc: return f
    return features[-1][0]

for sy in range(N // SECTOR):
    for sx in range(N // SECTOR):
        if rng.random() < 0.25: continue          # 빈 섹터
        for _ in range(rng.randint(1, 3)):
            cx = sx * SECTOR + rng.randint(10, SECTOR - 10)
            cz = sy * SECTOR + rng.randint(10, SECTOR - 10)
            f = pick()
            if f is wall_seg: f(cx, cz, WALL)
            else: f(cx, cz)

# 외벽 안쪽 2m (외벽과 겹치는 영역) 비움
grid[:3, :] = 0; grid[-3:, :] = 0; grid[:, :3] = 0; grid[:, -3:] = 0

# 스폰 주변 비우기
spawns = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "server", "GameServer", "spawns.txt")
if os.path.exists(spawns):
    for line in open(spawns, encoding="cp949", errors="ignore"):
        line = line.split("#")[0].split()
        if len(line) < 2: continue
        sx, sy = int(line[0]), int(line[1])
        cx, cz = int((sx + 0.5) * SECTOR), int((sy + 0.5) * SECTOR)
        r = 16
        zz, xx = np.ogrid[-r:r + 1, -r:r + 1]
        mask = xx * xx + zz * zz <= r * r
        sub = grid[cz - r:cz + r + 1, cx - r:cx + r + 1]
        sub[mask] = 0

# 4비트 BMP 저장 (VGA 16색 팔레트: 0 검정, 8 회색, 15 흰색)
pal = [(0,0,0),(128,0,0),(0,128,0),(128,128,0),(0,0,128),(128,0,128),(0,128,128),(192,192,192),
       (128,128,128),(255,0,0),(0,255,0),(255,255,0),(0,0,255),(255,0,255),(0,255,255),(255,255,255)]
idx = np.full((N, N), 15, dtype=np.uint8)
idx[grid == WALL] = 0
idx[grid == LOW] = 8
rows = idx[::1]                                   # bottom-up: 파일의 첫 행 = z 0 (맵 남쪽)
packed = ((rows[:, 0::2] << 4) | rows[:, 1::2]).astype(np.uint8)   # 3200 bytes/row (4바이트 정렬 OK)
stride = N // 2
offbits = 14 + 40 + 16 * 4
with open(out, "wb") as f:
    f.write(b"BM" + struct.pack("<IHHI", offbits + stride * N, 0, 0, offbits))
    f.write(struct.pack("<IiiHHIIiiII", 40, N, N, 1, 4, 0, stride * N, 2835, 2835, 16, 16))
    for r, g, b in pal: f.write(struct.pack("<BBBB", b, g, r, 0))
    f.write(packed.tobytes())
print(f"{out}: wall {(grid == WALL).sum()} cells, low {(grid == LOW).sum()} cells")
