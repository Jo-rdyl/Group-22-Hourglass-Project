#include <LedControl.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <math.h>

Adafruit_MPU6050 mpu;

// ================= Pin Configuration =================

const byte DIN_PIN = 11;
const byte CLK_PIN = 12;
const byte CS_PIN  = 13;

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

// ================= Sand Gravity =================

// X is the existing upright/flip axis. Choose the other axis that lies
// across the display: change to true if the sensor is mounted in the X/Z plane.
const bool GRAVITY_SIDE_USES_Z = false;
// Change to -1.0f if the sand slides opposite to the physical tilt.
const float GRAVITY_SIDE_SIGN = 1.0f;
const unsigned int SENSOR_SAMPLE_MS = 20;
const unsigned int SAND_FRAME_MS = FALL_FRAME_MS;
const float GRAVITY_FILTER = 0.22f;
const float GRAVITY_DEAD_ZONE = 0.12f;  // projected gravity, in g

float sensorX = 9.81f;
float filteredX = 9.81f;
float filteredSide = 0.0f;
float initialUprightSign = 1.0f;
bool sensorSampled = false;
unsigned long sensorSampleAt = 0;
unsigned long sandFrameAt = 0;
bool sandStepParity = false;
// One bit per LED. Grains keep their positions between animation frames.
byte upperSand[8] = {0};
byte lowerSand[8] = {0};

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

// Cache the sensor so buttons, flip detection and gravity can all update
// frequently without repeatedly reading I2C within the same frame.
void sampleGravity() {
  if (!mpuFound) return;
  unsigned long now = millis();
  if (sensorSampled && now - sensorSampleAt < SENSOR_SAMPLE_MS) return;
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  sensorX = a.acceleration.x;
  float side = GRAVITY_SIDE_USES_Z ? a.acceleration.z : a.acceleration.y;
  if (!sensorSampled) {
    filteredX = sensorX;
    filteredSide = side;
  } else {
    filteredX += GRAVITY_FILTER * (sensorX - filteredX);
    filteredSide += GRAVITY_FILTER * (side - filteredSide);
  }
  sensorSampleAt = now;
  sensorSampled = true;
}

// Raw position from the x-axis: 9 or -9 = upright / upside down, 0 = on its side.
int rawOrientation() {
  if (!mpuFound) return 9;
  sampleGravity();
  return (sensorX <= -3) ? -9 : (sensorX > 3) ? 9 : 0;
}

