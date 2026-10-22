#include <LedControl.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

// ================= Pin Configuration =================

const byte DIN_PIN = 11;
const byte CLK_PIN = 13;
const byte CS_PIN  = 7;

// Each button connects its pin to GND when pressed.
const byte TENS_BUTTON_PIN = 2;
const byte ONES_BUTTON_PIN = 3;
const byte CONFIRM_BUTTON_PIN = 4;

LedControl matrix(DIN_PIN, CLK_PIN, CS_PIN, 2);

// The matrix closest to the Arduino is device 0.
// According to the wiring diagram, the lower matrix is 0
// and the upper matrix is 1.
// These are NOT const: they swap when the hourglass is turned over, so the
// matrix that is physically on top is always UPPER_MATRIX.
byte LOWER_MATRIX = 1;
byte UPPER_MATRIX = 0;

// True while the hourglass is turned over (everything is drawn rotated 180 degrees).
bool rotated = false;

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

// Speaker (audio module signal goes through the 10K resistor to L or R).
const byte SPEAKER_PIN = 9;

// Boot animation timing (all in milliseconds).
const unsigned int BOOT_FILL_MS = 12;    // delay between grains filling the top
const unsigned int BOOT_STREAM_MS = 40;  // delay between steps of the falling stream
const unsigned int BOOT_HOLD_MS = 300;   // pause before the screen is wiped
const unsigned int BOOT_WIPE_MS = 40;    // delay between steps of the final wipe

// ================= Orientation (MPU6050) =================

// A new position must hold this long before it counts. This stops a knock
// or a shake from being mistaken for a flip (which would restart the timer).
const unsigned int ORIENTATION_SETTLE_MS = 150;

bool mpuFound = false;
int xpos = 0;                  // last upright position seen: -9 or 9 (0 = not known yet)
int stableOrientation = 0;     // filtered position: -9, 9, or 0 (on its side)
int candidateOrientation = 0;  // position being timed before it is accepted
unsigned long candidateSince = 0;

// ================= Time Setting =================

const unsigned int BUTTON_DEBOUNCE_MS = 30;
const unsigned int DIGIT_FRAME_MS = 30;

// Button press sounds: one short blip per press, a different pitch per button.
// tone() with a duration returns immediately, so it never slows down the
// buttons or the sand animation.
const unsigned int BUTTON_TONES[3] = {523, 659, 1047};  // tens = C5, ones = E5, confirm = C6
const unsigned int BUTTON_BEEP_MS = 50;

// Upper screen = tens; lower screen = ones. Range: 00-99 minutes.
byte selectedMinutes = 0;
byte timeDigits[2] = {0, 0};
byte previousDigits[2] = {0, 0};
bool digitRolling[2] = {false, false};
unsigned long digitStartedAt[2] = {0, 0};
unsigned long runStartedAt = 0;
unsigned long runDurationMs = 0;

// Share button states between setting and running, so holding D4 cannot
// immediately trigger a second action when the mode changes.
const byte BUTTON_PINS[3] = {TENS_BUTTON_PIN, ONES_BUTTON_PIN, CONFIRM_BUTTON_PIN};
bool buttonRaw[3] = {HIGH, HIGH, HIGH};
bool buttonStable[3] = {HIGH, HIGH, HIGH};
bool buttonArmed[3] = {false, false, false};
bool buttonPressed[3] = {false, false, false};
bool buttonReleased[3] = {false, false, false};
unsigned long buttonChangedAt[3] = {0, 0, 0};

// Diamond-mounted digit font: (0,0) is the top tip, (7,7) the bottom.
// Horizontal strokes follow row+column=constant; vertical strokes run
// along the (1,1) direction, just like the falling sand.
// These are hand-mapped seven-segment digits, avoiding rotation rounding.
const byte DIGIT_FONT[10][8] = {
  {0x00, 0x30, 0x48, 0x44, 0x22, 0x12, 0x0C, 0x00}, // 0
  {0x00, 0x00, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00}, // 1: centered along row == column
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
  if (flip180 != rotated) {
    row = 7 - row;
    column = 7 - column;
  }

  matrix.setLed(device, row, column, state);
}

// ================= Orientation Functions =================

void initializeMPU6050() {
  if (!mpu.begin()) {
    // Carry on without it: the hourglass still runs, it just can't pause or flip.
    Serial.println("MPU6050 not found - running without tilt control");
    return;
  }
  mpuFound = true;
  Serial.println("MPU6050 found");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  delay(100);
}

// Raw position from the x-axis: 9 or -9 = upright / upside down, 0 = on its side.
int rawOrientation() {
  if (!mpuFound) return 9;   // no sensor: behave as if it is always upright
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  return (a.acceleration.x <= -3) ? -9 : (a.acceleration.x > 3) ? 9 : 0;
}

