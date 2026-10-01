# 예시 엄폐물 맵 생성기 (4비트 16색 BMP, 1500x1500, 1픽셀 = 1m x 1m)
#   검정 = 벽(이동·총알 차단), 회색 = 낮은 엄폐물(이동만 차단), 흰색 = 빈 곳
#   빨강 계열 = 파괴 가능한 엄폐물 (R 150~255, G·B 40 → 체력 10~100, game-spec 3.3)
#   기본 배치는 예전과 같은 시드로 만들고(예전 스폰 49곳 주변 반경 16m 비움 포함), 그 위에 파괴 가능한 엄폐물을 더한다.
# 사용: python generate_example_map.py [out=obstacles.bmp] [seed=20260930] [destructibles=3000]
import sys, os, random, struct
import numpy as np

N = 1500      # 월드 크기 (m) = 섹터 50m x 30
SECTOR = 50
EMPTY, LOW, WALL, DEST = 0, 1, 2, 3
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "obstacles.bmp")
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260930
DEST_TARGET = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
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

# 예전 고정 스폰 49곳(섹터 2,6,...,26의 7x7) 주변 비우기 (기본 배치를 예전 맵과 같게 유지)
for sy in range(2, 30, 4):
    for sx in range(2, 30, 4):
        cx, cz = int((sx + 0.5) * SECTOR), int((sy + 0.5) * SECTOR)
        r = 16
        zz, xx = np.ogrid[-r:r + 1, -r:r + 1]
        mask = xx * xx + zz * zz <= r * r
        sub = grid[cz - r:cz + r + 1, cx - r:cx + r + 1]
        sub[mask] = 0

# 파괴 가능한 엄폐물: level 1~10 (체력 = level x 10). 약 1/4은 기존 벽 일부를 바꾸고, 나머지는 빈 곳에 상자 묶음으로 둔다.
#  - 같은 level로 상하좌우가 이어진 칸 묶음 = 엄폐물 1개 (서버·클라 동일 규칙) → 새 상자는 주변 2칸을 비워 서로 붙지 않게 한다.
drng = random.Random(seed + 1)
level = np.zeros((N, N), dtype=np.uint8)
from scipy import ndimage

def count_objects():
    n = 0
    for lv in range(1, 11):
        n += ndimage.label(level == lv)[1]
    return n

def free(x, z, w, h, margin):
    x0, z0, x1, z1 = x - margin, z - margin, x + w + margin, z + h + margin
    if x0 < 4 or z0 < 4 or x1 > N - 4 or z1 > N - 4: return False
    return not grid[z0:z1, x0:x1].any()

def place(x, z, w, h):
    grid[z:z + h, x:x + w] = DEST
    level[z:z + h, x:x + w] = drng.randint(1, 10)

# 1) 기존 벽 일부(길이 2~4)를 파괴 가능으로
wall_target = DEST_TARGET // 4
tries = 0
converted = 0
while converted < wall_target and tries < wall_target * 200:
    tries += 1
    x, z = drng.randint(4, N - 8), drng.randint(4, N - 8)
    if grid[z, x] != WALL: continue
    horiz = grid[z, x + 1] == WALL and grid[z, x - 1] == WALL
    vert = grid[z + 1, x] == WALL and grid[z - 1, x] == WALL
    if not (horiz or vert): continue
    L = drng.randint(2, 4)
    if horiz:
        # 두께 2짜리 벽이면 두 줄 모두 바꾼다
        th = 2 if (grid[z + 1, x] == WALL and grid[z + 1, x + L - 1] == WALL) else 1
        seg = grid[z:z + th, x:x + L]
        if seg.shape != (th, L) or (seg != WALL).any(): continue
        if (level[z - 1:z + th + 1, x - 1:x + L + 1] > 0).any(): continue
        place(x, z, L, th)
    else:
        th = 2 if (grid[z, x + 1] == WALL and grid[z + L - 1, x + 1] == WALL) else 1
        seg = grid[z:z + L, x:x + th]
        if seg.shape != (L, th) or (seg != WALL).any(): continue
        if (level[z - 1:z + L + 1, x - 1:x + th + 1] > 0).any(): continue
        place(x, z, th, L)
    converted += 1

# 2) 빈 곳에 상자 묶음 (1~4개, 크기 1x1 ~ 3x2)
sizes = [(1, 1), (2, 1), (1, 2), (2, 2), (2, 2), (3, 1), (1, 3), (3, 2), (2, 3)]
crates_target = DEST_TARGET - converted
placed = 0
tries = 0
while placed < crates_target and tries < crates_target * 100:
    tries += 1
    cx, cz = drng.randint(8, N - 9), drng.randint(8, N - 9)
    for _ in range(drng.randint(1, 4)):
        if placed >= crates_target: break
        w, h = drng.choice(sizes)
        x, z = cx + drng.randint(-8, 8), cz + drng.randint(-8, 8)
        if free(x, z, w, h, 2):
            place(x, z, w, h)
            placed += 1

objects = count_objects()

# 4비트 BMP 저장 (팔레트: 0 검정 = 벽, 1 회색 = 낮은 엄폐물, 2 흰색 = 빈 곳, 3~12 빨강 R 150~255 = 파괴 가능 level 1~10)
#  그림판에서 편집 후 저장할 때는 '24비트 비트맵'으로 저장할 것 (16색으로 저장하면 기본 팔레트로 바뀌어 빨강 단계가 사라짐)
DEST_R = [150, 162, 174, 186, 198, 210, 222, 234, 246, 255]
pal = [(0, 0, 0), (128, 128, 128), (255, 255, 255)] + [(r, 40, 40) for r in DEST_R] + [(255, 255, 255)] * 3
idx = np.full((N, N), 2, dtype=np.uint8)
idx[grid == WALL] = 0
idx[grid == LOW] = 1
d = grid == DEST
idx[d] = 2 + level[d]
rows = idx[::1]                                   # bottom-up: 파일의 첫 행 = z 0 (맵 남쪽)
packed = ((rows[:, 0::2] << 4) | rows[:, 1::2]).astype(np.uint8)   # N/2 bytes/row
stride = ((N * 4 + 31) // 32) * 4                 # 행은 4바이트 정렬 (1500 → 752)
if stride > packed.shape[1]:
    packed = np.hstack([packed, np.full((N, stride - packed.shape[1]), 0x22, dtype=np.uint8)])
offbits = 14 + 40 + 16 * 4
with open(out, "wb") as f:
    f.write(b"BM" + struct.pack("<IHHI", offbits + stride * N, 0, 0, offbits))
    f.write(struct.pack("<IiiHHIIiiII", 40, N, N, 1, 4, 0, stride * N, 2835, 2835, 16, 16))
    for r, g, b in pal: f.write(struct.pack("<BBBB", b, g, r, 0))
    f.write(packed.tobytes())
print(f"{out}: wall {(grid == WALL).sum()} cells, low {(grid == LOW).sum()} cells, destructible {(grid == DEST).sum()} cells / {objects} objects (wall-converted {converted}, crates {placed})")
