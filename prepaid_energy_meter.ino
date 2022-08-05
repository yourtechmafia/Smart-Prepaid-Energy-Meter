/* All you need to configure Blynk cloud with SIM800 GSM module
    Fill in the credentials obtained from your Blynk account
*/
#define BLYNK_TEMPLATE_ID " " //  Blynk template ID from your blynk.io account
#define BLYNK_DEVICE_NAME "Energy Meter"
#define BLYNK_AUTH_TOKEN " " // Blynk auth token from your blynk.io account
#define TINY_GSM_MODEM_SIM800 //  TinyGSM SIM800L module library
//  Include all necessary libraries
#include <TinyGsmClient.h> //  TinyGSM module client
#include <BlynkSimpleTinyGSM.h> //  TinyGSM Blynk library
#include <SPI.h>  //  SPI library
#include <SD.h> //  SD card library, SD module pins on Mega2560 - 50 - MISO, 51 - MOSI, 52 - SCK, 53 - CS
#include <EEPROMex.h> //  Modified EEPROM library that allows to store floats, doubles, strings, etc.
#include <LiquidCrystal.h>  //  LCD module library & config
#include <Keypad.h> //  Keypad module library
#include "ACS712.h" //  ACS712 library
//  Libraries constants for initialization
const char apn[] = " ", user[] = "", pass[] = ""; //  Set your ISP apn value here, username and password are both optional
const char keys[4][4] = { //  Keypad 4x4 config
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
const byte rowPins[4] = {A8, A9, A10, A11}; //  Keypad row pins on board
const byte colPins[4] = {A12, A13, A14, A15}; //  Keypad column pins on board
//  Initialize all libraries
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4); //  Initialize keypad module
TinyGsm modem(Serial2); //  SIM800L module placed on Serial2 of Mega2560
LiquidCrystal lcd(13, 12, 11, 10, 9, 8);  //  LCD module pins on board - RS,EN,D4,D5,D6,D7
ACS712  ACS(A3, 5.0, 1023, 185);  //  20A used with 185mA sensitivity. Sensor is on Pin A3
//  IDE constants
#define buzzer 4  //  Buzzer for communication
#define red_led 5 //  Red led to indicate low energy token
#define green_led 6 //  Green led to indicate enough energy token
#define power_read A2 //  5V input pin filtered from mains to check electricity supply status
#define relayLight A4 //  A relay to toggle the light-circuit
#define relayPower A5 //  A relay to toggle the power-circuit
#define relayHeat A6   //  A relay to toggle the heat-circuit
//  Board output pins
const int8_t outputPins[] = {buzzer, red_led, green_led, relayLight, relayHeat, relayPower};
//  Prepaid meter test tokens
const char five_unit_token[10] = "0529018201", twenty_unit_token[10] = "2018509281";
const char ten_unit_token[10] = "1029182738", fifty_unit_token[10] = "5013930183";
//  Global variables
bool localMode; //  Boolean to set device log mode, either locally to SD card or online to Blynk cloud
bool relayLight_state, relayHeat_state, relayPower_state;  // Variables to toggle individual circuit
byte reset_energy_variables;  //  Variable that resets energy variables
char str1[10];  //  Universal array to store input from keypad
int8_t n; //  Universal keypad counter
int voltage, temp_voltage;  //  Voltage(s) variable
double temp_energy_used;  //  Stored energy used temporarily
double current, temp_current; //  Current(s) variables
double a_power, temp_a_power; //  Active power variables
double r_power, temp_r_power; //  Reactive power variables
double app_power, temp_app_power;  //  Apparent power variables
volatile float energy_used = EEPROM.readFloat(5); //  Initialize the value of energy used stored on the EEPROM address 5
volatile float energy_balance = EEPROM.readFloat(10); //  Initialize the value of energy balance stored on the EEPROM address 10
unsigned long read_data, update_blynk, update_sd_card;  //  Time counters to trigger actions in loop()