// Takes however it is sitting right now as "normal".
void startOrientation() {
  xpos = stableOrientation = candidateOrientation = rawOrientation();
  candidateSince = millis();
}

// Returns  0 = on its side (pause),
//         -1 = same way up as before (keep running),
//          1 = just turned over (flip).
int tilted() {
  int raw = rawOrientation();
  if (raw != candidateOrientation) {
    candidateOrientation = raw;
    candidateSince = millis();
  }
  if (millis() - candidateSince >= ORIENTATION_SETTLE_MS) {
    stableOrientation = candidateOrientation;
  }

  if (stableOrientation == 0) return 0;      // on its side
  if (xpos == 0) {                           // first upright reading: just remember it
    xpos = stableOrientation;
    return -1;
  }
  if (stableOrientation == xpos) return -1;  // same way up as before
  xpos = stableOrientation;
  return 1;                                  // turned over
}

// Turning the hourglass over turns each matrix upside down and swaps which
// one is on top, so swap the matrix numbers and rotate the drawing 180 degrees.
void flipDisplay() {
  byte swapped = UPPER_MATRIX;
  UPPER_MATRIX = LOWER_MATRIX;
  LOWER_MATRIX = swapped;
  rotated = !rotated;
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
  drawTimeDigit(UPPER_MATRIX, 0, UPPER_FLIP_180 != rotated);
  drawTimeDigit(LOWER_MATRIX, 1, LOWER_FLIP_180 != rotated);
}

void incrementTimeDigit(byte index) {
  previousDigits[index] = timeDigits[index];
  timeDigits[index] = (timeDigits[index] + 1) % 10;
  digitStartedAt[index] = millis();
  digitRolling[index] = true;
  selectedMinutes = timeDigits[0] * 10 + timeDigits[1];
}

void updateButtons() {
  unsigned long now = millis();
  for (byte i = 0; i < 3; i++) {
    buttonPressed[i] = false;
    buttonReleased[i] = false;
    bool raw = digitalRead(BUTTON_PINS[i]);
    if (raw != buttonRaw[i]) {
      buttonRaw[i] = raw;
      buttonChangedAt[i] = now;
    }
    if (raw != buttonStable[i] && now - buttonChangedAt[i] >= BUTTON_DEBOUNCE_MS) {
      buttonStable[i] = raw;
      if (raw == LOW) {
        buttonPressed[i] = true;
        buttonArmed[i] = true;
        tone(SPEAKER_PIN, BUTTON_TONES[i], BUTTON_BEEP_MS);  // press sound
      } else {
        buttonReleased[i] = buttonArmed[i];
        buttonArmed[i] = false;
      }
    }
  }
}

