#include <SoftwareSerial.h>
#include <DFRobot_HuskyLens.h>
#include <HUSKYLENS.h>
#include <HUSKYLENSMindPlus.h>
#include <HuskyLensProtocolCore.h>
#include <Wire.h>


HUSKYLENS huskylens;

void setup() {
  // put your setup code here, to run once:
    Serial.begin(9600);
    Wire.begin();

    Serial.println("Connecting to HuskyLens...");

    while (!huskylens.begin(Wire))
    {
        Serial.println("HuskyLens connection failed!");
        delay(1000);
    }

    Serial.println("HuskyLens connected!");

    while (!huskylens.setCustomName("Purple", 1)) {
    Serial.println(F("Retrying ID 1 custom name..."));
    delay(100);
  }
  
  // Once the first one succeeds, subsequent writes usually go through instantly
  huskylens.setCustomName("Cyan", 2);
  huskylens.setCustomName("Red", 3);
  huskylens.setCustomName("Gold", 4);
  huskylens.setCustomName("Blue", 5);
  huskylens.setCustomName("Green", 6);

}

void loop() {

   if (!huskylens.request())
    {
        Serial.println("Request failed!");
        delay(100);
        return;
    }

    while (huskylens.available())
    {
        HUSKYLENSResult result = huskylens.read();

        if (result.command == COMMAND_RETURN_BLOCK)
        {
            Serial.print("ID = ");
            Serial.println(result.ID);

            if (result.ID == 1)
            {
                Serial.println("PURPLE");
            }
            else if (result.ID == 2)
            {
                Serial.println("CYAN");
            }
            else if (result.ID == 3)
            {
                Serial.println("RED");
            }
            else if (result.ID == 4)
            {
                Serial.println("GOLD");
            }
            else if (result.ID == 5)
            {
                Serial.println("BLUE");
            }
            else if (result.ID == 6)
            {
                Serial.println("GREEN");
            }

            Serial.print("Block: ");
            Serial.print("x=");
            Serial.print(result.xCenter);
            Serial.print(", y=");
            Serial.print(result.yCenter);
            Serial.print(", width=");
            Serial.print(result.width);
            Serial.print(", height=");
            Serial.print(result.height);
            Serial.print("\n");
            

        }
        else if (result.command == COMMAND_RETURN_ARROW)
        {
            Serial.println("Arrow detected");
        }
    }

    delay(1000);



}
