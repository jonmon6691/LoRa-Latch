/************** latch_control ***************
 *    Interface module to a central buzzer
 *    and door latch system in some apts
 *          Jon Wallace 2020
 */

#include <EEPROM.h>
#include "secrets.h"
#include "lora.h"
#include "time_window.h"

#define LATCH_PIN 8
#define BUZZER_PIN 6
#define COUNTER_RESET_PIN 11
#define COUNTER_RESET_GND_PIN 12

#define COUNTER_ADDR 0x00 // Address of the counter in the EEPROM
#define ROLLING_WINDOW_SIZE 1000 // How far ahead the remote can be and still be valid

#define RADIO_UNLOCK_TIME_MS ((unsigned long) 5 * 60 * 1000) // Buzzer unlock is active for 5 minutes since last radio contact
#define HOLD_TIME_MS ((unsigned long) 1 * 1000) // Hold the door latch for 1 second after pressing the buzzer

struct timer unlock_timer, latch_timer, rolling_code;

void setup() {
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Latch Controller");
  Serial.println("See https://github.com/jonmon6691/LoRa-Latch for documentation.");

  lora_reset();
  
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(LATCH_PIN, LOW); // Door latch relay driver
  pinMode(LATCH_PIN, OUTPUT);
  
  pinMode(BUZZER_PIN, INPUT_PULLUP); // Buzzer detect pull-up resistor

  pinMode(COUNTER_RESET_PIN, INPUT_PULLUP); // Resets counter in EEPROM when shorted to GND

  digitalWrite(COUNTER_RESET_GND_PIN, LOW); // Convinient GND for shorting the counter reset pin
  pinMode(COUNTER_RESET_GND_PIN, OUTPUT);

  clear_timer(&latch_timer);
  clear_timer(&unlock_timer);
}

void loop() {
  char next = Serial.read();
  process_character(next);
  
  unsigned long t = millis();
 
  bool hold_latch = check_timer(latch_timer, t);
  bool unlocked = check_timer(unlock_timer, t);

  if (hold_latch) {
    digitalWrite(LATCH_PIN, HIGH); // Open door
    clear_timer(&unlock_timer); // Reset unlock_timer lock after door is opened
  } else {
    digitalWrite(LATCH_PIN, LOW);
    if (unlocked && digitalRead(BUZZER_PIN) == LOW) {
      set_timer(&latch_timer, t, HOLD_TIME_MS);
    }
  }

  if (unlocked) { // Blink the LED if unlocked
    digitalWrite(LED_BUILTIN, (t / 250) % 2);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }

  if (digitalRead(COUNTER_RESET_PIN) == LOW) {
    set_timer(&rolling_code, 0, ROLLING_WINDOW_SIZE);
    EEPROM.put(COUNTER_ADDR, rolling_code);
    Serial.println("Counter reset.");
  }

}


void process_response() {
  switch (parse_response()) {
    case RES_READY:
      lora_initialized = true;
      Serial.print("AT+CPIN=" SECRET "\r\n");
      break;
      
    case RES_ERR: print_error(); break;
    
    case RES_OK: Serial.println("RES_OK"); break;
    
    case RES_RCV:
      process_rcv();
     
      // Load the last accepted counter
      EEPROM.get(COUNTER_ADDR, rolling_code);
     
      // Parse the input
      unsigned long rcvd_rolling_code;
      int salt_offset;
      int ret = sscanf(rcv_data, "%lu|%n", &rcvd_rolling_code, &salt_offset);
      if (ret != 1) break; // Check that sscanf succeeded

      // Check the counter is in the window
      bool counter_good = check_timer(rolling_code, rcvd_rolling_code);

      // Check the salt
      rcv_data += salt_offset;
      rcv_data_len -= salt_offset;
      bool salt_match = rcv_data_len == SALT_LEN &&
          strncmp(rcv_data, SALT, rcv_data_len) == 0;

      Serial.print("Window: ");
      Serial.print(rolling_code.start_time);
      Serial.print(" - ");
      Serial.println(rolling_code.end_time);
      Serial.print("Counter: ");
      Serial.print(rcvd_rolling_code);
      Serial.println(counter_good ? " (Inside window)":" (Not in window!)");
      Serial.print("Salt: ");
      Serial.println(salt_match ? "Matches":"Doesn't match!");
      
      if (counter_good && salt_match)
      {
        Serial.println("Access granted!\n");
        
        set_timer(&rolling_code, rcvd_rolling_code, ROLLING_WINDOW_SIZE);
        EEPROM.put(COUNTER_ADDR, rolling_code);

        set_timer(&unlock_timer, millis(), RADIO_UNLOCK_TIME_MS);
      } else {
        Serial.println("Access denied!\n");
      }
      break;
      
    case UNKNOWN_RESPONSE:
      Serial.print("UNKNOWN_RESPONSE: ");
      Serial.print(res_buff);
      break;
      
    default: break;
  }
}
