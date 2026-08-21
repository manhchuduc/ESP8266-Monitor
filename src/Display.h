#ifndef DISPLAY_H
#define DISPLAY_H

void drawDegreeCValue(float value, int rightX, int y);
void drawStatusBar();
void drawMenuRow(int rowIndex, const char *label, const char *rightText, bool selected);
void drawMainScreen(float temp, float hum, float dewPoint);
void drawMainMenu();
void drawFanMenu();
void drawFanSpeedAdjust();
void drawScreenTimeoutAdjust();
void drawTelemetryAdjust();

#endif // DISPLAY_H
