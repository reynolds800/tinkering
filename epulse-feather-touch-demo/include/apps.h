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

  // Optional; leave out any that are not needed.
  void (*leave)();       // the app is being closed
  void (*background)();  // called continuously, whether or not the app is open
  int (*badge)();        // number to show on the home screen logo, 0 for none
};

extern const App calculatorApp;
extern const App messagesApp;
extern const App drawingApp;
extern const App githubApp;
extern const App weatherApp;
extern const App calendarApp;
extern const App worldClockApp;
extern const App mapsApp;
extern const App doublesApp;
extern const App timerApp;

// Brings an app to the front, closing whichever is open. For an app that
// needs attention while it is not on show, such as a timer finishing.
void shellOpen(const App &app);

void drawCalculatorIcon(int x, int y);
void drawMessagesIcon(int x, int y);
void drawDrawingIcon(int x, int y);
void drawGithubIcon(int x, int y);
void drawWeatherIcon(int x, int y);
void drawCalendarIcon(int x, int y);
void drawWorldClockIcon(int x, int y);
void drawMapsIcon(int x, int y);
void drawDoublesIcon(int x, int y);
void drawTimerIcon(int x, int y);