void setup() {
  lcd.begin(20, 4); lcd.clear();  //  Initialize LCD and clear screen
  ACS.autoMidPoint(); //  Configure ACS module
  //  Configure board pins
  for (byte i = 0; i < 6; i++) pinMode(outputPins[i], OUTPUT);
  pinMode(power_read, INPUT);
  digitalWrite(red_led, 1); //  Turn on red light to indicate power
  //  Prompt the user to either log locally on SD card or online to blynk cloud
  lcd.print("1 - Local Operation");
  lcd.setCursor(0, 1);  lcd.print("2 - Connect to Web");
  lcd.setCursor(2, 3);  lcd.print("Press '1' or '2'  ");
  digitalWrite(buzzer, 1);  delay(2000);  digitalWrite(buzzer, 0);
  while (1) { //  Keypad prompt for input, infinite until key '1' or '2' is pressed
    char key = keypad.getKey();
    if (key == '1') { //  Pressing key 1 indicates you choose to log data locally on SD card
      lcd.clear();  lcd.print("Prepaid Energy Meter");
      lcd.setCursor(0, 1);  lcd.print("kW, kVAR, kVA, (kWh)");
      lcd.setCursor(2, 3);  lcd.print("Local Connection");
      SD.begin(53); //  Initializes SD card if present
      localMode = 1;  //  Used in loop to trigger SD card data logging actions
      Serial.begin(9600); //  Set the serial monitor baud rate
      break;
    } else if (key == '2') { //  Pressing key 2 indicates you choose to log data online to blynk cloud
      lcd.clear();  lcd.print("Prepaid Energy Meter");
      lcd.setCursor(0, 1);  lcd.print("kW, kVAR, kVA, (kWh)");
      lcd.setCursor(1, 3);  lcd.print("Connecting to Web..");
      Serial2.begin(115200);  //  Set baud rate for modem on Serial2
      modem.restart();  //  Initializes modem
      Blynk.begin(BLYNK_AUTH_TOKEN, modem, apn, user, pass);  //  Initiate connection to blynk cloud
      digitalWrite(buzzer, 1);
      localMode = 0;  //  Used in loop to sync data into Blynk cloud
      lcd.clear();
      break;
    }
  }
  digitalWrite(red_led, 0);  digitalWrite(green_led, 1);
  delay(1000);  digitalWrite(buzzer, 0);
  digitalWrite(green_led, 0);
  lcd.clear();  //  Clear screen
  updateLCD();  //  update LCD with new values
  read_data = update_blynk = update_sd_card = millis(); //  Reset the action-triggering time counters
}

void loop() {
  if (localMode) {  //  Local mode chosen, logs data locally,
    keyPad(); //  Checks input from keypad
    UpdateSDCard(); //  Run at intervals to log data into SD card
  } else {  //  Online mode chosen, log data into Blynk server
    Blynk.run();  //  Maintain steady connection with Blynk server
    updateBlynkCloud(); //  Runs at intervals to log data into Blynk cloud
  }
  if (!digitalRead(power_read)) { //  Checks if there is electricity from mains
    EEPROM.updateFloat(5, energy_used);
    EEPROM.updateFloat(10, energy_balance);
    /* Since there is no electricity, all parameters measured
        must be equal to zero to ensure precision  */
    relayLight_state = relayHeat_state = relayPower_state = 0;
    voltage = current = a_power = r_power = app_power = 0;
    digitalWrite(red_led, 0);
    digitalWrite(green_led, 0);
  } else {  //  Electricity is available
    if ((energy_used > energy_balance) || (energy_balance == 0)) { // low amount of energy unit, then
      relayLight_state = relayHeat_state = relayPower_state = 0;  //  turn off all relays
      voltage = random(215, 220); //  Randomly selects a voltage value, replace if you have a sensor value
      current = a_power = r_power = app_power = 0;  //  Ensures all data variables stay at 0
      digitalWrite(red_led, 1); //  Turn on red light which indicates low token balance
      digitalWrite(green_led, 0); //  Turns off green light which indicates enough token balance
    } else {  //  Energy unit is available
      readData(); //  Read and evaluate data from sensors
      digitalWrite(red_led, 0); //  Turn off red light which indicates low token balance
      digitalWrite(green_led, 1); //  Turns on green light which indicates enough token balance
    }
  }
  updateLCD();  //  Update all LCD values
  /* Enable all relay outputs to be toggled
    on since there is enough token balance   */
  digitalWrite(relayLight, relayLight_state);
  digitalWrite(relayHeat, relayHeat_state);
  digitalWrite(relayPower, relayPower_state);
}

