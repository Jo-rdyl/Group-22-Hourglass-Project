#include <LedControl.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

int xpos = 0;

// ================= Pin Configuration =================

const byte DIN_PIN = 11;
const byte CLK_PIN = 13;
const byte CS_PIN  = 7;

LedControl matrix(DIN_PIN, CLK_PIN, CS_PIN, 2);

// The matrix closest to the Arduino is device 0.
// According to the wiring diagram, the lower matrix is 0
// and the upper matrix is 1.
byte LOWER_MATRIX = 0;
byte UPPER_MATRIX = 1;
bool rotated = false;


// If the upper and lower matrices are reversed,
// swap the two device numbers above.

// If the sand moves in the wrong direction on either matrix,
// change the corresponding value to true.
const bool UPPER_FLIP_180 = false;
const bool LOWER_FLIP_180 = false;

// ================= Animation Speed =================

// Delay between each grain transfer.
// A smaller value makes the hourglass run faster.
const unsigned int GRAIN_INTERVAL_MS = 350;

// Speed of the falling-grain animation.
// A smaller value makes the grain fall faster.
const unsigned int FALL_FRAME_MS = 60;

// How long to wait before automatically restarting
// after all the sand has finished falling.
const unsigned int FINISHED_PAUSE_MS = 2500;

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

void flipAndReset() {
  byte temp = UPPER_MATRIX;
  UPPER_MATRIX = LOWER_MATRIX;
  LOWER_MATRIX = temp;

  resetHourglass();   // full pile on the new top matrix, empty bottom
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

  delay(GRAIN_INTERVAL_MS);
}

void resetHourglass() {
  upperGrains = 64;
  lowerGrains = 0;

  drawHourglass();
}

void initializeMPU6050() {
  // Check if the MPU6050 sensor is detected
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1)
      ;  // Halt if sensor not found
  }
  Serial.println("MPU6050 Found!");

  // set accelerometer range to +-8G
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  // set gyro range to +- 500 deg/s
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);

  // set filter bandwidth to 21 Hz
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("");
  delay(100);
}

int tilted() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  int orientation = (a.acceleration.x <= -3) ? -9 : (a.acceleration.x > 3) ? 9 : 0;

  if (orientation == 0) {
    return 0;
  } else if (orientation == xpos) {
    return (-1);
  }else {
    xpos = orientation;
    return 1;
  }
}

void setup() {
  createPileOrder();
  Serial.begin(9600);

  for (byte device = 0; device < 2; device++) {
    matrix.shutdown(device, false);
    matrix.setIntensity(device, 8);
    matrix.clearDisplay(device);
  }

  resetHourglass();         // draw first so you can see the display works

  initializeMPU6050();      // start the sensor before reading it

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  int orientation = (a.acceleration.x <= -3) ? -9 : (a.acceleration.x > 3) ? 9 : 0;
  xpos = orientation;
}

void loop() {
  int state = tilted();

  if (state == 1) {
    flipAndReset();
  } else if (state == 0) {
    delay(50);                  // board is flat: pause
  } else {
    if (upperGrains > 0) {
      transferOneGrain();
    } else {
      delay(FINISHED_PAUSE_MS);
    }
  }
}