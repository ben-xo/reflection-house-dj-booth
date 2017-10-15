#include <Adafruit_NeoPixel.h>

#define NEOPIXEL_PIN 6
#define BUTTON_PIN 2
#define BUTTON_LED_PIN 13
#define MODE_LED_PIN_1 10
#define MODE_LED_PIN_2 11
#define MODE_LED_PIN_3 12
#define STRIP_LENGTH 60

// modes 0 to MAX_MODE are effects
#define MAX_MODE 6

// Parameter 1 = number of pixels in strip
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(STRIP_LENGTH, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

const int sampleWindow = 10; // Sample window width in mS (50 mS = 20Hz)
unsigned int sample_i;
int maximum = 110;
bool is_beat;
unsigned long time;

uint8_t random_table[STRIP_LENGTH];

// used for auto-mode change
int beat_count = 0;

// used for resetting synchronisation when there's silence
int beatless_count = 0;

// whether or not to automatically change modes. Cancelled by pushing a button to pick a mode
bool auto_mode = true;

// button to change modes. button_was_pushed used to track when button was released.
char mode = 0;
bool button_was_pushed = false;

// Arduino Beat Detector By Damian Peckett 2015
// License: Public Domain.

// Our Global Sample Rate, 5000hz
#define SAMPLEPERIODUS 200

// defines for setting and clearing register bits
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

void setup()
{
  // Set ADC to 77khz, max for 10bit (this is for the beat detector)
  sbi(ADCSRA,ADPS2);
  cbi(ADCSRA,ADPS1);
  cbi(ADCSRA,ADPS0);

  // the pin with the push button
  pinMode(BUTTON_PIN, INPUT);
  
  // the pin with the push-button LED
  pinMode(BUTTON_LED_PIN,OUTPUT);  

  // the pin with the mode display
  pinMode(MODE_LED_PIN_1,OUTPUT);  
  pinMode(MODE_LED_PIN_2,OUTPUT);  
  pinMode(MODE_LED_PIN_3,OUTPUT);  

  // Initialize all pixels to 'off'
  strip.begin();
  strip.show();
  
  time = micros(); // Used to track rate

  generate_random_table();
}


void loop()
{
    float envelope;
    unsigned int peakToPeak;

    check_mode_change_button();

    // 4 loops (~50ms each). VU is on a 50ms loop, beat detection is on a 200ms loop
    // also, do fades every other render
    
    peakToPeak = read_vu_meter_and_beat_envelope(envelope);
    render(peakToPeak, is_beat, true, mode);
    peakToPeak = read_vu_meter_and_beat_envelope(envelope);
    render(peakToPeak, is_beat, false, mode);
    peakToPeak = read_vu_meter_and_beat_envelope(envelope);
    render(peakToPeak, is_beat, true, mode);
    peakToPeak = read_vu_meter_and_beat_envelope(envelope);
    // Every 200 samples (25hz) filter the envelope 
    is_beat = beat_detect(envelope);
    render(peakToPeak, is_beat, false, mode);

    if(auto_mode) {
      if(is_beat) {
        beatless_count = 0;
        beat_count++;
      } else {
        beatless_count++;
      }
  
      // resynchronise the beat count mode change with the song
      if(beatless_count > 32) {
        beat_count = 0;
      }
  
      // change mode after 64 beats
      if(beat_count > 64) {
        mode++;
        if(mode > MAX_MODE) {
          mode = 0;
        }
        beat_count = 0;
      }
    }
 
    for(unsigned long up = time+SAMPLEPERIODUS; up-time > 0 && up-time < 1024; time = micros()) {
      // empty loop, to consume excess clock cycles, to keep at 5000 hz  
    }
}

unsigned int read_vu_meter_and_beat_envelope(float &envelope) {
    float sample, value, thresh;
    
    unsigned long startMillis = millis(); // Start of sample window
    unsigned int peakToPeak = 0;   // peak-to-peak level
  
    unsigned int signalMax = 0;
    unsigned int signalMin = 100;
  
    // This loops collects VU data and does beat detection for 50 mS
    while (millis() - startMillis < sampleWindow)
    {
      sample_i = analogRead(0);
      if (sample_i < 1024)  // toss out spurious readings
      {
        if (sample_i > signalMax)
        {
          signalMax = sample_i;  // save just the max levels
        }
        else if (sample_i < signalMin)
        {
          signalMin = sample_i;  // save just the min levels
        }
      }
      
      // Read ADC and center to +-512
      sample = (float)sample_i-503.f;

      // Filter only bass component
      value = bassFilter(sample);

      // Take signal amplitude and filter
      if(value < 0)value=-value;
      envelope = envelopeFilter(value);
    }
    return signalMax - signalMin;  // max - min = peak-peak amplitude
}

bool beat_detect(float &envelope) {
    float beat, thresh;
    
    // Filter out repeating bass sounds 100 - 180bpm
    beat = beatFilter(envelope);

    // Threshold is based on potentiometer on AN1
    thresh = 0.02f * (float)analogRead(1);

    // If we are above threshold, FOUND A BEAT
    if(beat > thresh) {
      return true;
    }
    return false;
}

void render(unsigned int peakToPeak, bool is_beat, bool do_fade, char mode) {

    switch(mode) {
      case 0:
        render_vu_plus_beat_end(peakToPeak, is_beat, do_fade);
        break;
      case 1:
        render_shoot_pixels(peakToPeak, is_beat, do_fade);
        break;
      case 2:
        render_double_vu(peakToPeak, is_beat, do_fade);
        break;
      case 3:
        render_vu_plus_beat_interleave(peakToPeak, is_beat, do_fade);
        break;
      case 4:
        render_double_vu_fade_2(peakToPeak, is_beat, do_fade);
        break;
      case 5:
        render_stream_pixels(peakToPeak, is_beat, do_fade);
        break;
      case 6:
        render_sparkles(peakToPeak, is_beat, do_fade);
        break;
      case 7:
        render_double_vu_fade_3(peakToPeak, is_beat, do_fade);
        //render_beat_flash_1_pixel(is_beat);
        break;
      default:
        render_black();
        break;
    }

    strip.show();
}

void render_vu_plus_beat_end(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH - 1) - 1;
    int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
    
    for (int j = STRIP_LENGTH - 1; j >= 0; j--)
    {
      // 2 "pixels" "below" the strip, to exclude the noise floor from the VU
        if(j <= led && led >= 0) {
          // set VU color up to peak
          int color = map(j, 0, STRIP_LENGTH, 0, 255);
          strip.setPixelColor(j, Wheel(color));
        }
        else if(j >= 48 && j < 60 && is_beat) {
          strip.setPixelColor(j, beat_brightness,beat_brightness,beat_brightness);
        }
        else if(do_fade) {
          fade_pixel(j);
        }
    }  
}

