#!/usr/bin/env python3
"""Pixel Window v0.2.0 - 時間帯と天気(デモ)で移ろう、アンビエントなドット絵の湖景 (Raspberry Pi Zero WH向け軽量版)
天気は手動デモ切替のみ。実際の天気・現在地・ネットAPIには接続しません。"""
import argparse, datetime, math, random
import pygame

W, H = 160, 120
VERSION = "0.2.0"
PHASES = ["morning", "day", "evening", "night"]
WEATHERS = ["clear", "cloudy", "rain"]
HORIZON = 74          # 水面の始まり
# 時間帯: 空(上, 下), 遠山の霞色, 光源色, 明るさ
PAL = {
    "morning": ((96, 112, 168), (236, 196, 186), (180, 168, 196), (255, 214, 170), 0.85),
    "day":     ((70, 132, 210), (176, 214, 236), (150, 180, 210), (255, 250, 220), 1.0),
    "evening": ((70, 58, 120), (240, 150, 120), (150, 110, 140), (255, 170, 110), 0.7),
    "night":   ((14, 18, 48), (44, 52, 96), (50, 58, 96), (210, 220, 255), 0.4),
}
GREY = (128, 134, 148)


def phase_for_hour(h):
    if 5 <= h < 9: return "morning"
    if 9 <= h < 16: return "day"
    if 16 <= h < 19: return "evening"
    return "night"


def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def dim(c, k): return tuple(max(0, min(255, int(v * k))) for v in c)


