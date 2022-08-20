int firstBeat = 700;
int secondBeat = 350;
Timer beat = {firstBeat};

void heartbeat() {
  if (beat.complete()) {
    for (uint16_t i = 0; i < 100; i++) {
      leds[i] = knobColor;
    }
    beat.totalCycleTime =
        beat.totalCycleTime == firstBeat ? secondBeat : firstBeat;
    beat.reset();
  }
}