void render_stream_pixels(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH/2 - 1) - 1;
    int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
    
    for (int j = STRIP_LENGTH-1; j > 0; j--)
    {
      if(j <= led && led >= 0) {
        // set VU color up to peak
        int color = map(j, 0, STRIP_LENGTH, 0, 255);
        strip.setPixelColor(j, Wheel(color));
      }
      else {
        stream_pixel(j);
      }
    }  
}

// this effect shifts colours along the strip on the beat.
void render_shoot_pixels(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    // only VU half the strip; for the effect to work it needs headroom.
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH >> 1 - 1) - 1;
    int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
    
    for (int j = STRIP_LENGTH - 1; j >= 0; j--)
    {
      if(j <= led && led >= 0) {
        // set VU color up to peak
        int color = map(j, 0, STRIP_LENGTH >> 2, 0, 255);
        strip.setPixelColor(j, Wheel_Purple_Yellow(color));
      }
      else {
        if(do_fade) {
          shoot_pixel(j);
          if(!is_beat) {
            fade_pixel(j);
          }
        }
      }
    }  
}

void render_vu_plus_beat_interleave(unsigned int peakToPeak, bool is_beat, bool do_fade) {
  int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH - 1) - 1;
  int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);

  for (int j = 0; j < STRIP_LENGTH; j++ ) {
    if(j % 2) {
    
      // VU
      if(j <= led && led >= 0) {
        // set VU color up to peak
        int color = map(j, 0, STRIP_LENGTH, 0, 255);
        strip.setPixelColor(j, Wheel(color));
      } 
    } else if(is_beat) {
    // beats
      strip.setPixelColor(j, beat_brightness,beat_brightness,beat_brightness);
    }
     
    if(do_fade) {
      fade_pixel(j);
    }
  }
}

void render_double_vu(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH/4 - 1) - 1;
    int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
    
    for (int j = 0; j <= STRIP_LENGTH/4; j++)
    {
      // 2 "pixels" "below" the strip, to exclude the noise floor from the VU
        if(j <= led && led >= 0) {
          // set VU color up to peak
          int color = map(j, 0, STRIP_LENGTH/4, 0, 255);
          strip.setPixelColor(j, Wheel(color));
          strip.setPixelColor((STRIP_LENGTH/4*2)+j, Wheel(color));
          color = map(j, 0, STRIP_LENGTH/4, 255, 0);
          strip.setPixelColor((STRIP_LENGTH/4*2)-j, Wheel(color));
          strip.setPixelColor((STRIP_LENGTH/4*4)-j, Wheel(color));
        }
        else if(do_fade) {
          fade_pixel(j);
          fade_pixel((STRIP_LENGTH/4*2)+j);
          fade_pixel((STRIP_LENGTH/4*2)-j);
          fade_pixel((STRIP_LENGTH/4*4)-j);
        }
    }  
}