void keyPad() { //  Continuously checks input from keypad
  char key = keypad.getKey();
  if (key == 'A') relayLight_state = !relayLight_state; //  Toggle light-load relay upon key A press
  else if (key == 'B') {  //  Check and display energy balance upon key B press
    lcd.clear();
    if (energy_balance < 0)  energy_balance = 0;  //  No negative value for energy balance
    lcd.setCursor(1, 1);  lcd.print("Bal = ");
    lcd.print(energy_balance, 5);  lcd.print("kWh");  //  Display the current value for energy balance
    lcd.setCursor(3, 3);  lcd.print("Last R = ");
    lcd.print(EEPROM.read(2));  //  Also displays the stored value of last amount of energy unit recharged
    lcd.print("kWh");
    delay(2000); lcd.clear(); //  2 seconds delay before clearing screen
  }
  else if (key == 'C')  relayHeat_state = !relayHeat_state; //  Toggle heat-load relay
  else if (key == 'D')  relayPower_state = !relayPower_state; //  Toggle power-load relay
  else if (key == '*') {  //  Reset energy balance and energy used variables to zero
    reset_energy_variables++;
    if (reset_energy_variables == 2) {  //  Resets variables when button is pressed twice
      digitalWrite(buzzer, 1);
      reset_energy_variables = energy_used = energy_balance = 0;  //  Reset energy measurement variables
      EEPROM.updateFloat(5, 0); //  Resets the stored value of energy used to 0
      EEPROM.updateFloat(10, 0); //  Resets the stored value of energy balance to 0
      delay(2000);  digitalWrite(buzzer, 0);
    }
  } else if (key == '#') {  //  Key # prompts the input for energy recharge token
    lcd.clear();  lcd.print("  Enter your token");
    lcd.setCursor(4, 2);
    unsigned long display_time = millis();  //  Counter to check the time alloted for the recharge process, clears screen after 20s
    while (millis() - display_time <= 20000) {  //  Clears screen after 20s to avoid infinite display
      char key = keypad.getKey();
      if (isdigit(key)) {
        lcd.print(key); str1[n] = key;  n++;
        digitalWrite(buzzer, 1); delay(100);  digitalWrite(buzzer, 0);
        if (n == 5)  lcd.print("--");
        if (n >= 11) {  //  Max number of digits is 10
          strncpy(str1, "", 10);  //  Reset key array
          n = 0;  //  Reset key counter
          lcd.clear();
          break;
        }
      } else if (key == 'C') {  //  Cancel the energy recharge operation
        strncpy(str1, "", 10);
        n = 0;
        lcd.clear();
        break;
      }
      else if (key == '*') {  //  Clear the inputted digits on screen and reset key counter
        strncpy(str1, "", 10);
        n = 0;
        lcd.setCursor(4, 2);
        lcd.print("              ");
        lcd.setCursor(4, 2);
      }
      else if (key == '#') {    //  Compare the keys input with stored energy tokens characters
        lcd.clear();  digitalWrite(buzzer, 1);
        if (n == 10) {  //  Checks if the digits are 10 - Each stored token has 10 character
          lcd.setCursor(1, 1);
          if (!strncmp(str1, five_unit_token, 10)) { // Checks if the token received is that of 5-units
            lcd.print("5-kWh Energy token");
            lcd.setCursor(1, 3);  lcd.print("Successfully added");
            energy_balance += 5;  //  Increase the energy balance +5 units
            EEPROM.update(2, 5);  //  Stores the amount of unit recharged on EEPROM
          } else if (!strncmp(str1, ten_unit_token, 10)) { // Checks if the token received is that of 10-units
            lcd.print("10kWh Energy token");
            lcd.setCursor(1, 3);  lcd.print("Successfully added");
            energy_balance += 10;
            EEPROM.update(2, 10);
          } else if (!strncmp(str1, twenty_unit_token, 10)) { //  Checks if the token received is that of 20-units
            lcd.print("20kWh Energy token");
            lcd.setCursor(1, 3);  lcd.print("Successfully added");
            energy_balance += 20;
            EEPROM.update(2, 20);
          } else if (!strncmp(str1, fifty_unit_token, 10)) { // Checks if the token received is that of 50-units
            lcd.print("50kWh Energy token");
            lcd.setCursor(1, 3);  lcd.print("Successfully added");
            energy_balance += 50;
            EEPROM.update(2, 50);
          } else {  //  Runs if the token is not stored or incorrect
            lcd.setCursor(3, 1);  lcd.print("Invalid token!");
            lcd.setCursor(2, 3);  lcd.print("Please try again");
          }
          EEPROM.updateFloat(10, energy_balance); //  Store the new value for energy balance
        } else {  //  Invalid token
          lcd.clear();
          lcd.setCursor(3, 1);  lcd.print("Invalid token!");
          lcd.setCursor(2, 3);  lcd.print("Please try again");
        }
        strncpy(str1, "", 10);  n = 0;  //  Reset key array and counter
        delay(2000);
        digitalWrite(buzzer, 0);  
        lcd.clear();  
        break;
      }
    }
  }
}

