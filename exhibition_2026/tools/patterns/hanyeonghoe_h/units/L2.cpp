namespace L2 { // "flower" — rapeseed heads, small purple flowers and grass swaying in the wind
  // knobVal[0] Bloom = flower size (a small bud 0.4 .. a full bloom 1.6; the stems grow
  // with it and lift the heads), knobVal[1] CloudSize = sway width, knobVal[2] Wind =
  // sway speed. K1 press = gust: everything leans right and sways faster and wider.
  constexpr int L2_FLOWER_N = 14;
  constexpr int L2_PURPLE_N = 13;
  constexpr int L2_GRASS_N = 20;
  constexpr float L2_TWENTY_PI = 62.8318531f; // 20π: windTime feeds sin(t), sin(1.2t), sin(1.3t) only

  struct Flower { float rx, fx, fy, s; bool isMain; };
  struct Purple { float rx, bx, by; };
  struct Blade { float x0, y0, x1, y1; };

  static const Flower FLOWERS[L2_FLOWER_N] = {
    { 5.0f,  5.0f,  88.0f,  1.1f, false },
    { 7.0f,  6.0f,  78.0f,  1.0f, false },
    { 9.0f,  9.0f,  80.0f,  1.2f, true  },
    { 11.0f, 11.0f, 91.0f,  1.1f, false },
    { 15.0f, 15.0f, 76.0f,  1.3f, true  },
    { 17.0f, 17.0f, 86.0f,  1.1f, false },
    { 17.0f, 16.0f, 97.0f,  1.1f, false },
    { 20.0f, 20.0f, 89.0f,  1.1f, false },
    { 22.0f, 22.0f, 68.0f,  1.6f, true  },
    { 27.0f, 27.0f, 100.0f, 1.2f, false },
    { 28.0f, 28.0f, 76.0f,  1.4f, true  },
    { 32.0f, 33.0f, 80.0f,  1.3f, true  },
    { 42.0f, 43.0f, 116.0f, 1.2f, false },
    { 55.0f, 56.0f, 120.0f, 1.2f, false }
  };

  static const Purple PURPLES[L2_PURPLE_N] = {
    { 14.0f, 14.0f, 106.0f }, { 16.0f, 16.0f, 104.0f }, { 22.0f, 22.0f, 103.0f },
    { 24.0f, 24.0f, 101.0f }, { 25.0f, 25.0f, 110.0f }, { 35.0f, 35.0f, 112.0f },
    { 37.0f, 37.0f, 104.0f }, { 38.0f, 38.0f, 113.0f }, { 43.0f, 43.0f, 105.0f },
    { 45.0f, 45.0f, 99.0f  }, { 49.0f, 49.0f, 108.0f }, { 51.0f, 51.0f, 103.0f },
    { 59.0f, 59.0f, 114.0f }
  };

  static const Blade BLADES[L2_GRASS_N] = {
    { 2.0f, 127.0f, 8.0f, 108.0f },   { 4.0f, 127.0f, 3.0f, 101.0f },
    { 6.0f, 127.0f, 12.0f, 105.0f },  { 8.0f, 127.0f, 5.0f, 111.0f },
    { 12.0f, 127.0f, 16.0f, 103.0f }, { 14.0f, 127.0f, 9.0f, 107.0f },
    { 18.0f, 127.0f, 24.0f, 110.0f }, { 20.0f, 127.0f, 16.0f, 113.0f },
    { 24.0f, 127.0f, 26.0f, 104.0f }, { 26.0f, 127.0f, 21.0f, 109.0f },
    { 30.0f, 127.0f, 36.0f, 110.0f }, { 34.0f, 127.0f, 30.0f, 105.0f },
    { 38.0f, 127.0f, 44.0f, 104.0f }, { 40.0f, 127.0f, 37.0f, 113.0f },
    { 44.0f, 127.0f, 48.0f, 101.0f }, { 46.0f, 127.0f, 42.0f, 110.0f },
    { 50.0f, 127.0f, 56.0f, 107.0f }, { 52.0f, 127.0f, 47.0f, 112.0f },
    { 58.0f, 127.0f, 62.0f, 113.0f }, { 60.0f, 127.0f, 54.0f, 109.0f }
  };

  // Rapeseed petals: dx, dy, rx, ry, all x s.
  static const float PETALS[5][4] = {
    {  0.0f, -1.8f, 1.1f, 1.4f },
    { -1.4f, -1.4f, 1.0f, 1.3f },
    {  1.4f, -1.4f, 1.0f, 1.3f },
    { -2.4f, -0.4f, 1.1f, 1.1f },
    {  2.4f, -0.4f, 1.1f, 1.1f }
  };

  static float windTime = 0.0f;
  static float gustTarget = 0.0f; // K1 press gust target; 0 on the idle return
  static float gust = 0.0f;       // gust strength 0..1; 0 on the idle return
  static float swayAmp = 1.0f;    // 1 + 1.5 gust, recomputed every frame
  static float lean = 0.0f;       // 5 gust px to the right, recomputed every frame
  static float bloom = 1.0f;
  static float sway = 1.7f;

  static inline int iceil(float x) { return -PFMath::ifloor(-x); }

  static inline void plot(float x, float y, float v) {
    L2_setValue(PFMath::ifloor(x), PFMath::ifloor(y), v); // bounds-checked there
  }

  // Samples the segment every half pixel. For the radii used here (0.7, 0.5) the brush
  // is the single centre pixel, but the loop is kept general, as in the JS.
  static void drawBrushLine(float x0, float y0, float x1, float y1, float radius, float val) {
    const float ddx = x1 - x0;
    const float ddy = y1 - y0;
    const float dist = sqrtf(ddx * ddx + ddy * ddy);
    int steps = PFMath::ifloor(dist * 2.0f);
    if (steps < 1) steps = 1;
    const int r = PFMath::ifloor(radius);
    const float radSq = radius * radius;
    const float invSteps = 1.0f / (float)steps;
    if (r == 0) {
      // A one-pixel brush (every call here): samples 0.5 px apart mostly land on the
      // pixel before them, and repainting a pixel with the same value changes nothing.
      int lastX = INT32_MIN, lastY = INT32_MIN;
      for (int i = 0; i <= steps; i++) {
        const float frac = (float)i * invSteps;
        const int px = PFMath::ifloor(x0 + ddx * frac);
        const int py = PFMath::ifloor(y0 + ddy * frac);
        if (px == lastX && py == lastY) continue;
        lastX = px; lastY = py;
        L2_setValue(px, py, val);
      }
      return;
    }
    for (int i = 0; i <= steps; i++) {
      const float frac = (float)i * invSteps;
      const float cx = x0 + ddx * frac;
      const float cy = y0 + ddy * frac;
      for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
          if ((float)(dx * dx + dy * dy) <= radSq) plot(cx + (float)dx, cy + (float)dy, val);
    }
  }

  static void drawDab(float cx, float cy, float rx, float ry, float val) {
    const int startX = PFMath::ifloor(cx - rx);
    const int endX = iceil(cx + rx);
    const int startY = PFMath::ifloor(cy - ry);
    const int endY = iceil(cy + ry);
    const float invRx = 1.0f / rx;
    const float invRy = 1.0f / ry;
    for (int py = startY; py <= endY; py++) {
      const float dy = ((float)py - cy) * invRy;
      const float dySq = dy * dy;
      if (dySq > 1.0f) continue; // no pixel of this row can pass
      for (int px = startX; px <= endX; px++) {
        const float dx = ((float)px - cx) * invRx;
        if (dx * dx + dySq <= 1.0f) L2_setValue(px, py, val);
      }
    }
  }

  // Crown-shaped rapeseed head.
  static void drawRapeFlowerHead(float cx, float cy, float scale, bool isMain) {
    const float s = scale * bloom;
    const float coreVal = isMain ? 0.88f : 0.65f;
    const float petalVal = isMain ? 0.98f : 0.72f;
    const float calyxVal = 0.32f;
    drawDab(cx, cy + 1.2f * s, 1.6f * s, 0.8f * s, calyxVal);
    drawDab(cx, cy + 0.3f * s, 1.8f * s, 1.2f * s, coreVal);
    for (int i = 0; i < 5; i++)
      drawDab(cx + PETALS[i][0] * s, cy + PETALS[i][1] * s, PETALS[i][2] * s, PETALS[i][3] * s, petalVal);
  }

  // Small purple flower, 3-4 px wide.
  static void drawCompactPurpleFlower(float cx, float cy, float val) {
    // Below the bud size (0.6) the pixels would vanish, so it stops there.
    float s = bloom * 0.95f;
    if (s < 0.6f) s = 0.6f;
    drawDab(cx, cy, 1.3f * s, 0.8f * s, val);                                  // core
    drawDab(cx - 1.3f * s, cy - 0.2f * s, 0.8f * s, 0.5f * s, val + 0.05f);  // left wing
    drawDab(cx + 1.3f * s, cy - 0.2f * s, 0.8f * s, 0.5f * s, val + 0.05f);  // right wing
    drawDab(cx, cy - 0.8f * s, 0.6f * s, 0.6f * s, val + 0.03f);             // top
  }

  void setup() {
    windTime = 0.0f;
    gustTarget = 0.0f;
    gust = 0.0f;
    swayAmp = 1.0f;
    lean = 0.0f;
  }

  void update(float dt, const InputFrame& input) {
    // K1 press: gust - the same formula as the cloud layer (see L1::update).
    if (input.btnPressed[0]) gustTarget = 1.0f;
    gustTarget -= dt * 0.45f;
    if (gustTarget < 0.0f) gustTarget = 0.0f;
    float follow = dt * 7.0f;
    if (follow > 1.0f) follow = 1.0f;
    gust += (gustTarget - gust) * follow;
    if (gustTarget == 0.0f && gust < 0.001f) gust = 0.0f;

    // The gust adds to the sway speed rather than multiplying it: fast even at K3's
    // minimum, not frantic at its maximum.
    windTime += dt * (knobVal[2] * 2.0f + gust * 6.0f);
    // windTime only feeds sin(t), sin(1.2t), sin(1.3t): 20π is exactly one lap.
    if (windTime >= L2_TWENTY_PI) windTime -= L2_TWENTY_PI;

    float tNorm = (knobVal[0] - 2.0f) / 6.5f;
    if (tNorm < 0.0f) tNorm = 0.0f; else if (tNorm > 1.0f) tNorm = 1.0f;
    // Flower size 0.4 (small bud) .. 1.6 (full bloom), 1.0 in the middle.
    bloom = 0.4f + tNorm * 1.2f;

    sway = knobVal[1];

    // The gust's shape, once per frame: sway width x(1 + 1.5 gust), lean right in px.
    swayAmp = 1.0f + 1.5f * gust;
    lean = gust * 5.0f;
  }

  void draw() {
    // The JS clears the layer to 0.0 first. The scaffold has already cleared it to zero
    // bytes, which is what 0.0 paints while ramp(0.0) is transparent black (it is, in
    // this ramp). Should the ramp ever change, clear to 0.0 as the JS does.
    if (L2_RAMP[0][0] | L2_RAMP[0][1] | L2_RAMP[0][2] | L2_RAMP[0][3])
      for (int y = 0; y < FRAME_H; y++)
        for (int x = 0; x < FRAME_W; x++) L2_setValue(x, y, 0.0f);

    const float t = windTime;
    const float swayK = 1.0f + sway * 0.4f;
    // The stems grow as the flowers bloom and lift the heads: 0 in the middle, -4 px
    // at the bud end, +4 px at full bloom (half that for the purple flowers).
    const float lift = (bloom - 1.0f) * 7.0f;
    const float liftBud = (bloom - 1.0f) * 3.5f;

    // Each sway is used twice (stem, then head): computed once per frame. ~50 sinf.
    float swayF[L2_FLOWER_N];
    float swayP[L2_PURPLE_N];
    for (int i = 0; i < L2_FLOWER_N; i++)
      swayF[i] = sinf(t + (float)i * 1.5f) * 1.1f * swayK * swayAmp + lean;
    for (int k = 0; k < L2_PURPLE_N; k++)
      swayP[k] = sinf(t * 1.2f + (float)k * 1.8f) * 0.9f * swayK * swayAmp + lean * 0.8f;

    // 1. Stems, from the ground (y 127).
    for (int i = 0; i < L2_FLOWER_N; i++) {
      const Flower& fl = FLOWERS[i];
      const float stemVal = 0.22f + (float)(i % 3) * 0.03f;
      drawBrushLine(fl.rx, 127.0f, fl.fx + swayF[i], fl.fy - lift + 1.5f, 0.7f, stemVal);
    }
    for (int k = 0; k < L2_PURPLE_N; k++) {
      const Purple& pf = PURPLES[k];
      drawBrushLine(pf.rx, 127.0f, pf.bx + swayP[k], pf.by - liftBud + 1.2f, 0.5f, 0.23f);
    }

    // 2. Grass blades.
    for (int j = 0; j < L2_GRASS_N; j++) {
      const Blade& gb = BLADES[j];
      const float swayGrass = sinf(t * 1.3f + (float)j) * 0.6f * swayAmp + lean * 0.6f;
      const float bladeVal = 0.06f + (float)((j * 7) % 11) * 0.01f;
      drawBrushLine(gb.x0, gb.y0, gb.x1 + swayGrass, gb.y1, 0.7f, bladeVal);
    }

    // 3. Purple flower heads.
    for (int k = 0; k < L2_PURPLE_N; k++) {
      const Purple& pf = PURPLES[k];
      const float baseTone = 0.38f + (float)(k % 4) * 0.03f;
      drawCompactPurpleFlower(pf.bx + swayP[k], pf.by - liftBud, baseTone);
    }

    // 4. Yellow rapeseed heads, on top.
    for (int i = 0; i < L2_FLOWER_N; i++) {
      const Flower& fl = FLOWERS[i];
      drawRapeFlowerHead(fl.fx + swayF[i], fl.fy - lift, fl.s, fl.isMain);
    }
  }
}
