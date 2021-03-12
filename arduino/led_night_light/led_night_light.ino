/**
 * LED Night Light
 * Author: dJPoida (aka Peter Eldred)
 * URL: https://github.com/dJPoida/led_night_light
 * Version: 2021-03
 * License: MIT License
 * 
 * This program is designed to run on an Arduino Nano connected
 * to a strip of WS2812 LEDs as part of an RGB night light
 */

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

/**
 * CONFIGURATION
 */

// Comment out this line to remove serial debugging
// #define SERIAL_DEBUG

// Buttons
#define PIN_BUTTON_1          6       // The button for changing color
#define PIN_BUTTON_2          5       // The button for changing mode
#define DEBOUNCE_DELAY        50      // A delay (ms) to wait to prevent phantom button presses

// LED Strip
#define PIN_LED_STRIP         7       // The GPIO Pin for the data line on the LED strip
#define NUM_LEDS              10      // The number of LEDs connected on the LED strip
#define LED_UPDATE_DELAY      8.3333  // 8.3333ms = ~120fps
#define LED_COLOR_GRADUATION  1       // How many color steps to graduate in a single update
#define PHASE_DURATION        1000    // How long each phase of a transition animation should last
#define LED_BRIGHTNESS_1      50      // LED Strip brightness level 1/5 (0-255)
#define LED_BRIGHTNESS_2      90      // LED Strip brightness level 2/5 (0-255)
#define LED_BRIGHTNESS_3      130     // LED Strip brightness level 3/5 (0-255)
#define LED_BRIGHTNESS_4      180     // LED Strip brightness level 4/5 (0-255)
#define LED_BRIGHTNESS_5      220     // LED Strip brightness level 5/5 (0-255)

// The various modes the light can be in
#define MODECOUNT 3               // Total number of modes available
#define MODE_UNIFIED 0            // All LEDs have the same color
#define MODE_COMPLEMENTARY 1      // Two solid complementary colors
#define MODE_RAIN 2               // Two cascading complementary colors


/**
 * Variables
 */

// A Simple struct type for describing RGB colors
struct RGB {
  byte r;
  byte g;
  byte b;
};

// Some variables for handling each of the buttons (arrays of 2 values)
byte btnPins[2] = {PIN_BUTTON_1, PIN_BUTTON_2};   // Just the Pins defined above in an array for each button
bool btnState[2] = {0, 0};                        // The current state of each button
bool lastBtnState[2] = {0, 0};                    // The previous state of each button
bool btnPressHandled[2] = {0, 0};                 // Whether the current read of a button should handle a button press
unsigned long lastBtnDebounceTime[2] = {0, 0};    // The last time a button debounce was checked
unsigned long btnDownStart[2] = {0, 0};           // The first moment in time a button press was detected

// Some variables for timing LED updates
unsigned long lastLEDUpdate = 0;      // The last time the LED strip was updated
unsigned long lastPhaseChange = 0;    // The last time the LED strip phase changed

// Convert the mode definitions into an array for iteration (i.e. cycling)
byte modes[MODECOUNT] = {
  MODE_UNIFIED,
  MODE_COMPLEMENTARY,
  MODE_RAIN
};

// Convert the brightness definitions into an array for iteration (i.e. cycling)
byte brightnessValues[5] = {
  LED_BRIGHTNESS_1,
  LED_BRIGHTNESS_2,
  LED_BRIGHTNESS_3,
  LED_BRIGHTNESS_4,
  LED_BRIGHTNESS_5,
};

// These are the colors the lights will cycle through
#define NUM_COLORS 8
RGB colors[NUM_COLORS] = {
  {255, 255, 255},  // White
  {255, 127, 0},    // Orange
  {255, 225, 0},    // Yellow
  {127, 255, 0},    // Lime Green
  {0, 255, 255},    // Aqua
  {127, 0, 255},    // Purple
  {230, 30, 110},   // Pink
  {255, 60, 40},    // Red
};

// These colors are the complementary colors for the primary colors
// Used in the complementary mode and the rain mode
RGB complementary_colors[NUM_COLORS] = {
  colors[4], // White: Aqua
  colors[2], // Orange: Yellow
  colors[7], // Yellow: Red
  colors[2], // Lime Green: Yellow
  colors[0], // Aqua: White
  colors[6], // Purple: Pink
  colors[5], // Pink: Purple
  colors[1], // Red: Orange
};

