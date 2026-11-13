#include "app.h"

static void label(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
  tft.setTextColor(fg, bg);
  tft.drawString(s, x, y, 2);
}

void lcd_test_draw() {
  const uint16_t BLK = TFT_BLACK;
  const uint16_t WHT = TFT_WHITE;
  const uint16_t RED = TFT_RED;
  const uint16_t GRN = TFT_GREEN;
  const uint16_t BLU = TFT_BLUE;
  const uint16_t YEL = TFT_YELLOW;
  const uint16_t CYN = TFT_CYAN;
  const uint16_t MAG = TFT_MAGENTA;
  const uint16_t GRY = tft.color565(40, 40, 48);

  tft.fillScreen(GRY);

  // 1px orientation border
  tft.drawRect(0, 0, LCD_W, LCD_H, WHT);
  tft.drawRect(1, 1, LCD_W - 2, LCD_H - 2, YEL);

  tft.fillRect(0, 0, LCD_W, 22, BLK);
  label(8, 3, "LCD TEST", YEL, BLK);

  char line[48];
  snprintf(line, sizeof(line), "%s", PROFILE_NAME);
  tft.setTextColor(WHT, BLK);
  tft.drawString(line, 100, 4, 2);

  // RGB bars (colour identity)
  const int bx = 16, by = 32, bw = 88, bh = 36;
  tft.fillRect(bx, by, bw, bh, RED);
  tft.fillRect(bx + bw + 8, by, bw, bh, GRN);
  tft.fillRect(bx + 2 * (bw + 8), by, bw, bh, BLU);
  tft.setTextColor(WHT, RED);
  tft.drawCentreString("R", bx + bw / 2, by + 10, 2);
  tft.setTextColor(BLK, GRN);
  tft.drawCentreString("G", bx + bw + 8 + bw / 2, by + 10, 2);
  tft.setTextColor(WHT, BLU);
  tft.drawCentreString("B", bx + 2 * (bw + 8) + bw / 2, by + 10, 2);

  // White / black / grey
  const int y2 = 76;
  tft.fillRect(16, y2, 90, 28, WHT);
  tft.fillRect(114, y2, 90, 28, BLK);
  tft.drawRect(114, y2, 90, 28, WHT);
  tft.fillRect(212, y2, 90, 28, tft.color565(160, 160, 160));
  tft.setTextColor(BLK, WHT);
  tft.drawCentreString("WHITE", 16 + 45, y2 + 6, 2);
  tft.setTextColor(WHT, BLK);
  tft.drawCentreString("BLACK", 114 + 45, y2 + 6, 2);
  tft.setTextColor(BLK, tft.color565(160, 160, 160));
  tft.drawCentreString("GRAY", 212 + 45, y2 + 6, 2);

  // Grey ramp via fillRect (pushBlock, always byte-swapped on ESP32)
  for (int i = 0; i < 16; ++i) {
    uint8_t v = (uint8_t)(i * 17);
    tft.fillRect(16 + i * 18, 110, 18, 12, tft.color565(v, v, v));
  }
  // Same 565 values via pushPixels (scene path). Must match the ramp above.
  {
    uint16_t line[288];
    for (int x = 0; x < 288; ++x) {
      uint8_t v = (uint8_t)((x / 18) * 17);
      line[x] = tft.color565(v, v, v);
    }
    tft.setSwapBytes(true);
    tft.startWrite();
    tft.setAddrWindow(16, 122, 288, 12);
    for (int y = 0; y < 12; ++y) tft.pushPixels(line, 288);
    tft.endWrite();
  }
  tft.setTextColor(YEL, GRY);
  tft.drawString("fillRect vs pushPixels 565", 16, 136, 1);

  // B/W checker
  const int cx0 = 232, cy0 = 152, cs = 8;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 8; ++x) {
      uint16_t c = ((x + y) & 1) ? WHT : BLK;
      tft.fillRect(cx0 + x * cs, cy0 + y * cs, cs, cs, c);
    }
  }
  tft.setTextColor(WHT, GRY);
  tft.drawString("B/W", cx0, cy0 + 34, 1);

  // Orientation labels — these tell you if rotation 1 vs 3 is needed
  tft.fillTriangle(LCD_W / 2, 24, LCD_W / 2 - 8, 34, LCD_W / 2 + 8, 34, CYN);
  label(LCD_W / 2 - 18, 36, "TOP", CYN, GRY);
  tft.fillTriangle(LCD_W / 2, LCD_H - 8, LCD_W / 2 - 8, LCD_H - 18, LCD_W / 2 + 8,
                   LCD_H - 18, MAG);
  label(LCD_W / 2 - 28, LCD_H - 36, "BOTTOM", MAG, GRY);
  tft.fillTriangle(6, LCD_H / 2, 16, LCD_H / 2 - 8, 16, LCD_H / 2 + 8, YEL);
  tft.setTextColor(YEL, GRY);
  tft.drawString("LEFT", 20, LCD_H / 2 - 8, 2);
  tft.fillTriangle(LCD_W - 6, LCD_H / 2, LCD_W - 16, LCD_H / 2 - 8, LCD_W - 16,
                   LCD_H / 2 + 8, YEL);
  tft.drawRightString("RIGHT", LCD_W - 20, LCD_H / 2 - 8, 2);

  char info[64];
  snprintf(info, sizeof(info), "drv %s  %s  inv=%d", PROFILE_DRIVER, PROFILE_COLOR,
           (int)g.invert);
  tft.setTextColor(WHT, GRY);
  tft.drawString(info, 16, 150, 2);

  snprintf(info, sizeof(info), "rot=%u  320x240  bl=%u  fps=%u", g.rotation,
           g.brightness, g.fps);
  tft.drawString(info, 16, 170, 2);

  snprintf(info, sizeof(info), "swapRB=%d swapBytes=%d  GPIO21 PWM", (int)g.swap_rb,
           (int)tft.getSwapBytes());
  tft.drawString(info, 16, 190, 2);

  tft.setTextColor(TFT_GREENYELLOW, GRY);
  tft.drawString("serial: help | scene | weather | phase", 16, 212, 1);

  // Live brightness bar
  tft.drawRect(16, 224, 200, 10, WHT);
  int fill = (int)g.brightness * 198 / 255;
  tft.fillRect(17, 225, fill, 8, YEL);
}
