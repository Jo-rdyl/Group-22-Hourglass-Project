#include <LedControl.h>

// ================= Pin Configuration =================

const byte DIN_PIN = 11;
const byte CLK_PIN = 13;
const byte CS_PIN  = 7;

// Each button connects its pin to GND when pressed.
const byte TENS_BUTTON_PIN = 2;
const byte ONES_BUTTON_PIN = 4;

LedControl matrix(DIN_PIN, CLK_PIN, CS_PIN, 2);

// The matrix closest to the Arduino is device 0.
// According to the wiring diagram, the lower matrix is 0
// and the upper matrix is 1.
const byte LOWER_MATRIX = 0;
const byte UPPER_MATRIX = 1;

// If the upper and lower matrices are reversed,
// swap the two device numbers above.

// If the sand moves in the wrong direction on either matrix,
// change the corresponding value to true.
const bool UPPER_FLIP_180 = false;
const bool LOWER_FLIP_180 = false;

// ================= Animation Speed =================

// Grain spacing is calculated from the selected number of minutes.

// Speed of the falling-grain animation.
// A smaller value makes the grain fall faster.
const unsigned int FALL_FRAME_MS = 60;

// How long to wait before automatically restarting
// after all the sand has finished falling.
const unsigned int FINISHED_PAUSE_MS = 2500;

// ================= Time Setting =================

const unsigned int BUTTON_DEBOUNCE_MS = 30;
const unsigned int CONFIRM_HOLD_MS = 1000;
const unsigned int DIGIT_FRAME_MS = 30;

// Upper screen = tens; lower screen = ones. Range: 00-99 minutes.
byte selectedMinutes = 0;
byte timeDigits[2] = {0, 0};
byte previousDigits[2] = {0, 0};
bool digitRolling[2] = {false, false};
unsigned long digitStartedAt[2] = {0, 0};
unsigned long runStartedAt = 0;
unsigned long runDurationMs = 0;

// Diamond-mounted digit font: (0,0) is the top tip, (7,7) the bottom.
// Horizontal strokes follow row+column=constant; vertical strokes run
// along the (1,1) direction, just like the falling sand.
// These are hand-mapped seven-segment digits, avoiding rotation rounding.
const byte DIGIT_FONT[10][8] = {
  {0x00, 0x30, 0x48, 0x44, 0x22, 0x12, 0x0C, 0x00}, // 0
  {0x00, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00}, // 1
  {0x00, 0x30, 0x48, 0x08, 0x10, 0x12, 0x0C, 0x00}, // 2
  {0x00, 0x30, 0x48, 0x0C, 0x12, 0x02, 0x04, 0x00}, // 3
  {0x00, 0x10, 0x08, 0x4C, 0x32, 0x00, 0x00, 0x00}, // 4
  {0x00, 0x20, 0x40, 0x4C, 0x32, 0x02, 0x04, 0x00}, // 5
  {0x00, 0x20, 0x40, 0x4C, 0x32, 0x12, 0x0C, 0x00}, // 6
  {0x00, 0x30, 0x48, 0x04, 0x02, 0x00, 0x00, 0x00}, // 7
  {0x00, 0x30, 0x48, 0x4C, 0x32, 0x12, 0x0C, 0x00}, // 8
  {0x00, 0x30, 0x48, 0x4C, 0x32, 0x02, 0x04, 0x00} // 9
};

// ================= Hourglass Data =================

byte upperGrains = 64;
byte lowerGrains = 0;

// Stores the order in which grains are placed on the matrix.
byte pileRows[64];
byte pileColumns[64];

// ================= Basic Display Functions =================

void setSandLed(byte device,
                byte row,
                byte column,
                bool state,
                bool flip180) {
  if (flip180) {
    row = 7 - row;
    column = 7 - column;
  }

  matrix.setLed(device, row, column, state);
}

void addTimeDigit(byte frame[8], byte value, int offset) {
  // Moving both row and column makes the roll physically vertical on
  // the diamond-mounted display, rather than along a sloping board edge.
  for (byte row = 0; row < 8; row++) {
    int destinationRow = row + offset;
    if (destinationRow < 0 || destinationRow > 7) continue;
    for (byte column = 0; column < 8; column++) {
      if (!(DIGIT_FONT[value][row] & (1 << (7 - column)))) continue;
      int destinationColumn = column + offset;
      if (destinationColumn >= 0 && destinationColumn < 8) {
        frame[destinationRow] |= 1 << (7 - destinationColumn);
      }
    }
  }
}