// Takes however it is sitting right now as "normal".
void startOrientation() {
  xpos = stableOrientation = candidateOrientation = rawOrientation();
  candidateSince = millis();
  if (xpos != 0) initialUprightSign = xpos < 0 ? -1.0f : 1.0f;
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
    initialUprightSign = (xpos < 0 ? -1.0f : 1.0f) * (rotated ? -1.0f : 1.0f);
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

// ================= Gravity Simulation =================

bool sandAt(const byte grid[8], int row, int column) {
  return (grid[row] & (1 << column)) != 0;
}

void setSandAt(byte grid[8], int row, int column, bool on) {
  if (on) grid[row] |= 1 << column;
  else grid[row] &= ~(1 << column);
}

// Convert screen vertical/horizontal into the two axes of a diamond matrix.
// The same rotation used by setSandLed must also be applied to gravity.
void getSandGravity(float &rowGravity, float &columnGravity) {
  if (!mpuFound) {
    rowGravity = columnGravity = 0.7071f;
    return;
  }
  sampleGravity();
  float direction = initialUprightSign * (rotated ? -1.0f : 1.0f);
  float vertical = filteredX * direction / 9.81f;
  float horizontal = filteredSide * direction * GRAVITY_SIDE_SIGN / 9.81f;
  rowGravity = (vertical + horizontal) * 0.7071f;
  columnGravity = (vertical - horizontal) * 0.7071f;
}

bool stepSand(byte grid[8], float rowGravity, float columnGravity) {
  // A face-up display has almost no gravity in its plane. Keep the grains still.
  if (fabsf(rowGravity) + fabsf(columnGravity) < GRAVITY_DEAD_ZONE) return false;

  const int8_t rows[8] = {1, 0, -1, 0, 1, 1, -1, -1};
  const int8_t columns[8] = {0, 1, 0, -1, 1, -1, 1, -1};
  byte order[8];
  float scores[8];
  for (byte i = 0; i < 8; i++) {
    // Alternate tied choices so a vertical pile does not favour one side.
    order[i] = sandStepParity ? 7 - i : i;
    byte d = order[i];
    scores[i] = (rows[d] * rowGravity + columns[d] * columnGravity)
                * (d < 4 ? 1.0f : 0.7071f);
  }
  for (byte i = 1; i < 8; i++) {
    byte d = order[i];
    float score = scores[i];
    int j = i - 1;
    while (j >= 0 && scores[j] < score) {
      scores[j + 1] = scores[j];
      order[j + 1] = order[j];
      j--;
    }
    scores[j + 1] = score;
    order[j + 1] = d;
  }

  byte moved[8] = {0};
  bool changed = false;
  // Visit the downstream edge first; each grain moves at most one LED per tick.
  for (int ri = 0; ri < 8; ri++) {
    int row = rowGravity >= 0 ? 7 - ri : ri;
    for (int ci = 0; ci < 8; ci++) {
      int column = columnGravity >= 0 ? 7 - ci : ci;
      if (!sandAt(grid, row, column) || sandAt(moved, row, column)) continue;
      for (byte i = 0; i < 8; i++) {
        // Never slide level or uphill. This prevents a settled pile jittering.
        if (scores[i] < 0.04f) break;
        byte d = order[i];
        int nextRow = row + rows[d];
        int nextColumn = column + columns[d];
        if (nextRow < 0 || nextRow > 7 || nextColumn < 0 || nextColumn > 7) continue;
        if (sandAt(grid, nextRow, nextColumn)) continue;
        // A diagonal move cannot pass through two touching occupied cells.
        if (rows[d] && columns[d] &&
            sandAt(grid, row, nextColumn) && sandAt(grid, nextRow, column)) continue;
        setSandAt(grid, row, column, false);
        setSandAt(grid, nextRow, nextColumn, true);
        setSandAt(moved, nextRow, nextColumn, true);
        changed = true;
        break;
      }
    }
  }
  return changed;
}

void drawSandGrid(byte device, const byte grid[8], bool flip180) {
  bool reverse = flip180 != rotated;
  for (byte row = 0; row < 8; row++) {
    byte pixels = 0;
    for (byte column = 0; column < 8; column++) {
      if (sandAt(grid, row, column)) pixels |= 1 << (reverse ? column : 7 - column);
    }
    matrix.setRow(device, reverse ? 7 - row : row, pixels);
  }
}

void drawHourglass() {
  drawSandGrid(UPPER_MATRIX, upperSand, UPPER_FLIP_180);
  drawSandGrid(LOWER_MATRIX, lowerSand, LOWER_FLIP_180);
}

void updateSandGravity() {
  unsigned long now = millis();
  if (now - sandFrameAt < SAND_FRAME_MS) return;
  sandFrameAt = now;
  float rowGravity, columnGravity;
  getSandGravity(rowGravity, columnGravity);
  bool upperChanged = stepSand(upperSand, rowGravity, columnGravity);
  bool lowerChanged = stepSand(lowerSand, rowGravity, columnGravity);
  sandStepParity = !sandStepParity;
  if (upperChanged || lowerChanged) drawHourglass();
}

// Locate the available grain/cell nearest the neck. Grains in a tilted pile
// are no longer stored in the fixed boot-animation pile order.
int neckCell(const byte grid[8], bool occupied, bool upper) {
  int best = -1;
  int bestScore = -1000;
  for (int row = 0; row < 8; row++) {
    for (int column = 0; column < 8; column++) {
      if (sandAt(grid, row, column) != occupied) continue;
      int score = (upper ? row + column : 14 - row - column) * 16 - abs(row - column);
      if (score > bestScore) {
        bestScore = score;
        best = row * 8 + column;
      }
    }
  }
  return best;
}

// ================= Hourglass Animation =================

void returnToTimeSetting();
void flipAndReset();

void transferOneGrain() {
  if (upperGrains == 0) return;
  int source = neckCell(upperSand, true, true);
  int destination = neckCell(lowerSand, false, false);
  if (source < 0 || destination < 0) return;

  setSandAt(upperSand, source / 8, source % 8, false);
  setSandAt(lowerSand, destination / 8, destination % 8, true);
  upperGrains--;
  lowerGrains++;
  drawHourglass();

  // Reserve a fixed falling window so each scheduled grain finishes at the
  // selected time. Both piles and the new grain follow live gravity throughout.
  unsigned long fallStartedAt = millis();
  const unsigned long fallingMs = 8UL * FALL_FRAME_MS;
  while (millis() - fallStartedAt < fallingMs) {
    updateButtons();
    if (buttonPressed[2]) {
      returnToTimeSetting();
      return;
    }
    int orientation = tilted();
    if (orientation == 1) {
      flipAndReset();
      return;
    }
    if (orientation == 0) {
      unsigned long pauseStartedAt = millis();
      // Pause the timer and keep the sand moving, just as in the main loop.
      // Finish this falling window after resuming instead of losing a grain.
      while (orientation == 0) {
        updateButtons();
        if (buttonPressed[2]) {
          returnToTimeSetting();
          return;
        }
        updateSandGravity();
        delay(2);
        orientation = tilted();
        unsigned long now = millis();
        unsigned long paused = now - pauseStartedAt;
        runStartedAt += paused;
        fallStartedAt += paused;
        pauseStartedAt = now;
      }
      if (orientation == 1) {
        flipAndReset();
        return;
      }
    }
    updateSandGravity();
    delay(2);
  }
}

void resetHourglass() {
  upperGrains = 64;
  lowerGrains = 0;
  for (byte row = 0; row < 8; row++) {
    upperSand[row] = 0xFF;
    lowerSand[row] = 0;
  }
  sandFrameAt = millis();

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
    updateSandGravity();  // the timer pauses, but loose sand still follows tilt
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

  updateSandGravity();
  updateButtons();
  if (upperGrains > 0 && buttonPressed[2]) {
    returnToTimeSetting();
    return;
  }
  if (upperGrains > 0) {
    // Make each grain LAND at its scheduled time, including its animation.
    const unsigned long fallDuration = 8UL * FALL_FRAME_MS;
    unsigned long landingTime = runDurationMs * (lowerGrains + 1UL) / 64UL;
    unsigned long elapsed = millis() - runStartedAt;
    unsigned long releaseTime = landingTime > fallDuration ? landingTime - fallDuration : 0;
    if (elapsed >= releaseTime) {
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
