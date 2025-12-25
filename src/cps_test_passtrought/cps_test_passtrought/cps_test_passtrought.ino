#include <Audio.h>
#include <Wire.h>
#include <SPI.h>

// Audio objects
AudioInputI2S        in;        // LINE IN
AudioMixer4          mixL;      // mixeur gauche
AudioMixer4          mixR;      // mixeur droit (sera muet)
AudioOutputI2S       out;       // LINE OUT

// Connexions
AudioConnection pc1(in, 0, mixL, 0);   // LEFT -> LEFT
AudioConnection pc2(mixL, 0, out, 0);  // LEFT -> LINE OUT LEFT
AudioConnection pc3(mixR, 0, out, 1);  // RIGHT -> LINE OUT RIGHT (muet)

AudioControlSGTL5000 codec;

void setup() {
  AudioMemory(12);

  codec.enable();

  // Utiliser uniquement LINE IN (micro non sélectionné)
  codec.inputSelect(AUDIO_INPUT_LINEIN);

  // Pas de gain
  codec.lineInLevel(0);

  // LINE OUT actif (niveau fixe)
  codec.lineOutLevel(13);

  // Casque désactivé
  codec.volume(0.0f);

  // Mixer LEFT : gain 1.0
  mixL.gain(0, 1.0f);

  // Mixer RIGHT : tout à zéro => silence garanti
  for (int i = 0; i < 4; i++) {
    mixR.gain(i, 0.0f);
  }
}

void loop() {
  // passthrough matériel
}
