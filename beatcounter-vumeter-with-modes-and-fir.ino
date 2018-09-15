#include <Adafruit_NeoPixel.h>
#include <FIR.h>

#define SERIAL_DEBUG 0

#define FILTERTAPS 13

#define AUDIO_INPUT 0
#define AUDIO_INPUT_LP 2
#define AUDIO_INPUT_HP 3
#define NEOPIXEL_PIN 6
#define BUTTON_PIN 2
#define BUTTON_LED_PIN 13
#define MODE_LED_PIN_1 9
#define MODE_LED_PIN_2 10
#define MODE_LED_PIN_3 11
#define MODE_LED_PIN_4 12
#define STRIP_LENGTH 60

// This is the beat detect threshold.
// If you build a box without the pot, you can read the threshold out
// from one which has the pot using one of the test modes...
#define THRESHOLD_INPUT 1
#define DEFAULT_THRESHOLD 24.0
#define USE_POT_FOR_THRESHOLD 0

#define SILVER 0xFFFFFFFF
#define GOLD 0xFFFFFF77

// modes 0 to MAX_MODE are effects
#define MAX_MODE 10
#define MAX_AUTO_MODE 8

// Parameter 1 = number of pixels in strip
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(STRIP_LENGTH, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

FIR lowPass = FIR();
FIR highPass = FIR();

const int sampleWindow = 10; // Sample window width in mS (50 mS = 20Hz)

unsigned int sample_i;
unsigned int sample_lp_i;
unsigned int sample_hp_i;
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

static float lowPassCoeffs[] = {
0.058941394880239056,
0.067400917530924781,
0.074796392092324623,
0.080855873165259634,
0.085354127494019216,
0.088122662998354445,
0.089057263677756390,
0.088122662998354445,
0.085354127494019216,
0.080855873165259634,
0.074796392092324623,
0.067400917530924781,
0.058941394880239056
};

static float highPassCoeffs[] = {
0.014626551404255866,
0.067123004554128479,
0.067975933892614401,
-0.014742802807857579,
-0.159650634117853740,
-0.297951941959363475,
0.645239778068152026,
-0.297951941959363475,
-0.159650634117853740,
-0.014742802807857579,
0.067975933892614401,
0.067123004554128479,
0.014626551404255866
};
 

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
  pinMode(MODE_LED_PIN_4,OUTPUT);

  // Initialize all pixels to 'off'
  strip.begin();
  strip.show();
  
  time = micros(); // Used to track rate

  lowPass.setCoefficients(lowPassCoeffs);
  lowPass.setGain(1.0);
  highPass.setCoefficients(highPassCoeffs);
  highPass.setGain(1.0);
  
  Serial.begin(112500);
}

#define RENDER_P2P peakToPeak
void loop()
{
    float envelope;
    unsigned int peakToPeak;
    unsigned int peakToPeakLP;
    unsigned int peakToPeakHP;

    //check_mode_change_button();

    // 4 loops (~50ms each). VU is on a 50ms loop, beat detection is on a 200ms loop
    // also, do fades every other render
    
    peakToPeak = read_vu_meter_and_beat_envelope(envelope, peakToPeakLP, peakToPeakHP);
    render(RENDER_P2P, is_beat, true, mode, peakToPeakLP, peakToPeakHP);
    peakToPeak = read_vu_meter_and_beat_envelope(envelope, peakToPeakLP, peakToPeakHP);
    render(RENDER_P2P, is_beat, false, mode, peakToPeakLP, peakToPeakHP);
    peakToPeak = read_vu_meter_and_beat_envelope(envelope, peakToPeakLP, peakToPeakHP);
    render(RENDER_P2P, is_beat, true, mode, peakToPeakLP, peakToPeakHP);
    peakToPeak = read_vu_meter_and_beat_envelope(envelope, peakToPeakLP, peakToPeakHP);
    // Every 200 samples (25hz) filter the envelope 
    is_beat = beat_detect(envelope);
    render(RENDER_P2P, is_beat, false, mode, peakToPeakLP, peakToPeakHP);

    if(auto_mode) {
      if(is_beat) {
        beatless_count = 0;
        beat_count++;
      } else {
        beatless_count++;
      }
  
      // resynchronise the beat count mode change with the song
      if(beatless_count > 40) { // about 8 seconds
        beat_count = 0;
      }

      // change mode after 128 beats or 30 beatless seconds
      if(beat_count > 128 || beatless_count > 150) {
        mode++;
        if(mode > MAX_AUTO_MODE) {
          mode = 0;
        }
        beat_count = 0;
      }
    }
 
    for(unsigned long up = time+SAMPLEPERIODUS; up-time > 0 && up-time < 1024; time = micros()) {
      // empty loop, to consume excess clock cycles, to keep at 5000 hz  
    }
}