// The Complementary LED map tells the night light which LEDs are
// at the front and which LEDs are at the rear
#define COMPLEMENTARY_FRONT 0
#define COMPLEMENTARY_REAR 1
bool complementary_LED_map[NUM_LEDS] = {
  COMPLEMENTARY_FRONT,
  COMPLEMENTARY_FRONT,
  COMPLEMENTARY_FRONT,
  COMPLEMENTARY_REAR,
  COMPLEMENTARY_REAR,
  COMPLEMENTARY_REAR,
  COMPLEMENTARY_REAR,
  COMPLEMENTARY_REAR,
  COMPLEMENTARY_REAR,
  COMPLEMENTARY_FRONT
};

// The rain phase map indicates which LEDs have each phase of the rain
// animation. There are three phases (0-2) and each LED must be assigned a phase.
byte rain_phase_LED_map[NUM_LEDS] = { 0, 2, 1, 1, 2, 0, 0, 2, 1, 0 };

// The current mode that the light is in (Default = MODE_UNIFIED)
byte currentMode = MODE_UNIFIED;

// The input brightness level set by a button press (Default = Level 3)
byte inputBrightnessLevel = 3;

// The target brightness the code is aiming for (0-255)
byte targetBrightness = brightnessValues[inputBrightnessLevel];

// The actual brightness applied to the LEDs at the current time (0-255)
byte actualBrightness = brightnessValues[inputBrightnessLevel];

// The input color index set by a button press
int currentColorIndex = 0;

// The RGB color the code is aiming for based on the input color index
RGB targetColor[NUM_LEDS];

// The RGB color the strip is currently showing (as a frame in a transition)
RGB actualColor[NUM_LEDS];

// The phase of the rain that is currently showing
byte currentRainPhase = 0;

// A reference to the Adafruit NeoPixel Strip instance used to control the LED strip
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN_LED_STRIP, NEO_GRB + NEO_KHZ800);


/**
 * Setup
 */
void setup() {
  #ifdef SERIAL_DEBUG
  Serial.begin(9600);
  Serial.println("LED Night Light");
  #endif

  // Setup the GPIOs
  pinMode(PIN_BUTTON_1, INPUT);
  pinMode(PIN_BUTTON_2, INPUT);

  // Load the previous state from the EEPROM;
  loadState();

  // Initialise the LED Strip
  strip.begin();
}


/**
 * Loop
 */
void loop() { 
  // Look for and respond to button presses
  checkButtons();

  // Update any phase that may be required for the current mode
  updatePhase();

  // Update the LED strip values based on current state data
  updateLEDs();
}


/**
 * Check and register button state changes
 */
void checkButtons() {
  // Read the current time that the device has been running (in milliseconds)
  unsigned long currentMillis = millis();

  // For each button
  for (byte i = 0; i < 2; i++) {
    int btnReading = digitalRead(btnPins[i]);

    // If the button reading has changed
    if (btnReading != lastBtnState[i]) {
      // Make node of the change as a debounce timestamp
      lastBtnDebounceTime[i] = currentMillis;
    }
    
    // If the appropriate amount of time has passed and the button is stable, register the change
    if ((currentMillis - lastBtnDebounceTime[i]) > DEBOUNCE_DELAY) {
      // if the button state has changed:
      if (btnReading != btnState[i]) {
        // Register the button state change (and for how long the button has been in its previous state)
        btnStateChange(i, btnReading, btnReading ? 0 : currentMillis - btnDownStart[i]);
      }
    }

    // Keep track of the reading for the next time around
    lastBtnState[i] = btnReading;
  }
}


/**
 * Fired when one of the button presses has passed the de-bounce check
 *
 * @param {byte} btnNumber          - the button that was pressed
 * @param {bool} newState           - whether the button is depressed or released
 * @param {unsigned long} duration  - for how long the button was in the previous state
 */
void btnStateChange(byte btnNumber, bool newState, unsigned long duration) {
  // Read the current time that the device has been running (in milliseconds)
  unsigned long currentMillis = millis();

  // Update the new state for the target button
  btnState[btnNumber] = newState;
  
  #ifdef SERIAL_DEBUG
  Serial.print("Button: ");
  Serial.print(btnNumber);
  Serial.print(" ");
  Serial.print(newState ? "DOWN" : "UP");
  #endif

  // Button is Down
  if (newState) {
    #ifdef SERIAL_DEBUG
    Serial.println("");
    #endif

    btnDownStart[btnNumber] = currentMillis;
    btnPressHandled[btnNumber] = 0;
  }

  // Button is Up
  else {
    #ifdef SERIAL_DEBUG
    Serial.print(" ");
    Serial.print(duration);
    Serial.println("ms");
    #endif

    btnDownStart[btnNumber] = 0;

    // Button 0 released while button 1 not pressed
    if ((btnNumber == 0) && (btnState[1] == 0) && (btnPressHandled[0] == 0)) {
      cycleColor();
    }

    // Button 1 released while button 0 not pressed
    else if ((btnNumber == 1) && (btnState[0] == 0) && (btnPressHandled[1] == 0)) {
      cycleMode();
    }

    // Button 1 released while button 0 held (increase brightness)
    else if ((btnNumber == 1) && (btnState[0] == 1) && btnPressHandled[1] == 0) {
      // Don't action button 0 when released
      btnPressHandled[0] = 1;
      incBrightness();
    }
  }
}