// The same setting screen is used at boot (00) and when D4 stops a run
// (remaining whole minutes, rounded UP so partial minutes are visible).
void setTime(byte initialMinutes) {
  selectedMinutes = initialMinutes;
  timeDigits[0] = initialMinutes / 10;
  timeDigits[1] = initialMinutes % 10;
  for (byte i = 0; i < 2; i++) {
    previousDigits[i] = timeDigits[i];
    digitRolling[i] = false;
    buttonArmed[i] = false;
  }
  drawSelectedTime();

  while (true) {
    updateButtons();
    if (tilted() == 1) flipDisplay();   // turned over: keep the digits the right way up
    for (byte i = 0; i < 2; i++) {
      if (buttonReleased[i]) incrementTimeDigit(i);
    }

    if (buttonPressed[2] && selectedMinutes > 0) {
      runDurationMs = (unsigned long)selectedMinutes * 60000UL;
      return;
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

void returnToTimeSetting();
void flipAndReset();

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

    // Keep D4 responsive even while a grain is falling.
    unsigned long frameStartedAt = millis();
    while (millis() - frameStartedAt < FALL_FRAME_MS) {
      updateButtons();
      if (buttonPressed[2]) {
        returnToTimeSetting();
        return;
      }
      if (tilted() == 1) {   // turned over mid-fall: restart straight away
        flipAndReset();
        return;
      }
      delay(2);
    }
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

void returnToTimeSetting() {
  unsigned long elapsed = millis() - runStartedAt;
  unsigned long remainingMs = elapsed < runDurationMs ? runDurationMs - elapsed : 0;
  byte remainingMinutes = (remainingMs + 59999UL) / 60000UL;
  setTime(remainingMinutes);
  // Reuse the original startup flow: refill and start the selected time.
  resetHourglass();
}

// Turned over: swap and rotate the display, then start the same time again.
void flipAndReset() {
  flipDisplay();
  resetHourglass();
}

// On its side: freeze the countdown until it is picked up again.
void pauseWhileSideways() {
  unsigned long lastTick = millis();
  int state = 0;

  while (state == 0) {
    updateButtons();
    if (upperGrains > 0 && buttonPressed[2]) {   // confirm still lets you change the time
      returnToTimeSetting();
      return;
    }
    delay(5);
    state = tilted();

    // Push the start time forward by the time spent paused, so the sand
    // schedule carries on from exactly where it stopped.
    unsigned long now = millis();
    runStartedAt += now - lastTick;
    lastTick = now;
  }

  if (state == 1) flipAndReset();   // it was turned over while on its side
}

// ================= Boot Animation =================

// Plays once at power-on:
//   1. Sand fills the upper matrix grain by grain.
//   2. A thin stream falls through the lower matrix.
//   3. A diagonal wipe clears both matrices, top tip to bottom tip.
void bootAnimation() {
  matrix.clearDisplay(UPPER_MATRIX);
  matrix.clearDisplay(LOWER_MATRIX);

  // 1. Fill the upper matrix using the same pile order as the hourglass.
  for (byte i = 0; i < 64; i++) {
    setSandLed(UPPER_MATRIX, pileRows[i], pileColumns[i], true, UPPER_FLIP_180);
    delay(BOOT_FILL_MS);
  }

  // 2. A falling stream along the (0,0) to (7,7) line.
  for (byte position = 0; position < 8; position++) {
    setSandLed(LOWER_MATRIX, position, position, true, LOWER_FLIP_180);
    delay(BOOT_STREAM_MS);
  }

  delay(BOOT_HOLD_MS);

  // 3. Wipe both matrices one diagonal at a time (row + column = diagonal).
  for (int diagonal = 0; diagonal <= 14; diagonal++) {
    for (int row = 0; row < 8; row++) {
      int column = diagonal - row;
      if (column < 0 || column > 7) continue;
      setSandLed(UPPER_MATRIX, row, column, false, UPPER_FLIP_180);
      setSandLed(LOWER_MATRIX, row, column, false, LOWER_FLIP_180);
    }
    delay(BOOT_WIPE_MS);
  }

  matrix.clearDisplay(UPPER_MATRIX);
  matrix.clearDisplay(LOWER_MATRIX);
}

// ================= Finished Chime =================

// A short original chime (about 2.4 seconds), all notes from the C major scale:
// a soft rising sparkle, a high note left to ring, then a gentle descent
// that settles on a long low note.
void playFinishedChime() {
  const int rising[3]  = {523, 659, 784};   // C5, E5, G5
  const int falling[3] = {784, 659, 523};   // G5, E5, C5

  // Rising sparkle
  for (byte i = 0; i < 3; i++) {
    tone(SPEAKER_PIN, rising[i], 120);
    delay(150);
  }

  // High C6, held so it rings
  tone(SPEAKER_PIN, 1047, 450);
  delay(520);

  // Gentle descent
  for (byte i = 0; i < 3; i++) {
    tone(SPEAKER_PIN, falling[i], 180);
    delay(220);
  }

  // Settle on a long final C5
  tone(SPEAKER_PIN, 523, 700);
  delay(750);

  noTone(SPEAKER_PIN);
}

// ================= Arduino Main Program =================

void setup() {
  pinMode(TENS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ONES_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CONFIRM_BUTTON_PIN, INPUT_PULLUP);
  for (byte i = 0; i < 3; i++) {
    buttonRaw[i] = HIGH;
    buttonStable[i] = HIGH;
    buttonArmed[i] = false;
    buttonChangedAt[i] = millis();
  }
  Serial.begin(9600);
  createPileOrder();

  for (byte device = 0; device < 2; device++) {
    // Wake the MAX7219 from shutdown mode.
    matrix.shutdown(device, false);

    // Brightness range: 0 to 15.
    // Start with a low value to reduce power consumption.
    matrix.setIntensity(device, 2);

    matrix.clearDisplay(device);
  }

  initializeMPU6050();   // start the tilt sensor
  bootAnimation();       // plays once at power-on
  startOrientation();    // however it is sitting now counts as "normal"
  setTime(0);            // then the user sets a time and presses confirm
  resetHourglass();      // then the hourglass runs
}

void loop() {
  int orientation = tilted();
  if (orientation == 1) {            // turned over: restart with the new way up
    flipAndReset();
    return;
  }
  if (orientation == 0) {            // on its side: pause
    pauseWhileSideways();
    return;
  }

  updateButtons();
  if (upperGrains > 0 && buttonPressed[2]) {
    returnToTimeSetting();
    return;
  }
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
    // All the sand has finished falling: play the chime
    // (it also acts as the pause before the automatic restart).
    playFinishedChime();

    // Refill the upper matrix and start again automatically.
    resetHourglass();
  }
}
