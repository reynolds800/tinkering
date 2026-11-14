// An app is a name, a logo and two functions. The shell in src/shell.cpp
// draws the title bar and handles going back to the home screen.
#pragma once

#include "ui.h"

static const int ICON_SIZE = 76;

struct App {
  const char *name;
  void (*drawIcon)(int x, int y);    // ICON_SIZE square with its top-left at x, y
  void (*enter)();                   // draw the screen below the title bar
  void (*update)(const Touch &touch);  // called continuously while the app is open
};

extern const App calculatorApp;

void drawCalculatorIcon(int x, int y);
void drawMessagesIcon(int x, int y);
void drawDrawingIcon(int x, int y);
void drawGithubIcon(int x, int y);