void drawTimeDigit(byte device, byte index, bool flip180) {
  byte frame[8] = {0};
  if (digitRolling[index]) {
    unsigned long step = (millis() - digitStartedAt[index]) / DIGIT_FRAME_MS;
    if (step >= 8) {
      digitRolling[index] = false;
      addTimeDigit(frame, timeDigits[index], 0);
    } else {
      addTimeDigit(frame, previousDigits[index], -int(step));
      addTimeDigit(frame, timeDigits[index], 8 - int(step));
    }
  } else {
    addTimeDigit(frame, timeDigits[index], 0);
  }

  for (byte row = 0; row < 8; row++) {
    byte pixels = frame[row];
    if (flip180) {
      pixels = 0;
      byte source = frame[row];
      for (byte bit = 0; bit < 8; bit++) {
        pixels = (pixels << 1) | (source & 1);
        source >>= 1;
      }
    }
    matrix.setRow(device, flip180 ? 7 - row : row, pixels);
  }
}

void drawSelectedTime() {
  drawTimeDigit(UPPER_MATRIX, 0, UPPER_FLIP_180);
  drawTimeDigit(LOWER_MATRIX, 1, LOWER_FLIP_180);
}

void incrementTimeDigit(byte index) {
  previousDigits[index] = timeDigits[index];
  timeDigits[index] = (timeDigits[index] + 1) % 10;
  digitStartedAt[index] = millis();
  digitRolling[index] = true;
  selectedMinutes = timeDigits[0] * 10 + timeDigits[1];
}

// Enter time setting at boot. Each released button increments one digit.
// Hold both buttons for one second to confirm a nonzero time.
void setTime() {
  const byte pins[2] = {TENS_BUTTON_PIN, ONES_BUTTON_PIN};
  bool lastRaw[2] = {HIGH, HIGH};
  bool stable[2] = {HIGH, HIGH};
  bool armed[2] = {false, false};
  unsigned long changedAt[2] = {millis(), millis()};
  bool confirming = false;
  bool suppressRelease = false;
  unsigned long confirmStartedAt = 0;

  selectedMinutes = 0;
  for (byte i = 0; i < 2; i++) {
    timeDigits[i] = 0;
    previousDigits[i] = 0;
    digitRolling[i] = false;
  }
  drawSelectedTime();

  while (true) {
    unsigned long now = millis();
    bool released[2] = {false, false};

    for (byte i = 0; i < 2; i++) {
      bool raw = digitalRead(pins[i]);
      if (raw != lastRaw[i]) {
        lastRaw[i] = raw;
        changedAt[i] = now;
      }
      if (raw != stable[i] && now - changedAt[i] >= BUTTON_DEBOUNCE_MS) {
        stable[i] = raw;
        if (stable[i] == LOW) {
          armed[i] = true;
        } else {
          released[i] = armed[i];
          armed[i] = false;
        }
      }
    }

    if (stable[0] == LOW && stable[1] == LOW) {
      suppressRelease = true;
      if (!confirming) {
        confirming = true;
        confirmStartedAt = now;
      }
      if (selectedMinutes > 0 && now - confirmStartedAt >= CONFIRM_HOLD_MS) {
        // Do not carry a held confirmation button into the running mode.
        while (digitalRead(pins[0]) == LOW || digitalRead(pins[1]) == LOW) {
          drawSelectedTime();
          delay(2);
        }
        runDurationMs = (unsigned long)selectedMinutes * 60000UL;
        return;
      }
    } else {
      confirming = false;
    }

    if (!suppressRelease) {
      for (byte i = 0; i < 2; i++) {
        if (released[i]) incrementTimeDigit(i);
      }
    }
    if (stable[0] == HIGH && stable[1] == HIGH) {
      suppressRelease = false;
    }

    drawSelectedTime();
    delay(2);
  }
}

