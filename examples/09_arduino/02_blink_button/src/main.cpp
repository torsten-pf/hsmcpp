// Copyright (C) 2023 Igor Krechetov
// Distributed under MIT license. See file LICENSE for details

#include "hsm/BlinkButtonHsmBase.hpp"
#include <Arduino.h>
#include <hsmcpp/HsmEventDispatcherArduino.hpp>
#include <memory>

/*
  This example demonstrates basic usage of hsmcpp library by controlling LED on
  button press.

  Note: This code assumes the button is connected in a way that when it's
  pressed, the input on the button pin becomes HIGH.

  The main idea is to separate behavior of your device from code and define it
  in a form of a state machine.

  State machine is defined in blink_button.scxml file and looks like this:

        +-------+  BUTTON_PRESSED   +-------+
        |  LED  |------------------>|  LED  |
  [*]-->|  Off  |                   |  On   |
        |       |<------------------|       |
        +-------+  BUTTON_RELEASED  +-------+

  BUTTON_PRESSED and BUTTON_RELEASED events are sent from loop() based on
  current button state. A corresponding state callback will be called from
  BlinkHSM When transition is executed and HSM moved to a new state.

  blink_button.scxml will be automatically generated into BlinkButtonHsmBase C++
  class during build. Generation is controlled by "custom_hsm_files" and
  "custom_hsm_gendir" settings in platformio.ini.
*/

#define PIN_BUTTON (14)

// Implementation of state machine callbacks.
class BlinkHSM : public BlinkButtonHsmBase {
protected:
  void onOff(const hsmcpp::VariantVector_t &args) override {
    // turn the LED off by making the voltage LOW
    digitalWrite(LED_BUILTIN, LOW);
  }

  void onOn(const hsmcpp::VariantVector_t &args) override {
    // turn the LED on (HIGH is the voltage level)
    digitalWrite(LED_BUILTIN, HIGH);
  }
};

// global instances of state machine and dispatcher
BlinkHSM *gHSM = nullptr;
std::shared_ptr<hsmcpp::HsmEventDispatcherArduino> gDispatcher;

void setup() {
  // initialize digital pin LED_BUILTIN as an output
  pinMode(LED_BUILTIN, OUTPUT);
  // initialize button pin as an input
  pinMode(PIN_BUTTON, INPUT);

  // Create instance of the state machine
  // Usually you would want the instance of your state machine to live for
  // signifficant period of time (or forever). Because of this, it should be
  // created dynamically to use heap memory instead of stack.
  gHSM = new BlinkHSM();
  // Create event dispatcher
  gDispatcher = std::make_shared<hsmcpp::HsmEventDispatcherArduino>();
  // initialize state machine
  gHSM->initialize(gDispatcher);
}

void loop() {
  // store previous state to be able to detect only changes in button states
  // (pressed/released)
  static int prevButtonState = LOW;
  const int buttonState = digitalRead(PIN_BUTTON);

  if (buttonState != prevButtonState) {
    prevButtonState = buttonState;

    if (buttonState == HIGH) {
      gHSM->transition(BlinkButtonHsmEvents::BUTTON_PRESSED);
    } else {
      gHSM->transition(BlinkButtonHsmEvents::BUTTON_RELEASED);
    }
  }

  // tell dispatcher to process pending events and transitions
  gDispatcher->dispatchEvents();
}