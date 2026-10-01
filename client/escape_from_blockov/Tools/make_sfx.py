# 효과음 합성 (game-spec 12.4). 22050Hz mono 16bit WAV
import numpy as np, wave, sys, os
SR=22050
rng=np.random.default_rng(20261001)
def t(d): return np.arange(int(SR*d))/SR
def env(n, a=0.002, d=0.1):  # 빠른 어택 + 지수 감쇠
    x=np.arange(n)/SR; e=np.exp(-x/d); att=np.clip(x/a,0,1); return e*att
def lp(x, k):  # 단순 1차 저역 통과
    y=np.empty_like(x); acc=0.0; a=k
    for i,v in enumerate(x): acc+=a*(v-acc); y[i]=acc
    return y
def noise(n): return rng.uniform(-1,1,n)
def save(name, x, gain=0.9):
    x=x/ (np.max(np.abs(x))+1e-9)*gain
    pcm=(np.clip(x,-1,1)*32767).astype('<i2')
    with wave.open(name+'.wav','wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
    print(name, len(x)/SR)
out=sys.argv[1] if len(sys.argv)>1 else '.'
os.chdir(out)
# 권총: 짧은 노이즈 크랙 + 낮은 펀치
n=int(SR*0.25); x=lp(noise(n),0.5)*env(n,0.001,0.035) + 0.6*np.sin(2*np.pi*140*t(0.25)*(1-0.4*t(0.25)))*env(n,0.001,0.05)
save('sfx_pistol', x)
# 샷건: 더 길고 두꺼운 폭음
n=int(SR*0.55); x=lp(noise(n),0.25)*env(n,0.001,0.12) + 0.9*np.sin(2*np.pi*80*t(0.55)*(1-0.5*t(0.55)))*env(n,0.001,0.12) + 0.3*lp(noise(n),0.08)*env(n,0.01,0.25)
save('sfx_shotgun', x, 0.95)
# 저격총: 날카로운 크랙 + 긴 울림
n=int(SR*0.9); tt=t(0.9); x=noise(n)*env(n,0.0005,0.015)*1.2 + lp(noise(n),0.15)*env(n,0.002,0.25)*0.7 + 0.5*np.sin(2*np.pi*95*tt)*env(n,0.001,0.18)
save('sfx_sniper', x, 0.95)
# 명중: 짧고 높은 '틱'
n=int(SR*0.09); x=np.sin(2*np.pi*1900*t(0.09))*env(n,0.0005,0.02) + 0.4*np.sin(2*np.pi*2850*t(0.09))*env(n,0.0005,0.012)
save('sfx_hit', x, 0.7)
# 피격: 둔탁한 퍽
n=int(SR*0.22); tt=t(0.22); x=np.sin(2*np.pi*(180-300*tt)*tt)*env(n,0.001,0.06) + 0.5*lp(noise(n),0.2)*env(n,0.001,0.04)
save('sfx_hurt', x, 0.85)
# 처치: 두 음 상승 차임
tt=t(0.5); n=len(tt); x=np.zeros(n)
for f,st in [(880,0.0),(1320,0.12)]:
    m=tt>=st; tl=tt[m]-st
    x[m]+=(np.sin(2*np.pi*f*tl)+0.3*np.sin(2*np.pi*2*f*tl))*np.exp(-tl/0.18)*np.clip(tl/0.003,0,1)
save('sfx_kill', x, 0.7)
# 엄폐물 피격: 나무 '툭'
n=int(SR*0.15); tt=t(0.15); x=(np.sin(2*np.pi*420*tt)+0.6*np.sin(2*np.pi*690*tt))*env(n,0.0005,0.03) + 0.4*lp(noise(n),0.4)*env(n,0.0005,0.015)
save('sfx_cover_hit', x, 0.75)
# 엄폐물 파괴: 부서지는 소리 (노이즈 버스트 여러 개)
tt=t(0.7); n=len(tt); x=lp(noise(n),0.3)*env(n,0.002,0.2)
for k in range(7):
    st=rng.uniform(0,0.35); m=tt>=st; tl=tt[m]-st
    x[m]+=lp(noise(m.sum()),rng.uniform(0.2,0.7))*np.exp(-tl/rng.uniform(0.02,0.06))*0.8
x+=0.5*np.sin(2*np.pi*70*tt)*env(n,0.002,0.15)
save('sfx_cover_break', x, 0.9)
# 스폰: 상승 쉬익 + 반짝
tt=t(0.8); n=len(tt); sweep=np.sin(2*np.pi*np.cumsum(300+900*tt/0.8)/SR)
x=sweep*np.sin(np.pi*tt/0.8)*0.6 + lp(noise(n),0.6)*np.sin(np.pi*tt/0.8)*0.25
m=tt>=0.45; tl=tt[m]-0.45; x[m]+=0.5*np.sin(2*np.pi*1760*tl)*np.exp(-tl/0.12)
save('sfx_spawn', x, 0.6)
# 붕대: 천 감는 소리 (필터된 노이즈 스윕 3번, 약 1초)
tt=t(1.0); n=len(tt); x=np.zeros(n)
for k,st in enumerate([0.0,0.32,0.64]):
    m=(tt>=st)&(tt<st+0.3); tl=tt[m]-st
    x[m]+=lp(noise(m.sum()),0.35+0.1*k)*np.sin(np.pi*tl/0.3)**2
save('sfx_bandage', x, 0.6)
# 회복: 부드러운 상승 3음
tt=t(0.6); n=len(tt); x=np.zeros(n)
for f,st in [(660,0.0),(880,0.09),(1100,0.18)]:
    m=tt>=st; tl=tt[m]-st
    x[m]+=np.sin(2*np.pi*f*tl)*np.exp(-tl/0.2)*np.clip(tl/0.01,0,1)
save('sfx_heal', x, 0.55)
