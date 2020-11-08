/************** latch_control ***************
 *    Interface module to a central buzzer
 *    and door latch system in some apts
 *          Jon Wallace 2020
 */

#define MAX_RES_SIZE (263)
char res_buff[MAX_RES_SIZE];
int res_i = 0;

// Populated by process_rcv() in the RES_RCV case
int rcv_addr;
int rcv_len;
int rcv_data_offset;
int rcv_rssi;
int rcv_snr;

#include "password.h"
#define LATCH_PIN 8
#define BUZZER_PIN 6

#define RADIO_UNLOCK_TIME_MS ((unsigned long) 5*60*1000) // Buzzer unlock is active for 5 minutes since last radio contact
#define HOLD_TIME_MS ((unsigned long) 3 * 1000) // Hold the door latch for 3 seconds after pressing the buzzer

unsigned long radio_unlock;
unsigned long hold_latch;

void setup() {
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Latch Controller");
  Serial.println("See https://github.com/jonmon6691/LoRa-Latch for documentation.");
  
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(LATCH_PIN, LOW);    // Door latch relay driver
  pinMode(LATCH_PIN, OUTPUT);
  
  pinMode(BUZZER_PIN, INPUT);      // Buzzer detect pull-up resistor
  digitalWrite(BUZZER_PIN, HIGH);

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
      break;
      
    case RES_ERR: print_error(); break;
    case RES_OK: Serial.println("RES_OK"); break;
    
    case RES_RCV:
      process_rcv();
      if (rcv_len == PASSWORD_LEN && strncmp(res_buff+rcv_data_offset, PASSWORD, rcv_len) == 0) {
        radio_unlock = millis() + RADIO_UNLOCK_TIME_MS;
        Serial.print("Unlocking until ");
        Serial.println(radio_unlock);
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
  sscanf(res_buff + rcv_data_offset, ",%d,%d", &rcv_rssi, &rcv_snr);
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