/**
 * Write the current settings to the programmable memory (EEPROM)
 */
void saveState() {
  EEPROM.write(0, currentMode);
  EEPROM.write(1, inputBrightnessLevel);
  EEPROM.write(2, currentColorIndex);
}


/**
 * Read the current settings from the programmable memory (EEPROM)
 */
void loadState() {
  currentMode = EEPROM.read(0) == 255 ? currentMode : EEPROM.read(0);
  
  inputBrightnessLevel = EEPROM.read(1) == 255 ? inputBrightnessLevel : EEPROM.read(1);
  targetBrightness = brightnessValues[inputBrightnessLevel];
  actualBrightness = 0;
  
  currentColorIndex = EEPROM.read(2) == 255 ? currentColorIndex : EEPROM.read(2);
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    actualColor[i] = {0, 0, 0};
  }

  #ifdef SERIAL_DEBUG
  Serial.println("Loaded previous state:");
  Serial.print("  - Mode: ");
  Serial.println(currentMode);
  Serial.print("  - Brightness: ");
  Serial.println(inputBrightnessLevel);
  Serial.print("  - Color: ");
  Serial.println(currentColorIndex);
  #endif
}


/**
 * Increment the current brightness
 */
void incBrightness() {
  byte newBrightness = inputBrightnessLevel + 1;
  if (newBrightness > 4) {
    newBrightness = 0;
  }
  inputBrightnessLevel = newBrightness;
  targetBrightness = brightnessValues[inputBrightnessLevel];

  #ifdef SERIAL_DEBUG
  Serial.print("Brightness: ");
  Serial.println(inputBrightnessLevel);
  #endif

  // Make sure the new value is stored incase we lose power
  saveState();
}


/**
 * Increment the current mode
 */
void cycleMode() {
  byte newMode = currentMode + 1;
    if (newMode >= MODECOUNT) {
    newMode = 0;
  }
  currentMode = newMode;

  #ifdef SERIAL_DEBUG
  Serial.print("Mode: ");
  Serial.println(currentMode);
  #endif
  
  // Make sure the new value is stored incase we lose power
  saveState();
}


/**
 * Respond to a button press and cycle the color
 */
void cycleColor() {
  byte newColorIndex = currentColorIndex + 1;
  if (newColorIndex >= NUM_COLORS) {
    newColorIndex = 0;
  }
  currentColorIndex = newColorIndex;
  
  #ifdef SERIAL_DEBUG
  Serial.print("Input Color: ");
  Serial.println(currentColorIndex);
  #endif

  // Make sure the new value is stored incase we lose power
  saveState();
}


/**
 * For those modes which require cycling, this method
 * updates the phase.
 */
void updatePhase() {
  // Read the current time that the device has been running (in milliseconds)
  unsigned long currentMillis = millis();

  // Rain Mode
  if (currentMode == MODE_RAIN) {
    if ((currentMillis - lastPhaseChange) > PHASE_DURATION) {
      lastPhaseChange = currentMillis;
      currentRainPhase += 1;
      if (currentRainPhase > 2) {
        currentRainPhase = 0;
      }
    }
  }
}


/**
 * Take the current LED colors and apply them to the strip
 */
