#pragma once

void appSetup();
void appLoop();

// Simulates a finger tap at a screen position, for scripted testing.
void appTap(int x, int y);

// Simulates a finger pressing at one position, sliding to another and lifting.
void appDrag(int x0, int y0, int x1, int y1);