void readData() { // Fetch electricity data from sensors
  if (millis() - read_data >= 1000) {  // Only obtain data every 1 second
    read_data = millis();
    current = ACS.mA_AC() * 0.001;  //  Obtain current readings from ACS712 sensor and convert from mA to Ampere
    voltage = random(215, 220); //  Randomly selects a voltage value, replace if you have a sensor value
    app_power = voltage * current / 1000; //  Evaluate the apparent power from current and voltage readings
    /* If all relays are off at the same time,
      then, apparently, these values must stay at 0 */
    if ((!relayLight_state) && (!relayHeat_state) && (!relayPower_state)) current = a_power = r_power = app_power = 0;
    /* The calculations here are based on some rules of
         electricity and power measurement I came across  */
    else if (!relayPower_state) { //  If the power circuit relay is off, then:
      r_power = 0;  //  Reactive power must be zero
      /* If the relay toggling the light and heat loads are on
         active and apparent power are equal  */
      if ((relayLight_state) || (relayHeat_state)) a_power = app_power;
    } else if (relayPower_state) {  //  If the power circuit relay is on, then:
      /* There must be a power factor
         Generate random value for the power factor
         based on the current consumed
      */
      double pf;
      if (current < 1.0) pf = random(0.01, 0.4);
      else if (current > 1.0 && current < 3.0) pf = random(0.4, 0.8);
      else if (current > 3.0)  pf = random(0.8, 0.9);
      a_power = app_power * pf; //  Evaluates active power
      r_power = sqrt((app_power * app_power) - (a_power * a_power));  //  Evaluates reactive power
    }
    energy_used += (app_power / 3600);  //  Evaluate the new value of energy used
    energy_balance -= (app_power / 3600); //  Evaluate the new value of energy balance
    /* These variables are used to store data obtained every second
        into a minute and evaluate the average values later
    */
    temp_voltage += voltage;  temp_current += current;
    temp_app_power += app_power;  temp_a_power += app_power;
    temp_r_power += r_power;  temp_energy_used += energy_used;
  }
}