void updateLEDs() {
  // Read the current time that the device has been running (in milliseconds)
  unsigned long currentMillis = millis();

  // Don't update the LEDs any more frequently than specified
  if ((currentMillis - lastLEDUpdate) > LED_UPDATE_DELAY) {

    // Do something different depending on the current mode
    switch (currentMode) {
      
      // All colors are the same as the input color
      case MODE_UNIFIED:
        for (uint16_t i = 0; i < NUM_LEDS; i++) {
          targetColor[i] = colors[currentColorIndex];
        }
        break;

      // Complementary Mode
      case MODE_COMPLEMENTARY:
        // Primary LEDs
        for (uint16_t i = 0; i < NUM_LEDS; i++) {
          if (complementary_LED_map[i] == COMPLEMENTARY_FRONT) {
            targetColor[i] = colors[currentColorIndex];
          } else {
            targetColor[i] = complementary_colors[currentColorIndex];
          }
        }
        break;

      // Rain Mode
      case MODE_RAIN:
        byte primaryPhase = currentRainPhase;
        byte secondaryPhase = currentRainPhase + 1;
        byte tertiaryPhase = currentRainPhase + 2;

        if (secondaryPhase > 2) {
          secondaryPhase = (secondaryPhase - 3);
        }
        if (tertiaryPhase > 2) {
          tertiaryPhase = (tertiaryPhase - 3);
        }
        
        for (uint16_t i = 0; i < NUM_LEDS; i++) {
          // Primary LEDs
          if (rain_phase_LED_map[i] == primaryPhase) {
            targetColor[i] = colors[currentColorIndex];
          }

          // Secondary LEDs
          else if (rain_phase_LED_map[i] == secondaryPhase) {
            targetColor[i] = halfwayBetweenColors(colors[currentColorIndex], complementary_colors[currentColorIndex]);
          }
  
          // Tertiary LEDs
          else if (rain_phase_LED_map[i] == tertiaryPhase) {
            targetColor[i] = complementary_colors[currentColorIndex];
          }
        }
        break;
    }

    // Begin the next block of code assuming that none of the LEDs have changed in any way
    bool somethingChanged = 0;

    // Check that each of the target colors match the actual colors and if not, graduate to them
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
      if (
        actualColor[i].r != targetColor[i].r ||
        actualColor[i].g != targetColor[i].g ||
        actualColor[i].b != targetColor[i].b
      ) {
        // We now know that something has changed and we will need to apply new values to the strip
        somethingChanged = 1;
        
        // perform the linear graduation
        int newR = actualColor[i].r;
        int newG = actualColor[i].g;
        int newB = actualColor[i].b;
        
        if (newR < targetColor[i].r) {
          newR = constrain((newR + LED_COLOR_GRADUATION), 0, targetColor[i].r);
        } else if (newR > targetColor[i].r) {
          newR = constrain((newR - LED_COLOR_GRADUATION), targetColor[i].r, 255);
        }

        if (newG < targetColor[i].g) {
          newG = constrain((newG + LED_COLOR_GRADUATION), 0, targetColor[i].g);
        } else if (newG > targetColor[i].g) {
          newG = constrain((newG - LED_COLOR_GRADUATION), targetColor[i].g, 255);
        }

        if (newB < targetColor[i].b) {
          newB = constrain((newB + LED_COLOR_GRADUATION), 0, targetColor[i].b);
        } else if (newB > targetColor[i].b) {
          newB = constrain((newB - LED_COLOR_GRADUATION), targetColor[i].b, 255);
        }

        // Construct the RGB value from its components
        actualColor[i] = { byte(newR), byte(newG), byte(newB) };

        // Send the value to the strip
        strip.setPixelColor(i, strip.Color(actualColor[i].r, actualColor[i].g, actualColor[i].b)); 
      }
    }

    // Check that the target brightness matches the actual brightness
    if (targetBrightness != actualBrightness) {
      // We now know that something has changed and we will need to apply new values to the strip
      somethingChanged = 1;

      // perform the brightness graduation
      int newBrightness = actualBrightness;
      if (newBrightness < targetBrightness) {
        newBrightness = constrain((newBrightness + LED_COLOR_GRADUATION), 0, targetBrightness);
      } else {
        newBrightness = constrain((newBrightness - LED_COLOR_GRADUATION), targetBrightness, 255);
      }
      actualBrightness = newBrightness;

      // Send the value to the strip
      strip.setBrightness(actualBrightness);
    }
      
    // Update the actual LED Strip only if a pixel RGB value or brightness changed
    if (somethingChanged) {
      strip.show();
    }
    
    // This helps us maintain our desired frame rate
    lastLEDUpdate = currentMillis;  
  }
}


/**
 * Get the RGB of a color halfway between two colors
 * 
 * @param {RGB} A   - the first color
 * @param [RGB} B   - the second color
 */
RGB halfwayBetweenColors(RGB A, RGB B) {
  RGB result = A;

  if (A.r < B.r) {
    result.r = A.r + ((B.r - A.r) / 2);
  } else if (A.r > B.r) {
    result.r = B.r + ((A.r - B.r) / 2);
  }
  
  if (A.g < B.g) {
    result.g = A.g + ((B.g - A.g) / 2);
  } else if (A.g > B.g) {
    result.g = B.g + ((A.g - B.g) / 2);
  }

  if (A.b < B.b) {
    result.b = A.b + ((B.b - A.b) / 2);
  } else if (A.b > B.b) {
    result.b = B.b + ((A.b - B.b) / 2);
  }

  return result;
}
