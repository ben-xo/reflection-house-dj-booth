#include <Adafruit_NeoPixel.h>

#define PIN 6

// Parameter 1 = number of pixels in strip
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(60, PIN, NEO_GRB + NEO_KHZ800);

const int sampleWindow = 10; // Sample window width in mS (50 mS = 20Hz)
unsigned int sample_i;
int maximum = 110;
int peak;
int dotCount;
unsigned int num_pixels;
unsigned int fade_loop_counter = 0;
bool is_beat;
unsigned long time;
unsigned char i = 0;

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

  // Set ADC to 77khz, max for 10bit
  sbi(ADCSRA,ADPS2);
  cbi(ADCSRA,ADPS1);
  cbi(ADCSRA,ADPS0);
//
//  //The pin with the LED
//  pinMode(2, OUTPUT);
  
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  num_pixels = strip.numPixels();
  time = micros(); // Used to track rate
}


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

void loop()
{
  float sample, value, envelope, beat, thresh;

//  for(i = 0;;){
    unsigned long startMillis = millis(); // Start of sample window
    unsigned int peakToPeak = 0;   // peak-to-peak level
  
    unsigned int signalMax = 0;
    unsigned int signalMin = 100;

      // collect data for 50 mS
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
        i++;
      }
      peakToPeak = signalMax - signalMin;  // max - min = peak-peak amplitude

      while (i < 200) {
        // Read ADC and center to +-512
        sample = (float)analogRead(0)-503.f;
  
        // Filter only bass component
        value = bassFilter(sample);
  
        // Take signal amplitude and filter
        if(value < 0)value=-value;
        envelope = envelopeFilter(value);
        i++;        
      }

      // Every 200 samples (25hz) filter the envelope 
      
      // Filter out repeating bass sounds 100 - 180bpm
      beat = beatFilter(envelope);

      // Threshold it based on potentiometer on AN1
      thresh = 0.02f * (float)60;

      // If we are above threshold, light up LED
      if(beat > thresh) is_beat = true;
      else is_beat = false;

      //Reset sample counter
      i = 0;
      
      int led = map(peakToPeak, 0, maximum, -2, num_pixels - 1) - 1;
      int beat_brightness = map(peakToPeak, 0, maximum, 0, 100);
      
      for (int j = num_pixels - 1; j >= 0; j--)
      {
        // 2 "pixels" "below" the strip, to exclude the noise floor from the VU
          if(j <= led && led >= 0) {
            // set VU color up to peak
            int color = map(j, 0, num_pixels, 0, 255);
            strip.setPixelColor(j, Wheel(color));
            //strip.setPixelColor(j,color);
          }
          else if(j >= 48 && j < 60 && is_beat) {
            strip.setPixelColor(j, beat_brightness,beat_brightness,beat_brightness);
          }
          else {
            int old_color = strip.getPixelColor(j);
            int r = (old_color & 0x00FF0000) >> 17;
            int g = (old_color & 0x0000FF00) >> 9;
            int b = (old_color & 0x000000FF) >> 1;
            strip.setPixelColor(j, r,g,b);
          }
      }

        // THIS BIT FLASHES ONE LED SO YOU CAN SEE THE BEATS
//      if(is_beat) {
//        strip.setPixelColor(0, strip.Color(127,127,127));
//      } else {
//        strip.setPixelColor(0, 0);
//      }

      strip.show();
      
      // Consume excess clock cycles, to keep at 5000 hz
      for(unsigned long up = time+SAMPLEPERIODUS; time > 20 && time < up; time = micros()) {}
//  }  
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

void colorWipe(uint32_t c, uint8_t wait) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}