void updateLCD() {  //  This function updates the 20x4 LCD with all new data
  lcd.setCursor(0, 0);  lcd.print(voltage);
  lcd.print("V | ");
  if (current < 10)  lcd.print(current, 2);
  else  lcd.print(current, 1);
  lcd.print("A | (");
  lcd.print(relayLight_state);  lcd.print(relayHeat_state);
  lcd.print(relayPower_state);
  lcd.write(')');
  lcd.setCursor(0, 1);  lcd.print("  kW   | kVAR |  kVA");
  lcd.setCursor(0, 2);  lcd.print(a_power, 4);
  lcd.print(" | ");
  lcd.print(r_power);  lcd.print(" | ");
  lcd.print(app_power);
  lcd.setCursor(3, 3);  lcd.print("E = ");
  lcd.print(energy_used, 5);  lcd.print("kWh");
}

void updateBlynkCloud() { //  Update the Blynk cloud
  if (millis() - update_blynk >= 1000) {  //  Only run the update every 1 second to avoid overloading and disconnection
    update_blynk = millis();
    Blynk.virtualWrite(V3, voltage);
    Blynk.virtualWrite(V4, current);
    Blynk.virtualWrite(V5, app_power);
    Blynk.virtualWrite(V6, a_power);
    Blynk.virtualWrite(V7, r_power);
    if (!digitalRead(power_read)) Blynk.virtualWrite(V8, 0);
    else  Blynk.virtualWrite(V8, 1);
    Blynk.virtualWrite(V9, energy_used);
    Blynk.virtualWrite(V10, energy_balance);
  }
}

void UpdateSDCard() { //  Update the SD card
  if (millis() - update_sd_card >= 60000) {  // Run the update every 60 seconds - 1 minute
    update_sd_card = millis();
    // Print the data to the serial monitor
    Serial.println("--------1 minute average values log----------");
    Serial.print("Voltage (V/10):"); Serial.print(temp_voltage / 60 / 10); Serial.print(", ");
    Serial.print("Current (A):"); Serial.print(temp_current / 60); Serial.print(", ");
    Serial.print("App. Power (kVA):"); Serial.print(temp_app_power / 60); Serial.print(", ");
    Serial.print("Active Power (kW):"); Serial.print(temp_a_power / 60); Serial.print(", ");
    Serial.print("Reactive Power (kVAR):"); Serial.print(temp_r_power / 60); Serial.print(", ");
    Serial.print("Energy Used (kWh):"); Serial.print(temp_energy_used / 60); Serial.print(", ");
    Serial.println();
    // Log the data to the SD card if available
    File dataFile = SD.open("PowerEnergyLOG.txt", FILE_WRITE);
    dataFile.println("--------1 minute average values log----------");
    dataFile.println("Average Voltage (1 minute) is " + String(temp_voltage / 60) + "V.");
    dataFile.println("Average Current (1 minute) is " + String(temp_current / 60) + "A.");
    dataFile.println("Average Apparent Power (1 minute) is " + String(temp_app_power / 60) + "kVA.");
    dataFile.println("Average Active Power (1 minute) is " + String(temp_a_power / 60) + "kW.");
    dataFile.println("Average Reactive Power (1 minute) is " + String(temp_r_power / 60) + "kVAR.");
    dataFile.println("Average Energy Used (1 minute) is " + String(temp_energy_used / 60) + "kWh.");
    dataFile.println("Total Energy Used since reset is " + String(energy_used) + "kWh.");
    dataFile.println("Energy balance is " + String(energy_balance) + "kWh.");
    dataFile.close();
    // Resets the variable evaluating minutes data from seconds data
    temp_voltage = temp_current = temp_app_power = temp_a_power = temp_r_power = temp_energy_used = 0;
  }
}

BLYNK_WRITE(V0) { // Constantly checks for update to the light-circuit switch from blynk web
  relayLight_state = param.asInt();
}

BLYNK_WRITE(V1) { // Constantly checks for update to the heat-circuit switch from blynk web
  relayHeat_state = param.asInt();
}

BLYNK_WRITE(V2) { // Constantly checks for update to the power-circuit switch from blynk web
  relayPower_state = param.asInt();
}
