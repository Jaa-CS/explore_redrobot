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

bool close_servo = true;
static int color_id;
// แหวกกลางให้หินกระจาย
// start_ceremony();

static bool pick_up_mode = true;
static bool placing_mode = false;

long long box_size;
long long minimum_box_size

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

        // โหมดเก็บหิน
        while (pick_up_mode) {
          Serial.println("Pick up mode started!\n");

          // ยัง detected หิน ไม่เจอ 
          // ถ้า block_count เป็น 0 จะ true อื่นๆ จะ false
          while (!huskylens.countBlocks() && result.command != COMMAND_RETURN_BLOCK) {
            // หันจนกว่าจะเจอ
            // turn()
          }

          while (result.command == COMMAND_RETURN_BLOCK && (result.xCenter < 120 || result.xCenter > 200) ) {
            // หันจนกว่าหินจะอยู่กลางframe กล้อง
            if (result.xCenter < 120) {
                // turnRight();
            else if (result.xCenter > 200) {
                // turnLeft();
            }
            }

          }
          
          // check ว่า huskylens detect เจอหิน และหินอยู่บริเวณกลางๆแนวนอนของ กล้อง
          if (result.command == COMMAND_RETURN_BLOCK && result.xCenter >= 120 && result.xCenter <= 200 ) {
              Serial.println("Stone detected!/n");
              
              // กางแขน servo เพื่อเตรียมเก็บหิน
              close_servo = false;
              Serial.println("Spreading arms!\n");
              box_size = result.width * result.height;
          

              while (box_size <= minimum_box_size) {
                  // เดินไปจนกว่าจะใกล้หินพอ
                  Serial.println("Still can't pick up the stone, Need to go closer!\n");
                  // forward();


                  // ถ้าอยู่ใกล้พอ(ขนาด detected box ใหญ่พอ) งับแขนเพื่อครอบหิน
                  if (box_size > minimum_box_size && huskylens.countBlocks() == 1) {
                      Serial.println("I can pick up the stone now!\n");
                    
                    // print before picking the stone
                    // If the result received from HuskyLens is a block/object detection result, then run the code inside this
                      if (result.command == COMMAND_RETURN_BLOCK)
                      {   
                          Serial.print("Picking stone, ")
                          Serial.print("ID = ");
                          Serial.println(result.ID);

                          if (result.ID == 1)
                          {
                              Serial.println("Orange");
                          }
                          else if (result.ID == 2)
                          {
                              Serial.println("Blue");
                          }
                          else if (result.ID == 3)
                          {
                              Serial.println("Purple");
                          }
                          else if (result.ID == 4)
                          {
                              Serial.println("Green");
                          }
                          else if (result.ID == 5)
                          {
                              Serial.println("Cyan");
                          }
                          else if (result.ID == 6)
                          {
                              Serial.println("Red");
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
                          Serial.printIn("box_size=")
                          Serial.print(result.width * result.height);
                          Serial.print("\n");
                          

                      }
                      // ครอบเก็บหิน

                      Serial.println("Closing arms!\n");
                      close_servo = true;
                      // เปลี่ยนไปโหมดวางหิน
                      Serial.println("Switching to placing mode!\n");
                      pick_up_mode = false;
                      placing_mode = true;

                      break;
                  
                  
                  }
                                                  }

                                
              
              
              
              }
        }
        


        
    }

    delay(5000);



}
