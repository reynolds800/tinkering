// The on-screen keyboard, shared by any app that needs typing. It occupies
// the bottom of the screen from keyboard::TOP down. One app uses it at a time.
#pragma once

#include "ui.h"

namespace keyboard {

static const int KEY_H = 40;
static const int TOP = SCREEN_H - 4 * KEY_H;

enum Event {
  NONE,
  TYPED,      // a character was typed; it is passed back through `typed`
  BACKSPACE,
  ACTION,     // the coloured key at the bottom right ("Send", "Go", ...)
};

// Draws the keyboard, on its letters page. `capitalise` starts with shift on.
void open(const char *actionLabel, bool capitalise);

// Turns shift on or off. Shift switches itself off after one letter.
void setShift(bool on);

// Call from the app's update function. Reports what, if anything, was pressed.
Event update(const Touch &touch, char *typed);

}  // namespace keyboard
