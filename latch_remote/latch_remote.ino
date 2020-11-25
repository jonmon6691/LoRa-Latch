/********************** latch_remote ************************
 *    LoRa beacon that announces it presence periodically
 *                   Jon Wallace 2020
 */

#define MAX_RES_SIZE (8+5+1+3+1+240+1+3+1+2+2)
char res_buff[MAX_RES_SIZE];
int  res_i = 0;

#include "password.h"

#define BEACON_PERIOD_MS 1000 // Send ping every second
unsigned long beacon;

#define CAR_ON_PIN (7) // Connected to the "USB" pin on the boost/charger
#define TRANSMIT_AFTER_CAR_OFF_MS (15000) // Stop transmitting 15 seconds after car is turned off
unsigned long transmit_limit;

void setup() {
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Latch Remote");
  Serial.println("See https://github.com/jonmon6691/LoRa-Latch for documentation.");
  
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(CAR_ON_PIN, INPUT);
  
  beacon = 0;
  transmit_limit = 0;
}

void loop() {
  char next = Serial.read();
  process_character(next);

  bool car_on = digitalRead(CAR_ON_PIN);
  unsigned long now = millis();
  
  if (car_on == HIGH) {
    transmit_limit = now + TRANSMIT_AFTER_CAR_OFF_MS;
  }

  if (car_on == LOW && now < transmit_limit && now > beacon) {
    beacon = millis() + BEACON_PERIOD_MS;
    Serial.print("AT+SEND=0," PASSWORD_LEN_STR "," PASSWORD "\r\n");
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

void process_response() {
  switch (parse_response()) {
    case RES_READY: break;
    case RES_ERR: break; //print_error(); break;
    case RES_OK: break;
    case RES_RCV: break;
    case UNKNOWN_RESPONSE: break;
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

void print_error() {
  char a = res_buff[5];
  int err = (a >= '0' && a <= '9') ? a - '0' : -1;
  a = res_buff[6];
  if (a >= '0' && a <= '9') err = 10*err + (a - '0');
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
