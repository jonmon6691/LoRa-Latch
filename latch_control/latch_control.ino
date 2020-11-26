/************** latch_control ***************
 *    Interface module to a central buzzer
 *    and door latch system in some apts
 *          Jon Wallace 2020
 */

#include <EEPROM.h>
#include "secrets.h"

#define MAX_RES_SIZE (263)
char res_buff[MAX_RES_SIZE];
int res_i = 0;

// Populated by process_rcv() in the RES_RCV case
int rcv_addr;
int rcv_len;
int rcv_data_offset;
int rcv_rssi;
int rcv_snr;

#define LATCH_PIN 8
#define BUZZER_PIN 6
#define COUNTER_RESET_PIN 11
#define COUNTER_RESET_GND_PIN 12

bool lora_initd;

#define COUNTER_ADDR (0) // Address of the counter in the EEPROM
#define ROLLING_WINDOW_SIZE (1000) // How far ahead the remote can be and still be valid

#define RADIO_UNLOCK_TIME_MS ((unsigned long) 5*60*1000) // Buzzer unlock is active for 5 minutes since last radio contact
#define HOLD_TIME_MS ((unsigned long) 1 * 1000) // Hold the door latch for 3 seconds after pressing the buzzer

unsigned long radio_unlock;
unsigned long hold_latch;

void setup() {
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Latch Controller");
  Serial.println("See https://github.com/jonmon6691/LoRa-Latch for documentation.");

  lora_initd = false;
  Serial.print("AT+RESET\r\n");
  
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(LATCH_PIN, LOW);    // Door latch relay driver
  pinMode(LATCH_PIN, OUTPUT);
  
  pinMode(BUZZER_PIN, INPUT);      // Buzzer detect pull-up resistor
  digitalWrite(BUZZER_PIN, HIGH);

  pinMode(COUNTER_RESET_PIN, INPUT);      // Resets counter in EEPROM when shorted to GND
  digitalWrite(COUNTER_RESET_PIN, HIGH);

  digitalWrite(COUNTER_RESET_GND_PIN, LOW);    // Convinient GND for shorting the counter reset pin
  pinMode(COUNTER_RESET_GND_PIN, OUTPUT);

  radio_unlock = 0;
  hold_latch = 0;
}

void loop() {
  char next = Serial.read();
  process_character(next);
  
  unsigned long t = millis();
  
  // Overflow detection is a bit naive. If either timer spans the overflow value it will get cut short.
  // A better way to do it is finding some way to maintain the timer over the overflow threshold
  if (hold_latch > t && hold_latch - t > HOLD_TIME_MS) { // Detect overflow
      hold_latch = t;
  }
  if (radio_unlock > t && radio_unlock - t > RADIO_UNLOCK_TIME_MS) { // Detect overflow
      radio_unlock = t;
  }
  
  if (t > hold_latch) {
    digitalWrite(LATCH_PIN, LOW);
    if (!digitalRead(BUZZER_PIN) && radio_unlock > t) {
      hold_latch = t + HOLD_TIME_MS;
    }
  } else {
    digitalWrite(LATCH_PIN, HIGH); // Open door
    radio_unlock = t; // Reset radio lock after door is opened
  }

  if (radio_unlock > t) { // Blink the LED if unlocked
    digitalWrite(LED_BUILTIN, (t / 250) % 2);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }

  if (digitalRead(COUNTER_RESET_PIN) == LOW) {
    unsigned int counter = 0;
    EEPROM.put(COUNTER_ADDR, counter);
  }

}

void process_character(char next) {
  switch (next) {
  case -1: break; // Nothing to read
  
  case 10: // Line feed
    res_buff[res_i++] = next;
    res_buff[res_i++] = '\0'; // Add a null termination for easy printing if need be
    
    process_response();
    break;
    
  case '+': res_i = 0;
    // fall through
  default: res_buff[res_i++] = next;
    break;
  }
}