void render_sparkles(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH -1);
    
    int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
    
    for (int j = 0; j <= STRIP_LENGTH/4; j++)
    {
      // 2 "pixels" "below" the strip, to exclude the noise floor from the VU
        if(j <= led && led >= 0) {
          // set VU color up to peak
          int color = map(j, 0, STRIP_LENGTH/4, 0, 255);
          strip.setPixelColor(j, Wheel(color));
          strip.setPixelColor((STRIP_LENGTH/4*2)+j, Wheel(color));
          color = map(j, 0, STRIP_LENGTH/4, 255, 0);
          strip.setPixelColor((STRIP_LENGTH/4*2)-j, Wheel(color));
          strip.setPixelColor((STRIP_LENGTH/4*4)-j, Wheel(color));
        }
        else if(do_fade) {
          fade_pixel(j);
          fade_pixel((STRIP_LENGTH/4*2)+j);
          fade_pixel((STRIP_LENGTH/4*2)-j);
          fade_pixel((STRIP_LENGTH/4*4)-j);
        }
    }  
}

void render_double_vu_fade_2(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH >> 2 - 1) - 1;
    int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
    
    for (int j = 0; j <= STRIP_LENGTH >> 2; j++)
    {
      // 2 "pixels" "below" the strip, to exclude the noise floor from the VU
        if(j <= led && led >= 0) {
          // set VU color up to peak
          int color = map(j, 0, STRIP_LENGTH >> 2, 0, 255);
          strip.setPixelColor(j, Wheel2(color));
          strip.setPixelColor((STRIP_LENGTH >> 1)+j, Wheel2(color));
          strip.setPixelColor((STRIP_LENGTH >> 1)-j, Wheel2(color));
          strip.setPixelColor((STRIP_LENGTH     )-j, Wheel2(color));
        }
        else if(do_fade) {
          fade_pixel(j);
          fade_pixel((STRIP_LENGTH >> 1)+j);
          fade_pixel((STRIP_LENGTH >> 1)-j);
          fade_pixel((STRIP_LENGTH     )-j);
        }
    }
}

void render_double_vu_fade_3(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH >> 2 - 1) - 1;
    int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
    
    for (int j = 0; j <= STRIP_LENGTH >> 2; j++)
    {
      // 2 "pixels" "below" the strip, to exclude the noise floor from the VU
        if(j <= led && led >= 0) {
          // set VU color up to peak
          int color = map(j, 0, STRIP_LENGTH >> 2, 0, 255);
          strip.setPixelColor(j, Wheel3(color));
          strip.setPixelColor((STRIP_LENGTH >> 1)+j, Wheel3(color));
          strip.setPixelColor((STRIP_LENGTH >> 1)-j, Wheel3(color));
          strip.setPixelColor((STRIP_LENGTH     )-j, Wheel3(color));
        }
        else if(do_fade) {
          fade_pixel(j);
          fade_pixel((STRIP_LENGTH >> 1)+j);
          fade_pixel((STRIP_LENGTH >> 1)-j);
          fade_pixel((STRIP_LENGTH     )-j);
        }
    } 
}
void render_beat_flash_1_pixel(bool is_beat) {
    // THIS BIT FLASHES ONE LED SO YOU CAN SEE THE BEATS
    if(is_beat) {
      strip.setPixelColor(0, strip.Color(127,127,127));
    } else {
      strip.setPixelColor(0, 0);
    }
    for (int j = STRIP_LENGTH - 1; j >= 1; j--) {
      strip.setPixelColor(j, 0);
    }  
}

void render_black() {
    for (int j = STRIP_LENGTH - 1; j >= 0; j--) {
      strip.setPixelColor(j, 0);
    }  
}

void fade_pixel(int pixel) {
  // The trick here is to halve the brightness of each color by shifting
  // it 1 place right (which is the same as divide by 2, but cheaper)
  // of course, we also have to shift Red by 16 and Green by 8 to get 
  // the current brightness.
  uint32_t color = strip.getPixelColor(pixel);
  color = (color >> 1) & 0x7F7F7F7F; // shift and mask WRGB all at once.
  strip.setPixelColor(pixel, color);
}


