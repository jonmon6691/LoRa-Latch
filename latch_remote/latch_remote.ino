/*************** latch_remote *****************
 *    A remote rolling code transmitter
 *            Jon Wallace 2020
 */
 
#include <EEPROM.h>
#include "secrets.h"
#include "lora.h"
#include "time_window.h"

#define BEACON_PERIOD_MS 1000 // Send ping every second
struct timer beacon_timeout;
#define TRANSMIT_AFTER_CAR_OFF_MS 15000 // Stop transmitting 15 seconds after car is turned off
struct timer transmit_timer;

#define CAR_ON_PIN 7 // Connected to the "USB" pin on the boost/charger
#define COUNTER_RESET_PIN 11
#define COUNTER_RESET_GND_PIN 12

#define COUNTER_ADDR 0x00 // Address of the counter in the EEPROM
unsigned long counter;

void setup() {
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Latch Remote");
  Serial.println("See https://github.com/jonmon6691/LoRa-Latch for documentation.");
  
  lora_reset();

  pinMode(LED_BUILTIN, OUTPUT);
  
  pinMode(CAR_ON_PIN, INPUT);

  pinMode(COUNTER_RESET_PIN, INPUT); // Resets counter in EEPROM when shorted to GND
  digitalWrite(COUNTER_RESET_PIN, HIGH);

  digitalWrite(COUNTER_RESET_GND_PIN, LOW); // Convinient GND for shorting the counter reset pin
  pinMode(COUNTER_RESET_GND_PIN, OUTPUT);

  clear_timer(&beacon_timeout);
  clear_timer(&transmit_timer);
}

void loop() {
  char next = Serial.read();
  process_character(next);

  // Don't do anything until the LoRa module is initialized
  if (lora_initialized == false) return; 

  bool car_on = digitalRead(CAR_ON_PIN);
  unsigned long now = millis();
  
  if (car_on == HIGH) {
    set_timer(&transmit_timer, now, TRANSMIT_AFTER_CAR_OFF_MS);
  }

  bool transmit = check_timer(transmit_timer, now);
  bool wait_for_timeout = check_timer(beacon_timeout, now);

  if (car_on == LOW &&
      transmit &&
      wait_for_timeout == false) {
    Serial.println("Sending");
    set_timer(&beacon_timeout, now, BEACON_PERIOD_MS);
    
    counter = EEPROM.get(COUNTER_ADDR, counter);

    char buff[10 + 1 + SALT_LEN]; // MAX_INT + '|' + password
    int len = sprintf(buff, "%lu|%s", counter, SALT);
    sprintf(res_buff, "AT+SEND=0,%d,%s\r\n", len, buff);

    counter += 1;
    EEPROM.put(COUNTER_ADDR, counter);

    Serial.print(res_buff);
  }

  if (digitalRead(COUNTER_RESET_PIN) == LOW) {
    counter = 1;
    EEPROM.put(COUNTER_ADDR, counter);
    Serial.println("Counter reset.");
  }

}

void process_response() {
  switch (parse_response()) {
    case RES_READY: 
      lora_initialized = true;
      Serial.print("AT+CPIN=" SECRET "\r\n");
      break;
    case RES_ERR: break; //print_error(); break;
    case RES_OK: break;
    case RES_RCV: break;
    case UNKNOWN_RESPONSE: break;
    default: break;
  }
}
