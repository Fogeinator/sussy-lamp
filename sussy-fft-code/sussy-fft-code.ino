// -------------------- FFT SETUP --------------------
#include "arduinoFFT.h"
#define SAMPLES 1024             //Must be a power of 2
#define SAMPLING_FREQUENCY 25000 //Hz, must be less than 50000 due to ADC

arduinoFFT FFT = arduinoFFT();

unsigned int sampling_period_us;
unsigned long microseconds;

double vReal[SAMPLES];
double vImag[SAMPLES];
double peak;

#define MIC_PIN A0

// -------------------- OLED --------------------
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C // < See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

char peakString[10];

float frequencies[] { 130.8, 138.6, 146.8, 155.6, 164.8, 174.6, 185.0, 196.0, 207.7, 220.0, 232.2, 247.0 };
String notes[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
int freqLength = sizeof frequencies / sizeof frequencies[0];

// -------------------- RGB LED --------------------
#define R_PIN 14 //D5
#define G_PIN 12 //D6
#define B_PIN 13 //D7
byte color[] { 0, 0, 0 };
double h;

// -------------------- OTA --------------------
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
unsigned long previousBlinkMillis = 0;
const long blinkInterval = 1000;
int ledState = LOW;

#include "credentials.h"

void setup() {
  Serial.begin(115200);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("[DISPLAY] SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  // Clear the buffer
  display.clearDisplay();
  display.setRotation(2);
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE); // Draw white text

  Serial.println(F("[WIFI] Booting"));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println(F("[WIFI] Connection Failed! Rebooting..."));
    delay(5000);
    ESP.restart();
  }
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.print(F("[OTA] Start updating "));
    Serial.println(type);
    ledState = HIGH;
    digitalWrite(R_PIN, ledState);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\n[OTA] End"));
    ledState = LOW;
    digitalWrite(R_PIN, ledState);
    digitalWrite(G_PIN, HIGH);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    if (millis() - previousBlinkMillis >= blinkInterval) {
      previousBlinkMillis = millis();
      ledState = not(ledState);
      digitalWrite(R_PIN,  ledState);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
    ledState = HIGH;
    digitalWrite(R_PIN, ledState);
  });
  ArduinoOTA.begin();
  Serial.println(F("[OTA] Ready"));
  Serial.print(F("[WIFI] IP address: "));
  Serial.println(WiFi.localIP());

  sampling_period_us = round(1000000 * (1.0 / SAMPLING_FREQUENCY));

  pinMode(R_PIN, OUTPUT);
  pinMode(G_PIN, OUTPUT);
  pinMode(B_PIN, OUTPUT);

  delay(100);
}

void loop() {
  ArduinoOTA.handle();

  /*SAMPLING*/
  for (int i = 0; i < SAMPLES; i++)
  {
    microseconds = micros();    //Overflows after around 70 minutes!

    vReal[i] = analogRead(MIC_PIN);
    vImag[i] = 0;

    while (micros() < (microseconds + sampling_period_us)) { /* wait by doing nothing */ }
  }

  /*FFT*/
  FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);
  peak = FFT.MajorPeak(vReal, SAMPLES, SAMPLING_FREQUENCY);

  /*PRINT RESULTS*/
  //Serial.println(peak);     //Print out what frequency is the most dominant.

  vReal[0] = 0;
  vReal[1] = 0;

//  for (int i = 0; i < (SAMPLES / 2); i++)
//  {
//    /*View all these three lines in serial terminal to see which frequencies has which amplitudes*/
//    //Serial.print((i * 1.0 * SAMPLING_FREQUENCY) / SAMPLES, 1);
//    //Serial.print(" ");
//    //Serial.println(vReal[i], 1);    //View only this line in serial plotter to visualize the bins
//  }

  dtostrf(peak, 10, 2, peakString);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.write(peakString);
  display.write(" ");
  
  // transform frequency into 3rd octave
  while (peak < frequencies[0]) peak *= 2.0;
  while (peak > frequencies[freqLength - 1]) peak *= 0.5;
  display.write(findFrequency(peak).c_str());

  display.display();

  h = map(peak, frequencies[0], frequencies[freqLength - 1], 0, 360);
  hslToRgb(h, 1.0, 0.5, color);

  analogWrite(R_PIN, 255 - color[0]);
  analogWrite(G_PIN, 255 - color[1]);
  analogWrite(B_PIN, 255 - color[2]);

  delay(1);
  //while(1);       //Run code once
}

String findFrequency(float freq) {
  int closestIndex = 0;
  double diff = frequencies[closestIndex];

  for (int i = 0; i < freqLength; i++) {
    diff = (abs(frequencies[i] - freq) < diff) ? abs(frequencies[i] - freq) : diff;
    if (diff == abs(frequencies[i] - freq)) {
      closestIndex = i;
    }
  }
  return notes[closestIndex];
}

double hue2rgb(double p, double q, double t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1 / 6.0) return p + (q - p) * 6 * t;
  if (t < 1 / 2.0) return q;
  if (t < 2 / 3.0) return p + (q - p) * (2 / 3.0 - t) * 6;
  return p;
}

void hslToRgb(double h, double s, double l, byte rgb[]) {
  double r, g, b;
  h /= 360;

  if (s == 0) {
    r = g = b = l; // achromatic
  } else {
    double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    double p = 2 * l - q;
    r = hue2rgb(p, q, h + 1 / 3.0);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 1 / 3.0);
  }

  rgb[0] = r * 255;
  rgb[1] = g * 255;
  rgb[2] = b * 255;
}