// this effect needs to be rendered from the end of the strip backwards
void stream_pixel(int pixel) {
  uint32_t old_color[4];
  
  if(pixel > 3) {
    for (char i = 0; i<4; i++) {
      old_color[i] = strip.getPixelColor(pixel-i);
      
      // Rotate and mask all colours at once.
      // Each of the 4 previous pixels contributes 1/4 brightness
      // so we divide each colour by 2.
      old_color[i] = (old_color[i] >> 2) & 0x3F3F3F3F;
    }

    strip.setPixelColor(pixel, old_color[0] + old_color[1] + old_color[2] + old_color[3]);  
  } else {
    fade_pixel(pixel);
  }
}

// like stream pixel but with a sharper fade
void shoot_pixel(int pixel) {
  uint32_t color;
  
  if(pixel >= 3) {
    color  = (strip.getPixelColor(pixel-2) >> 1) & 0x7F7F7F7F;
    color += (strip.getPixelColor(pixel-3) >> 2) & 0x3F3F3F3F;
//    color += (strip.getPixelColor(pixel-3) >> 3) & 0x1F1F1F1F;    
  } else {
    fade_pixel(pixel);
  }

  strip.setPixelColor(pixel, color);  
}

void check_mode_change_button() {
  
    // mode-change button
    bool button_is_pushed = !digitalRead(BUTTON_PIN); // button pulls pin low
    digitalWrite(BUTTON_LED_PIN, button_is_pushed);
    if(button_was_pushed && !button_is_pushed) {
      // button was released
      button_was_pushed = false;
      mode = (mode + 1) % 8;
    } else if(button_is_pushed) {
      button_was_pushed = true;
      // pushing the button cancels auto mode.
      auto_mode = false;
    }

    if(!auto_mode) {
      // don't show the lights unless we're in manual mode.
      digitalWrite(MODE_LED_PIN_1, (mode & 0x01) ? HIGH : LOW);
      digitalWrite(MODE_LED_PIN_2, (mode & 0x02) ? HIGH : LOW);
      digitalWrite(MODE_LED_PIN_3, (mode & 0x04) ? HIGH : LOW);
    }
}

void generate_random_table() {
  int i;
  
  for (i = 0; i < STRIP_LENGTH; i++) {
    random_table[i] = i;
  }

  // shuffle!
  for (i = 0; i < STRIP_LENGTH; i++)
  {
      size_t j = random(0, STRIP_LENGTH - i);
    
      int t = random_table[i];
      random_table[i] = random_table[j];
      random_table[j] = t;
  }  
}

uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  }
}

uint32_t Wheel2(byte WheelPos) {
  return strip.Color(WheelPos + 128 > 255 ? 255 : WheelPos + 128, 0, 128-WheelPos/2 < 0 ? 0 : 128-WheelPos/2);
}

uint32_t Wheel3(byte WheelPos) {
  return strip.Color(0, 128-WheelPos > 0 ? 128-WheelPos : 0, WheelPos > 128 ? 128 : WheelPos);
}

uint32_t Wheel_Purple_Yellow(byte WheelPos) {
  // 0 is purple (63,0,255)
  // 255 is yellow (255,127,0)
  
  return strip.Color(
    map(WheelPos,0,255,63,255),
    WheelPos >> 2,
    255-WheelPos
  );
}

void colorWipe(uint32_t c, uint8_t wait) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

// stuff for the beat detector

// 20 - 200hz Single Pole Bandpass IIR Filter
float bassFilter(float sample) {
    static float xv[3] = {0,0,0}, yv[3] = {0,0,0};
    xv[0] = xv[1]; xv[1] = xv[2]; 
    xv[2] = sample / 9.1f;
    yv[0] = yv[1]; yv[1] = yv[2]; 
    yv[2] = (xv[2] - xv[0])
        + (-0.7960060012f * yv[0]) + (1.7903124146f * yv[1]);
    return yv[2];
}

// 10hz Single Pole Lowpass IIR Filter
float envelopeFilter(float sample) { //10hz low pass
    static float xv[2] = {0,0}, yv[2] = {0,0};
    xv[0] = xv[1]; 
    xv[1] = sample / 160.f;
    yv[0] = yv[1]; 
    yv[1] = (xv[0] + xv[1]) + (0.9875119299f * yv[0]);
    return yv[1];
}

// 1.7 - 3.0hz Single Pole Bandpass IIR Filter
float beatFilter(float sample) {
    static float xv[3] = {0,0,0}, yv[3] = {0,0,0};
    xv[0] = xv[1]; xv[1] = xv[2]; 
    xv[2] = sample / 7.015f;
    yv[0] = yv[1]; yv[1] = yv[2]; 
    yv[2] = (xv[2] - xv[0])
        + (-0.7169861741f * yv[0]) + (1.4453653501f * yv[1]);
    return yv[2];
}