#define UNKNOWN_RESPONSE -1
#define RES_READY 0
#define RES_ERR 1
#define RES_OK 2
#define RES_RCV 3
#define N_RES 4
const char *RES_STRINGS[N_RES] = {"+READY", "+ERR=", "+OK", "+RCV="};
#define RES_ERR_OFFSET 5
#define RES_RCV_OFFSET 5

void process_response() {
  switch (parse_response()) {
    case RES_READY:
      lora_initd = true;
      Serial.print("AT+CPIN=" SECRET "\r\n");
      break;
      
    case RES_ERR: print_error(); break;
    
    case RES_OK: Serial.println("RES_OK"); break;
    
    case RES_RCV:
      process_rcv();
     
      // Load the last accepted counter
      unsigned int eeprom_counter;
      EEPROM.get(COUNTER_ADDR, eeprom_counter);
     
      // Parse the input
      unsigned int rcv_counter;
      int salt_offset;
      int ret = sscanf(res_buff+rcv_data_offset, "%u|%n", &rcv_counter, &salt_offset);
      if (ret != 1) break; // Check that sscanf succeeded
      
      rcv_data_offset += salt_offset;
      rcv_len -= salt_offset;
      if (eeprom_counter < rcv_counter && // Don't allow replays
          rcv_counter < eeprom_counter + ROLLING_WINDOW_SIZE && // Limit how far ahead the remote can be
          rcv_len == SALT_LEN &&
          strncmp(res_buff+rcv_data_offset, SALT, rcv_len) == 0) // Check the salt
      {
        EEPROM.put(COUNTER_ADDR, rcv_counter);
        radio_unlock = millis() + RADIO_UNLOCK_TIME_MS;
        Serial.print("Access granted, counter: ");
        Serial.print(eeprom_counter);
        Serial.print(" -> ");
        Serial.println(rcv_counter);
      }
      break;
      
    case UNKNOWN_RESPONSE:
      Serial.print("UNKNOWN_RESPONSE: ");
      Serial.print(res_buff);
      break;
      
    default: break;
  }
}

int parse_response() {
  int res;
  for (res = 0; res < N_RES; res++) { // for each known response string
    for (int i = 0; i < res_i; i++) { // for each character in the response buffer
      if (RES_STRINGS[res][i] == '\0') goto parse_response_match;
      if (RES_STRINGS[res][i] != res_buff[i]) break;
    }
  }
  // Went through all response strings without a match
  return UNKNOWN_RESPONSE;
parse_response_match:
  return res;
}

void process_rcv() {
  sscanf(res_buff + RES_RCV_OFFSET, "%d,%d,%n", &rcv_addr, &rcv_len, &rcv_data_offset);
  rcv_data_offset += RES_RCV_OFFSET; // Add in the offset for convenience
  sscanf(res_buff + rcv_data_offset + rcv_len, ",%d,%d", &rcv_rssi, &rcv_snr);
}

void print_error() {
  int err;
  sscanf(res_buff + RES_ERR_OFFSET, "%d", &err);
  switch(err) {
    default:
    case -1: // Couldn't parse the error number
      Serial.print("Error: ");
      Serial.print(res_buff); // Just throw it back
      break;
    // Errors 1&2 must be suppressed since they are sent back any time Serial.println is used
    case 1: break; // Serial.println("There is not \"enter\" or 0x0D 0x0A in the end of the AT Command.");
    case 2: break; // Serial.println("The head of AT command is not \"AT\" string.");
    case 3: Serial.println("Error: There is not \"=\" symbol in the AT command."); break;
    case 4: Serial.println("Error: Unknown command."); break;
    case 10: Serial.println("Error: TX is over times."); break;
    case 11: Serial.println("Error: RX is over times."); break;
    case 12: Serial.println("Error: CRC error."); break;
    case 13: Serial.println("Error: TX data more than 240bytes."); break;
    case 15: Serial.println("Error: Unknown error."); break;
  }
}