void render(unsigned int peakToPeak, bool is_beat, bool do_fade, char mode, unsigned int lpvu, unsigned int hpvu) {

    switch(mode) {
      case 0:
        render_vu_plus_beat_end(peakToPeak, is_beat, do_fade, lpvu, hpvu);
        break;
      case 1:
        render_shoot_pixels(peakToPeak, is_beat, do_fade, lpvu, hpvu);
        break;
      case 2:
        render_double_vu(peakToPeak, is_beat, do_fade, 0, lpvu, hpvu);
        break;
      case 3:
        render_vu_plus_beat_interleave(peakToPeak, is_beat, do_fade, lpvu, hpvu);
        break;
      case 4:
        render_double_vu(peakToPeak, is_beat, do_fade, 1, lpvu, hpvu);
        break;
      case 5:
        render_stream_pixels(peakToPeak, is_beat, do_fade);
        break;
      case 6:
        render_sparkles(peakToPeak, is_beat, do_fade);
        break;
      case 7:
        render_double_vu(peakToPeak, is_beat, do_fade, 2, lpvu, hpvu);
        break;

      // these modes suck
      case 8:
        render_beat_line(peakToPeak, is_beat, do_fade);
        break;
      case 9:
        render_beat_flash_1_pixel(is_beat);
        break;
      case 10:
        render_threshold();
        break;
      default:
        render_black();
        break;
    }

    strip.show();
}

