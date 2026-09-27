#include <HUSKYLENS.h>
#include <HUSKYLENSMindPlus.h>
#include <HuskyLensProtocolCore.h>
#include <Wire.h>
#include <SoftwareSerial.h>




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

    while (!huskylens.setCustomName("Orange", 1)) {
    Serial.println(F("Retrying ID 1 custom name..."));
    delay(100);
  }
  
  // Once the first one succeeds, subsequent writes usually go through instantly
  huskylens.setCustomName("Blue", 2);
  huskylens.setCustomName("Purple", 3);
  huskylens.setCustomName("Green", 4);
  huskylens.setCustomName("Cyan", 5);
  huskylens.setCustomName("Red", 6);

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

            switch (result.ID)
            {
                case 1: Serial.println("Orange"); break;
                case 2: Serial.println("Blue");   break;
                case 3: Serial.println("Purple"); break;
                case 4: Serial.println("Green");  break;
                case 5: Serial.println("Cyan");   break;
                case 6: Serial.println("Red");    break;
                default: Serial.println("Unknown"); break;
            }    
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
