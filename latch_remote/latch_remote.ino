/********************** latch_remote ************************
 *    LoRa beacon that announces it presence periodically
 *                   Jon Wallace 2020
 */
 
#include <EEPROM.h>
#include "secrets.h"

#define MAX_RES_SIZE (8+5+1+3+1+240+1+3+1+2+2)
char res_buff[MAX_RES_SIZE];
int  res_i = 0;

#define BEACON_PERIOD_MS 1000 // Send ping every second
unsigned long beacon;

#define CAR_ON_PIN (7) // Connected to the "USB" pin on the boost/charger
#define COUNTER_RESET_PIN 11
#define COUNTER_RESET_GND_PIN 12

bool lora_initd;

#define COUNTER_ADDR (0) // Address of the counter in the EEPROM
unsigned int counter;

#define TRANSMIT_AFTER_CAR_OFF_MS (15000) // Stop transmitting 15 seconds after car is turned off
unsigned long transmit_limit;

void setup() {
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Latch Remote");
  Serial.println("See https://github.com/jonmon6691/LoRa-Latch for documentation.");
  
  lora_initd = false;
  Serial.print("AT+RESET\r\n");
  
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(CAR_ON_PIN, INPUT);

  pinMode(COUNTER_RESET_PIN, INPUT);      // Resets counter in EEPROM when shorted to GND
  digitalWrite(COUNTER_RESET_PIN, HIGH);

  digitalWrite(COUNTER_RESET_GND_PIN, LOW);    // Convinient GND for shorting the counter reset pin
  pinMode(COUNTER_RESET_GND_PIN, OUTPUT);

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

  if (lora_initd && car_on == LOW && now < transmit_limit && now > beacon) {
    beacon = now + BEACON_PERIOD_MS;
    counter = EEPROM.get(COUNTER_ADDR, counter);

    char buff[10 + 1 + SALT_LEN]; // MAX_INT + '|' + password
    int len = sprintf(buff, "%u|%s", counter, SALT);
    sprintf(res_buff, "AT+SEND=0,%d,%s\r\n", len, buff);

    counter += 1;
    EEPROM.put(COUNTER_ADDR, counter);

    Serial.print(res_buff);
  }

  if (digitalRead(COUNTER_RESET_PIN) == LOW) {
    unsigned int counter = 1;
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

void process_response() {
  switch (parse_response()) {
    case RES_READY: 
      lora_initd = true;
      Serial.print("AT+CPIN=" SECRET "\r\n");
      break;
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