unsigned int read_vu_meter_and_beat_envelope(float &envelope, unsigned int &peakToPeakLP, unsigned int &peakToPeakHP) {
    float sample, value, thresh;
    
    unsigned long startMillis = millis(); // Start of sample window
    unsigned int peakToPeak = 0;   // peak-to-peak level
  
    unsigned int signalMax = 0;
    unsigned int signalMin = 100;

    float lpSignalMax = 0;
    float lpSignalMin = 100;
    float hpSignalMax = 0;
    float hpSignalMin = 100;
  
    // This loops collects VU data and does beat detection for 50 mS
    while (millis() - startMillis < sampleWindow)
    {
      sample_i = analogRead(AUDIO_INPUT);
      
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

      float sampleLP = lowPass.process((float)sample_i);
      float sampleHP = highPass.process((float)sample_i);

      if (sampleLP < 1024)  // toss out spurious readings
      {
        if (sampleLP > lpSignalMax)
        {
          lpSignalMax = sampleLP;  // save just the max levels
        }
        else if (sampleLP < lpSignalMin)
        {
          lpSignalMin = sampleLP;  // save just the min levels
        }
      }
      
      if (sampleHP < 1024)  // toss out spurious readings
      {
        if (sampleHP > hpSignalMax)
        {
          hpSignalMax = sampleHP;  // save just the max levels
        }
        else if (sampleHP < hpSignalMin)
        {
          hpSignalMin = sampleHP;  // save just the min levels
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
    peakToPeakLP = (unsigned int)(lpSignalMax - lpSignalMin);
    peakToPeakHP = (unsigned int)(hpSignalMax - hpSignalMin);

#if SERIAL_DEBUG
    // debugging
    Serial.print((unsigned int)peakToPeakLP, DEC);
    Serial.print("  ");
    Serial.print((unsigned int)peakToPeakHP, DEC);
    Serial.print("  ");
    Serial.print(signalMax - signalMin, DEC);
    Serial.print("  ");
    Serial.print("\n");
#endif 
    
    return signalMax - signalMin;  // max - min = peak-peak amplitude
}

bool beat_detect(float &envelope) {
    float beat, thresh;
    
    // Filter out repeating bass sounds 100 - 180bpm
    beat = beatFilter(envelope);

    // Threshold is based on potentiometer on AN1
#if USE_POT_FOR_THRESHOLD
    thresh = 0.02f * (float)analogRead(THRESHOLD_INPUT);
#else
    thresh = 0.02f * DEFAULT_THRESHOLD;
#endif

    // If we are above threshold, FOUND A BEAT
    if(beat > thresh) {
      return true;
    }
    return false;
}

void render_vu_plus_beat_end(unsigned int peakToPeak, bool is_beat, bool do_fade, unsigned int lpvu, unsigned int hpvu) {
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH - 1) - 1;
    int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
    int bias = lpvu;
    
    for (int j = STRIP_LENGTH - 1; j >= 0; j--)
    {
      // 2 "pixels" "below" the strip, to exclude the noise floor from the VU
        if(j <= led && led >= 0) {
          // set VU color up to peak
          int color = map(j, 0, STRIP_LENGTH, 0, 255);
          strip.setPixelColor(j, Wheel((color-bias)%256));
        }
        else if(j >= STRIP_LENGTH/2 && j < STRIP_LENGTH && is_beat) {
          strip.setPixelColor(j, beat_brightness,beat_brightness,beat_brightness);
        }
        else if(do_fade) {
          fade_pixel(j);
        }
    }  
}

void render_stream_pixels(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH/2 - 1) - 1;
    
    for (int j = STRIP_LENGTH-1; j >= 0; j--)
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
void render_shoot_pixels(unsigned int peakToPeak, bool is_beat, bool do_fade, unsigned int lpvu, unsigned int hpvu) {
    // only VU half the strip; for the effect to work it needs headroom.
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH >> 1 - 1) - 1;
    
    for (int j = STRIP_LENGTH - 1; j >= 0; j--)
    {
      if(j <= led && led >= 0) {
        // set VU color up to peak
        int color = map(j, 0, STRIP_LENGTH >> 2, 0, 255);
        strip.setPixelColor(j, Wheel_Purple_Yellow(color));
      }
      else {
        shoot_pixel(j);
        if(!is_beat && do_fade) {
          fade_pixel_plume(j);
        }
      }
    }  
}

void render_vu_plus_beat_interleave(unsigned int peakToPeak, bool is_beat, bool do_fade, unsigned int lpvu, unsigned int hpvu) {
  int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH - 1) - 1;
  int beat_brightness = map(peakToPeak, 0, maximum, 0, 255);
  int bias = lpvu;

  for (int j = 0; j < STRIP_LENGTH; j++ ) {
    if(j % 2) {
    
      // VU
      if(j <= led && led >= 0) {
        // set VU color up to peak
        int color = map(j, 0, STRIP_LENGTH, 0, 255);
        strip.setPixelColor(j, Wheel((color-bias)%256));
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

void render_sparkles(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    if(do_fade) {
      for (int j = 0; j < STRIP_LENGTH; j++)
      {
        fade_pixel(j);
      }
    }
    int index = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH/3 );
    if(index >= 0) {
      generate_sparkle_table();
      for (int j = 0; j <= index; j++) {
        strip.setPixelColor(random_table[j], j%2 ? GOLD : SILVER);
      }
    }
}

void render_beat_line(unsigned int peakToPeak, bool is_beat, bool do_fade) {
    int color = map(peakToPeak, 0, maximum, 0, 255);
    for (int j = STRIP_LENGTH - 1; j > 0; j--)
    {
      // shift all the pixels along
      strip.setPixelColor(j, strip.getPixelColor(j-1));
    }
    if(is_beat) {
      strip.setPixelColor(0, 255, 255, 255);
    } else {
      strip.setPixelColor(0, color >> 2, color >> 2, color >> 2);
    }
}

void render_double_vu(unsigned int peakToPeak, bool is_beat, bool do_fade, char fade_type, unsigned int lpvu, unsigned int hpvu) {
    uint32_t color;
    // 2 "pixels" "below" the strip, to exclude the noise floor from the VU
    int led = map(peakToPeak, 0, maximum, -2, STRIP_LENGTH/2);
    int bias = lpvu;
    
    for (int j = 0; j <= STRIP_LENGTH/4; j++)
    {
      if(j <= led && led >= 0) {
        
        // set VU color up to peak
        color = map(j, 0, STRIP_LENGTH/4, 0, 255);
        switch(fade_type) {
          default:
          case 0: color = Wheel((color+bias)%256); break;
          case 1: color = Wheel2((color+bias)%256); break;
          case 2: color = Wheel3((color+bias)%256); break;
        }
        strip.setPixelColor(j, color);
        strip.setPixelColor((STRIP_LENGTH/2)+j, color);
        
        // set VU color up to peak
        color = map(j, 0, STRIP_LENGTH/4, 255, 0);
        switch(fade_type) {
          default:
          case 0: color = Wheel((color-bias)%256); break;
          case 1: color = Wheel2((color-bias)%256); break;
          case 2: color = Wheel3((color-bias)%256); break;
        }
        strip.setPixelColor((STRIP_LENGTH/2)-j, color);
        strip.setPixelColor((STRIP_LENGTH)-j, color);
      }
      else if(do_fade) {
        fade_pixel(j);
        fade_pixel((STRIP_LENGTH/2)+j);
        fade_pixel((STRIP_LENGTH/2)-j);
        fade_pixel((STRIP_LENGTH  )-j);
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

void render_threshold() {
  // THIS BIT DRAWS A NUMBER IN BINARY ON TO THE STRIP
  unsigned int threshold = analogRead(THRESHOLD_INPUT);
  for(int i = 0; i < STRIP_LENGTH; i++)
  {
    if (threshold & 0x01) {
      strip.setPixelColor(i, strip.Color(127,127,127));
    } else {
      strip.setPixelColor(i, 0);
    }
    threshold = threshold >> 1;
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

// fades pixels more the closer they are the start, so that peaks stay visible
void fade_pixel_plume(int pixel) {
  float fade_factor;
  if(pixel < STRIP_LENGTH >> 1) {
    fade_factor = map(pixel, 0, STRIP_LENGTH >> 1, 0.5, 1.0);  
  } else {
    fade_factor = map(pixel, STRIP_LENGTH >> 1, STRIP_LENGTH, 1.0, 0.5);  
  }
  uint32_t color = strip.getPixelColor(pixel);
  uint8_t r = color >> 16;
  uint8_t g = color >> 8;
  uint8_t b = color;
  strip.setPixelColor(pixel, r*fade_factor, g*fade_factor, b*fade_factor);
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
  
  if(pixel >= 4) {
    color  = (strip.getPixelColor(pixel-2) >> 1) & 0x7F7F7F7F;
    color += (strip.getPixelColor(pixel-3) >> 2) & 0x3F3F3F3F;
    color += (strip.getPixelColor(pixel-4) >> 3) & 0x1F1F1F1F;    
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
      mode = (mode + 1);
      if(mode > MAX_MODE) {
        mode = 0;
      }
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
      digitalWrite(MODE_LED_PIN_4, (mode & 0x08) ? HIGH : LOW);
    }
}

void generate_sparkle_table() {
  int i;
  
  for (i = 0; i < STRIP_LENGTH; i++) {
    random_table[i] = i;
  }

  // shuffle!
  // we only shuffle HALF the table, because render_sparkle
  // only 
  for (i = 0; i < STRIP_LENGTH / 2; i++)
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
  // 0 is blue (0,0,255)
  // 255 is yellow (255,127,0)
  return strip.Color(
    WheelPos, 
    WheelPos >> 1, 
    255-WheelPos);
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

void print_float(float f, int num_digits)
{
    int f_int;
    int pows_of_ten[4] = {1, 10, 100, 1000};
    int multiplier, whole, fract, d, n;

    multiplier = pows_of_ten[num_digits];
    if (f < 0.0)
    {
        f = -f;
        Serial.print("-");
    }
    whole = (int) f;
    fract = (int) (multiplier * (f - (float)whole));

    Serial.print(whole);
    Serial.print(".");

    for (n=num_digits-1; n>=0; n--) // print each digit with no leading zero suppression
    {
         d = fract / pows_of_ten[n];
         Serial.print(d);
         fract = fract % pows_of_ten[n];
    }
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max)
{
 return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
