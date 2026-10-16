const int SPEAKER_PIN = 9`;

// Note frequencies in Hz
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784

int melody[] = {
  NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_C5, NOTE_B4,                // Happy birthday to you
  NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_D5, NOTE_C5,                // Happy birthday to you
  NOTE_G4, NOTE_G4, NOTE_G5, NOTE_E5, NOTE_C5, NOTE_B4, NOTE_A4,       // Happy birthday dear ...
  NOTE_F5, NOTE_F5, NOTE_E5, NOTE_C5, NOTE_D5, NOTE_C5                 // Happy birthday to you
};

// 8 = eighth note, 4 = quarter, 2 = half
int noteDurations[] = {
  8, 8, 4, 4, 4, 2,
  8, 8, 4, 4, 4, 2,
  8, 8, 4, 4, 4, 4, 2,
  8, 8, 4, 4, 4, 2
};

void playMelody() {
  int count = sizeof(melody) / sizeof(melody[0]);
  for (int i = 0; i < count; i++) {
    int duration = 1000 / noteDurations[i];
    tone(SPEAKER_PIN, melody[i], duration);
    delay(duration * 1.3);          // small gap so repeated notes sound separate
    noTone(SPEAKER_PIN);
  }
}

void setup() {
  playMelody();
}

void loop() {}