// Creates the order in which grains pile up
// from the bottom corner towards the top.
//
// When the matrix is mounted diagonally,
// a larger row + column value means that the LED
// is closer to the bottom corner.
void createPileOrder() {
  byte index = 0;

  // Start at the lowest corner and build the pile
  // upwards one diagonal layer at a time.
  for (int diagonal = 14; diagonal >= 0; diagonal--) {
    // Within each layer, fill from the centre
    // towards the two sides.
    for (int distance = 0; distance <= 7; distance++) {
      for (int row = 0; row < 8; row++) {
        int column = diagonal - row;

        if (column < 0 || column > 7) {
          continue;
        }

        if (abs(row - column) == distance) {
          pileRows[index] = row;
          pileColumns[index] = column;
          index++;
        }
      }
    }
  }
}

// Checks whether a given position is already occupied
// by a settled grain of sand.
bool positionHasSand(byte grainCount,
                     byte row,
                     byte column) {
  for (byte i = 0; i < grainCount; i++) {
    if (pileRows[i] == row &&
        pileColumns[i] == column) {
      return true;
    }
  }

  return false;
}

// Draws a settled pile of sand on one matrix.
void drawPile(byte device,
              byte grainCount,
              bool flip180) {
  matrix.clearDisplay(device);

  for (byte i = 0; i < grainCount; i++) {
    setSandLed(
      device,
      pileRows[i],
      pileColumns[i],
      true,
      flip180
    );
  }
}

// Refreshes both matrices.
void drawHourglass() {
  drawPile(
    UPPER_MATRIX,
    upperGrains,
    UPPER_FLIP_180
  );

  drawPile(
    LOWER_MATRIX,
    lowerGrains,
    LOWER_FLIP_180
  );
}

// ================= Hourglass Animation =================

void transferOneGrain() {
  if (upperGrains == 0) {
    return;
  }

  // Remove one grain from the upper matrix.
  upperGrains--;

  drawHourglass();

  // Animate the grain falling in a straight line
  // through the lower matrix.
  //
  // When the matrix is mounted diagonally,
  // the line from (0,0) to (7,7) appears vertical.
  for (byte position = 0; position < 8; position++) {
    // Stop falling when the grain reaches
    // the settled pile below.
    if (positionHasSand(
          lowerGrains,
          position,
          position
        )) {
      break;
    }

    // Redraw both piles to remove the falling grain
    // from the previous animation frame.
    drawHourglass();

    // Draw the falling grain at its current position.
    setSandLed(
      LOWER_MATRIX,
      position,
      position,
      true,
      LOWER_FLIP_180
    );

    delay(FALL_FRAME_MS);
  }

  // Add the grain to the lower settled pile.
  lowerGrains++;

  drawHourglass();

  // The main loop schedules grains against the selected total duration.
}

void resetHourglass() {
  upperGrains = 64;
  lowerGrains = 0;

  drawHourglass();
  runStartedAt = millis();
}

// ================= Arduino Main Program =================

void setup() {
  pinMode(TENS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ONES_BUTTON_PIN, INPUT_PULLUP);
  createPileOrder();

  for (byte device = 0; device < 2; device++) {
    // Wake the MAX7219 from shutdown mode.
    matrix.shutdown(device, false);

    // Brightness range: 0 to 15.
    // Start with a low value to reduce power consumption.
    matrix.setIntensity(device, 2);

    matrix.clearDisplay(device);
  }

  setTime();
  resetHourglass();
}

void loop() {
  if (upperGrains > 0) {
    // Make each grain LAND at its scheduled time, including its animation.
    byte fallFrames = 0;
    while (fallFrames < 8 &&
           !positionHasSand(lowerGrains, fallFrames, fallFrames)) {
      fallFrames++;
    }
    unsigned long fallDuration = (unsigned long)fallFrames * FALL_FRAME_MS;
    unsigned long landingTime = runDurationMs * (lowerGrains + 1UL) / 64UL;
    unsigned long elapsed = millis() - runStartedAt;
    if (elapsed >= landingTime - fallDuration) {
      transferOneGrain();
    } else {
      delay(1);
    }
  } else {
    // Wait after all the sand has finished falling.
    delay(FINISHED_PAUSE_MS);

    // Refill the upper matrix and start again automatically.
    resetHourglass();
  }
}
