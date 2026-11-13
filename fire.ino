/*
  Fire animation for the 3x3x3 cube.

  Integer-only "heat" simulation (a tiny cousin of the well known Fire2012 idea):
    1. every cell cools down a random amount (more cooling higher up)
    2. heat rises: each cell takes most of its heat from the cell below
    3. heat spreads a little sideways, so the nine columns form one flame
    4. random sparks add heat at the bottom, more often near the center axis
  The heat value (0-255) is then mapped to a color through a 3-stop palette
  (black -> pal[0] -> pal[1] -> pal[2]), so color variations only need another palette.

  cube[x][y][z]: y is height (0 = bottom, 2 = top)

  Memory: 27 bytes heat + 9 bytes palette + a few statics. No floating point.
*/


// ---- Tunables --------------------------------------------------------------
#define FIRE_STEP_MS     50   // time between simulation steps (ms)
#define FIRE_COOL_BASE   60   // max random cooling per step, bottom layer
#define FIRE_COOL_RISE   60   // extra max cooling per layer going up
#define FIRE_SPREAD       1   // sideways heat mixing, 0 (none) to 4 (strong)
#define FIRE_SPARK_MIN  140   // minimum heat added by a spark (max is 255)

// Spark chance per step (out of 256) by distance from the center axis:
// center column, the four side columns, the four corner columns
static const uint8_t fireSparkChance[3] = { 170, 95, 30 };

// Classic fire palette: deep red -> orange -> yellowish white
// (values are "perceptual", they go through the gamma table before reaching the LEDs)
static const RGB fireClassic[3] = {
  { 255,  40,   0 },
  { 255, 130,   0 },
  { 255, 215,  60 }
};


static uint8_t fireHeat[3][3][3];   // [x][y][z]
static unsigned long fireLast = 0;


static uint8_t fireQadd(uint8_t a, uint8_t b) {
  unsigned int s = (unsigned int)a + b;
  return (s > 255) ? 255 : (uint8_t)s;
}

static uint8_t fireQsub(uint8_t a, uint8_t b) {
  return (a > b) ? (uint8_t)(a - b) : 0;
}

// Linear interpolation, f = 0..255
static uint8_t fireLerp(uint8_t a, uint8_t b, uint8_t f) {
  return (uint8_t)(a + ((long)b - a) * f / 256);
}


// Heat (0-255) to color: three equal segments black->pal[0]->pal[1]->pal[2]
static RGB fireColor(uint8_t heat, const RGB *pal) {
  uint8_t seg = heat / 85;
  if (seg >= 3) return pal[2];
  uint8_t f = (heat - seg * 85) * 3;
  RGB a = (seg == 0) ? black : pal[seg - 1];
  RGB b = pal[seg];
  return { fireLerp(a.r, b.r, f), fireLerp(a.g, b.g, f), fireLerp(a.b, b.b, f) };
}


static void fireStep() {
  // 1. cool down
  for (int x = 0; x < 3; x++)
    for (int z = 0; z < 3; z++)
      for (int y = 0; y < 3; y++)
        fireHeat[x][y][z] = fireQsub(fireHeat[x][y][z],
                                     (uint8_t)random(FIRE_COOL_BASE + y * FIRE_COOL_RISE + 1));

  // 2. heat rises (top layer first, so it reads the not yet updated layer below)
  for (int x = 0; x < 3; x++)
    for (int z = 0; z < 3; z++)
      for (int y = 2; y > 0; y--)
        fireHeat[x][y][z] = (2 * fireHeat[x][y - 1][z] + fireHeat[x][y][z]) / 3;

  // 3. mix sideways within each layer
  for (int y = 0; y < 3; y++) {
    uint8_t tmp[3][3];
    for (int x = 0; x < 3; x++)
      for (int z = 0; z < 3; z++)
        tmp[x][z] = fireHeat[x][y][z];

    for (int x = 0; x < 3; x++) {
      for (int z = 0; z < 3; z++) {
        int sum = 0, n = 0;
        if (x > 0) { sum += tmp[x - 1][z]; n++; }
        if (x < 2) { sum += tmp[x + 1][z]; n++; }
        if (z > 0) { sum += tmp[x][z - 1]; n++; }
        if (z < 2) { sum += tmp[x][z + 1]; n++; }
        int avg = sum / n;
        fireHeat[x][y][z] = (tmp[x][z] * (4 - FIRE_SPREAD) + avg * FIRE_SPREAD) / 4;
      }
    }
  }

  // 4. sparks at the bottom
  for (int x = 0; x < 3; x++) {
    for (int z = 0; z < 3; z++) {
      int dist = abs(x - 1) + abs(z - 1);
      if (random(256) < fireSparkChance[dist]) {
        fireHeat[x][0][z] = fireQadd(fireHeat[x][0][z], (uint8_t)random(FIRE_SPARK_MIN, 256));
      }
    }
  }
}


// Run the simulation (at its own pace) and draw it with the given palette
static void fireUpdate(const RGB *pal) {
  if (currentTime - fireLast < FIRE_STEP_MS) return;
  fireLast = currentTime;

  fireStep();

  for (int x = 0; x < 3; x++)
    for (int y = 0; y < 3; y++)
      for (int z = 0; z < 3; z++)
        cube[x][y][z] = fireColor(fireHeat[x][y][z], pal);
}


void fire() {
  fireUpdate(fireClassic);
}