class Scene:
    def __init__(self, seed=11):
        r = random.Random(seed); self.r = r
        self.ridges = []
        for k in range(3):   # 奥→手前の山並み
            base, amp = 46 + k * 9, 9 - k * 2
            ph = r.uniform(0, 6)
            self.ridges.append([int(base - amp * (0.6 * math.sin(x / (18 - k * 3) + ph) + 0.4 * math.sin(x / 7.0 + ph * 2)))
                                for x in range(W)])
        self.reeds = [(x, r.randint(3, 7)) for x in list(range(2, 26, 3)) + list(range(136, 158, 3))]
        self.trees = [(8, 16), (17, 11), (146, 14), (154, 10)]
        self.drops = [[r.uniform(0, W), r.uniform(0, H), r.uniform(2.2, 3.2)] for _ in range(90)]
        self.ripples = [[r.uniform(4, W - 4), r.uniform(HORIZON + 4, H - 4), r.uniform(0, 1)] for _ in range(9)]
        self.glints = [(r.randrange(W), r.randrange(HORIZON + 3, H), r.uniform(0, 6)) for _ in range(26)]
        self.cache = {}

    # ---- 事前描画 (時間帯×天気ごとに1回) ----
    def _sky_cols(self, phase, weather):
        top, bot, haze, light, lv = PAL[phase]
        k = {"clear": 0.0, "cloudy": 0.55, "rain": 0.7}[weather]
        g = dim(GREY, 0.35 + 0.65 * lv)
        return mix(top, g, k), mix(bot, g, k * 0.9), mix(haze, g, k), light, lv * (1 - 0.25 * k)

    def static(self, phase, weather):
        key = (phase, weather)
        if key in self.cache: return self.cache[key]
        top, bot, haze, light, lv = self._sky_cols(phase, weather)
        s = pygame.Surface((W, H))
        for y in range(HORIZON):
            s.fill(mix(top, bot, (y / HORIZON) ** 1.3), (0, y, W, 1))
        # 光源
        sx, sy = {"morning": (34, 40), "day": (118, 20), "evening": (124, 42), "night": (40, 22)}[phase]
        glow = pygame.Surface((W, H), pygame.SRCALPHA)
        if weather == "clear":
            for rr in range(34, 6, -3):
                pygame.draw.circle(glow, light + (12,), (sx, sy), rr)
            pygame.draw.circle(glow, mix(light, (255, 255, 255), .5) + (255,), (sx, sy), 5)
            if phase == "night":   # 三日月
                pygame.draw.circle(glow, top + (255,), (sx + 3, sy - 2), 4)
        elif weather == "cloudy":  # 雲越しの拡散光 (円盤なし)
            for rr in range(44, 10, -4):
                pygame.draw.circle(glow, light + (8,), (sx, sy), rr)
        s.blit(glow, (0, 0))
        # 山並み (奥ほど霞む)
        for k, ridge in enumerate(self.ridges):
            col = mix(haze, dim(haze, 0.45 + 0.1 * k), 0.35 + 0.25 * k)
            col = mix(col, bot, 0.35 - 0.1 * k)
            for x in range(W):
                s.fill(col, (x, ridge[x], 1, HORIZON - ridge[x]))
        # 山裾の霞
        mist = pygame.Surface((W, 10), pygame.SRCALPHA)
        for y in range(10): mist.fill(bot + (int(110 * (1 - abs(y - 6) / 6)) if y < 12 else 0,), (0, y, W, 1))
        s.blit(mist, (0, HORIZON - 9))
        # 水面: 空の映り込み (上下反転・暗め)
        for y in range(HORIZON, H):
            d = (y - HORIZON) / (H - HORIZON)
            s.fill(dim(mix(bot, top, d), 0.75 - 0.15 * d), (0, y, W, 1))
        if weather == "clear":   # 光の道
            for y in range(HORIZON + 1, H, 2):
                w = 3 + (y - HORIZON) // 4
                s.fill(mix(dim(mix(bot, top, .3), .75), light, .45), (sx - w // 2, y, w, 1))
        # 岸辺のシルエット
        sil = dim(mix(haze, (20, 30, 35), .75), 0.5 + 0.5 * lv)
        for x, h in self.reeds:
            s.fill(sil, (x, HORIZON + 6 - h, 1, h + 1))
        s.fill(sil, (0, HORIZON + 5, 28, 3)); s.fill(sil, (134, HORIZON + 5, 26, 3))
        for tx, th in self.trees:
            pygame.draw.polygon(s, sil, [(tx, HORIZON + 6 - th), (tx - 4, HORIZON + 6), (tx + 4, HORIZON + 6)])
        self.cache[key] = s
        return s

    def cloud_layer(self, phase, weather):
        key = ("cloud", phase, weather)
        if key in self.cache: return self.cache[key]
        _, bot, _, light, lv = self._sky_cols(phase, weather)
        r = random.Random(3 + WEATHERS.index(weather))
        L = pygame.Surface((W * 2, HORIZON), pygame.SRCALPHA)
        if weather == "clear":   # 薄い筋雲 少し
            col = mix((255, 255, 255), light, .3)
            for _ in range(5):
                x, y, w = r.randrange(W * 2), r.randrange(6, 40), r.randint(16, 34)
                pygame.draw.ellipse(L, col + (int(70 * lv) + 30,), (x, y, w, 3))
        else:                    # 厚い雲の帯 (空を覆う)
            n = 26 if weather == "cloudy" else 32
            base = dim(GREY, (0.55 if weather == "rain" else 1.0) * (0.4 + 0.6 * lv))
            for i in range(n):
                x, y = r.randrange(-10, W * 2), r.randrange(-6, 48 if weather == "cloudy" else 56)
                w, h = r.randint(26, 52), r.randint(8, 16)
                shade = mix(base, (255, 255, 255), 0.12 * (1 - y / 60)) if weather == "cloudy" else dim(base, 0.9 + 0.2 * r.random())
                a = 150 if weather == "cloudy" else 200
                for xx in (x, x - W * 2):
                    pygame.draw.ellipse(L, shade + (a,), (xx, y, w, h))
                    if weather == "cloudy":   # 上側の明るい縁
                        pygame.draw.ellipse(L, mix(shade, light, .3) + (110,), (xx + 4, y, w - 8, h // 2))
            if weather == "cloudy":   # 雲の縁の明るさ (拡散光)
                for i in range(10):
                    x, y = r.randrange(W * 2), r.randrange(2, 40)
                    pygame.draw.ellipse(L, mix(base, light, .35) + (90,), (x, y, r.randint(20, 40), 3))
        self.cache[key] = L
        return L

    def draw(self, dst, phase, weather, t, frame=True):
        dst.blit(self.static(phase, weather), (0, 0))
        top, bot, haze, light, lv = self._sky_cols(phase, weather)
        cl = self.cloud_layer(phase, weather)
        speed = {"clear": 0.8, "cloudy": 1.5, "rain": 2.6}[weather]
        ox = int(t * speed) % (W * 2)
        dst.blit(cl, (-ox, 0)); dst.blit(cl, (W * 2 - ox, 0))
        # 漂う霞 (ゆっくり)
        my = HORIZON - 14 + int(2 * math.sin(t * 0.2))
        for i in range(4):
            x = int((t * 2 + i * 47) % (W + 60)) - 50
            pygame.draw.ellipse(dst, mix(bot, haze, .4), (x, my + i % 2 * 3, 50, 3))
        # 水面のゆらぎ
        for i in range(7):
            y = HORIZON + 3 + i * 6 + int(math.sin(t * 0.8 + i) * 1.5)
            x = int((t * (3 + i) + i * 31) % (W + 30)) - 15
            dst.fill(mix(dim(bot, .8), light, .25), (x, y, 8 + i * 2, 1))
        if weather == "clear":   # 日差し(光の筋)ときらめき
            if phase != "night":
                ray = mix(bot, light, .5)
                sx = {"morning": 34, "day": 118, "evening": 124}[phase]
                for k in range(3):
                    if math.sin(t * 0.5 + k * 2) > -0.2:
                        x0 = sx - 30 + k * 26
                        for y in range(30, HORIZON, 3):
                            dst.set_at((min(W - 1, max(0, x0 + (y - 30) // 3)), y), ray)
            for gx, gy, gp in self.glints:
                if math.sin(t * 2.2 + gp * 5) > 0.75:
                    dst.fill(mix(light, (255, 255, 255), .5), (gx, gy, 2, 1))
        if weather == "rain":
            rc = mix(dim(GREY, 1.4), light, .15)
            for d in self.drops:
                d[1] += d[2]; d[0] -= 0.6
                if d[1] > H: d[1] = -4; d[0] = self.r.uniform(0, W + 20)
                x, y = int(d[0]) % W, int(d[1])
                dst.fill(rc, (x, y, 1, 3))
            rp = mix(dim(bot, 1.1), (230, 235, 245), .4)
            for rp_ in self.ripples:   # 水面の波紋
                rp_[2] += 0.035
                if rp_[2] > 1:
                    rp_[:] = [self.r.uniform(4, W - 4), self.r.uniform(HORIZON + 4, H - 4), 0]
                k = rp_[2]; w = int(2 + k * 12)
                pygame.draw.ellipse(dst, rp, (int(rp_[0] - w / 2), int(rp_[1] - w / 6), w, max(2, w // 3)), 1)
        if not frame: return
        fc = (95, 70, 50); hl = (130, 100, 75)
        dst.fill(fc, (0, 0, W, 4)); dst.fill(fc, (0, H - 6, W, 6))
        dst.fill(fc, (0, 0, 4, H)); dst.fill(fc, (W - 4, 0, 4, H))
        dst.fill(fc, (W // 2 - 1, 0, 3, H)); dst.fill(fc, (0, 58, W, 2))
        dst.fill(hl, (0, H - 6, W, 1))


def make_window(fullscreen, scale):
    pygame.display.init()
    if fullscreen:
        return pygame.display.set_mode((0, 0), pygame.FULLSCREEN)
    return pygame.display.set_mode((W * scale, H * scale))


def blit_scaled(screen, canvas, fit="contain", rotate180=False):
    sw, sh = screen.get_size()
    if fit == "cover":
        k = max(1, -(-sw // W), -(-sh // H))
    else:
        k = max(1, min(sw // W, sh // H))
    img = pygame.transform.scale(canvas, (W * k, H * k))  # nearest neighbor
    if rotate180: img = pygame.transform.rotate(img, 180)
    screen.fill((0, 0, 0)); screen.blit(img, ((sw - W * k) // 2, (sh - H * k) // 2))


def render_frames(modes, n, fps, frame=True):
    """modes の各 (phase, weather) を n フレーム描画した Surface リストのリストを返す"""
    out = []
    for ph, we in modes:
        sc = Scene(); c = pygame.Surface((W, H)); fr = []
        for f in range(n):
            sc.draw(c, ph, we, f / fps, frame); fr.append(c.copy())
        out.append(fr)
    return out


def snapshot(path, modes, scale=3, frame=True):
    frames = render_frames(modes, 45, 15, frame)
    gap = 6
    out = pygame.Surface((len(modes) * (W * scale + gap) - gap, H * scale)); out.fill((20, 20, 20))
    for i, fr in enumerate(frames):
        out.blit(pygame.transform.scale(fr[-1], (W * scale, H * scale)), (i * (W * scale + gap), 0))
    pygame.image.save(out, path)


def main():
    ap = argparse.ArgumentParser(description="Pixel Window")
    ap.add_argument("--fullscreen", action="store_true")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--fps", type=int, default=15)
    ap.add_argument("--weather", choices=WEATHERS, default="clear", help="天気デモの初期状態")
    ap.add_argument("--rain", action="store_true", help="--weather rain と同じ(互換)")
    ap.add_argument("--phase", choices=PHASES, help="時間帯を固定")
    ap.add_argument("--frames", type=int, default=0, help="指定フレームで終了(テスト用)")
    ap.add_argument("--snapshot", help="昼の晴/曇/雨を並べたPNGを書き出して終了")
    ap.add_argument("--no-frame", action="store_true", help="ソフト側の窓枠を描かない(筐体の物理窓枠用)")
    ap.add_argument("--fit", choices=["contain", "cover"], default="contain", help="cover=画面を埋める(上下を少しトリミング)")
    ap.add_argument("--rotate180", action="store_true", help="画面を上下逆に取り付けた場合")
    a = ap.parse_args()
    weather = "rain" if a.rain else a.weather
    if a.snapshot:
        pygame.init(); snapshot(a.snapshot, [("day", w) for w in WEATHERS], frame=not a.no_frame); return
    screen = make_window(a.fullscreen, a.scale); pygame.display.set_caption("Pixel Window")
    pygame.mouse.set_visible(not a.fullscreen)
    canvas = pygame.Surface((W, H)); sc = Scene(); clock = pygame.time.Clock()
    manual, full, t, n = a.phase, a.fullscreen, 0.0, 0
    keys = {pygame.K_1: "morning", pygame.K_2: "day", pygame.K_3: "evening", pygame.K_4: "night"}
    while True:
        for e in pygame.event.get():
            if e.type == pygame.QUIT: return
            if e.type == pygame.KEYDOWN:
                if e.key in (pygame.K_ESCAPE, pygame.K_q): return
                if e.key in keys: manual = keys[e.key]
                elif e.key == pygame.K_a: manual = None
                elif e.key == pygame.K_w: weather = WEATHERS[(WEATHERS.index(weather) + 1) % 3]
                elif e.key == pygame.K_s: weather = "clear"
                elif e.key == pygame.K_c: weather = "cloudy"
                elif e.key == pygame.K_r: weather = "clear" if weather == "rain" else "rain"
                elif e.key == pygame.K_f:
                    full = not full; screen = make_window(full, a.scale); pygame.mouse.set_visible(not full)
        phase = manual or phase_for_hour(datetime.datetime.now().hour)
        sc.draw(canvas, phase, weather, t, not a.no_frame)
        blit_scaled(screen, canvas, a.fit, a.rotate180); pygame.display.flip()
        t += clock.tick(a.fps) / 1000; n += 1
        if a.frames and n >= a.frames: print(f"ok frames={n} phase={phase} weather={weather}"); return


if __name__ == "__main__":
    main()
