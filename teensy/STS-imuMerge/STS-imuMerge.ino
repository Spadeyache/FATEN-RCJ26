// =============================================================================
//  STS-imuMerge.ino — Main entry point
//  State machine: FOLLOWING_LINE → EXECUTING_TURN / EVACUATION_ZONE / STALLED_RED
//
//  File layout:
//    config.h      — all tunable constants
//    globals.h     — CommandFilter class + extern declarations for shared vars
//    Sensors.ino   — owns pitch/roll/yaw; IMU init + update
//    Comms.ino     — owns xiao/xiaoCommand/xiaoLineError/cmdFilter; XIAO link
//    Drive.ino     — STS motors + IntervalTimer ISR + runLinePID()
//    Actions.ino   — executeTurn(), doUTurn(), enterEvacuationZone(), ARM helpers
// =============================================================================

#include "globals.h"   // all types, enums, and extern declarations
#include "yacheMPU6050.h"

// ---------------------------------------------------------------------------
//  Forward declarations — listed here as a readable module API reference.
//  (Arduino's auto-prototype generator can fail on default parameters.)
// ---------------------------------------------------------------------------

// Sensors.ino
void initSensors();
void updateSensors();

// Comms.ino
void initComms();
void updateComms(bool instantRun = false);

// Drive.ino
void initDrive();
void motor(float32_t left, float32_t right);
void runLinePID();

// Actions.ino
void initActions();
void executeTurn(float angle, bool blocking = false);
void doUTurn();
void enterEvacuationZone();
void handleTurnTick();
void handleEvacuationZone();
void grabARM(bool closed);
void liftARM(bool lift);
void execForward(float speed, float distance_mm, bool usePID = false);

// Mapping.ino
void mappingInit(bool restart);
void mappingTick();
void mappingPersist();
void mappingHandleSerial(char c);

// RobotState enum is defined in globals.h
RobotState robotState    = FOLLOWING_LINE;
bool       isBusyTurning = false; // Set true during turns; blocks new command dispatch
bool       disableGreen  = false; // Set true after a green turn; prevents re-triggering the same intersection

static unsigned long _disableGreenStart = 0; // Timestamp when disableGreen was set

// ---------------------------------------------------------------------------
//  setup
// ---------------------------------------------------------------------------
FLASHMEM void setup() {
    Serial.begin(115200);

    analogWrite(BUZZER_PIN, 30);

    pinMode(PIN_74HCT126_EN , OUTPUT);
    digitalWrite(PIN_74HCT126_EN , HIGH);


    initActions();  // Servos, buzzer, KRS, startup beep   (Actions.ino)
    initDrive();    // STS motors + control timer          (Drive.ino)
    initSensors();    // IMU (DMP on Wire1)  & touch       (Sensors.ino)
    initComms();    // XIAO & K230 serial                  (Comms.ino)

    // Confirmation beep after motors are ready
    delay(50); analogWrite(BUZZER_PIN, 160); delay(40);analogWrite(BUZZER_PIN, 0);
}

// ---------------------------------------------------------------------------
//  loop
// ---------------------------------------------------------------------------
void loop() {
    // delay(20);   // 50 Hz main loop (was 100 ms / 10 Hz — too slow for stable PID)
    updateComms();    // Parse XIAO packets → xiaoCommand, xiaoLineError & // Send run/idle cmd; parse K230D detections → detections[]
    updateSensors();  // Update pitch / roll / yaw from DMP & TOUCH, CONDCT update

    // Debug Serial commands ('m' = map dump, 'p' = pose print)
    while (Serial.available()) mappingHandleSerial((char)Serial.read());

    switch (robotState) {

        // ------------------------------------------------------------------
        case FOLLOWING_LINE:
        // ------------------------------------------------------------------
            // Auto-reset disableGreen after cooldown elapses
        if (disableGreen && millis() - _disableGreenStart >= DISABLE_GREEN_MS) {
            disableGreen = false;
            analogWrite(BUZZER_PIN, 0);
            Serial.println("Green re-enabled");
        }

        static unsigned long lastTouch = 0;    // ------ THOUCH -------------
        if(touchfront && millis() - lastTouch >= 20){
            Serial.println("touch - detected");
            motor(0,0);
            delay(800);
            updateSensors();
            if(touchfront){
                execForward(-70, 50);
                executeTurn(-80.0f, true);
                cmdFilter.clear(); 
                while(xiaoCommand != 6){
                    updateComms();
                    updateSensors();
                    if(conduct0){
                        analogWrite(BUZZER_PIN, 80);
                        executeTurn(-20,true);
                        execForward(80, 2);
                    }
                    analogWrite(BUZZER_PIN, 0);
                    motor(100,7);
                }

            }
            lastTouch = millis();
            break;
        }
        
        if (!isBusyTurning) {
            // Priority: U-Turn > Intersection (L/R) > Red > Silver > PID
            if      (xiaoCommand == 1) { doUTurn(); }                               // u turn
            else if (xiaoCommand == 2 && !disableGreen) {                           // turn left
                // analogWrite(BUZZER_PIN, 60); // stays on until handleTurnTick() finishes
                execForward(70, 28); executeTurn(-90.0f);
                disableGreen = true;
            }
            else if (xiaoCommand == 3 && !disableGreen) {                                       // turn right
                // analogWrite(BUZZER_PIN, 160); // stays on until handleTurnTick() finishes
                execForward(70, 28); executeTurn(90.0f);
                disableGreen = true;
            }
            else if (xiaoCommand == 4) { robotState = STALLED_RED;  }  // red
            else if (xiaoCommand == 5) { enterEvacuationZone();     }  // silver
            else if (xiaoCommand == 6 || xiaoCommand == 7) { 
                Serial.printf("CmdFilter votes | U:%u L:%u R:%u Red:%u Slv:%u Blk:%u | Cmd:%u Err:%.1f\n",
                    cmdFilter.votesUturn, cmdFilter.votesLeft, cmdFilter.votesRight,
                    cmdFilter.votesRed, cmdFilter.votesSilver, cmdFilter.votesBlack,
                    xiaoCommand, xiaoLineError);

                motor(0,0); xiao.send(XIAO_REG_MODE, XIAO_MODE_NOGI); cmdFilter.clear(); delay(200);  // no green intersection
                for (int i = 0; i < 15; i++) {
                    updateComms(true);
                }
                Serial.printf("CmdFilter votes | Blk:%u", cmdFilter.votesBlack);

                if(xiaoCommand == 6){
                    analogWrite(BUZZER_PIN, 80);// buzzer if intersection.
                    execForward(100,40);
                }  else{
                    // execForward(-70, 35); // for now we just go back and go no intersection line follow.
                    //turn to the direction of 90 deg turn.
                }

                xiao.send(XIAO_REG_MODE, XIAO_MODE_LINE);
                delay(200);
                cmdFilter.clear(); disableGreen = true; _disableGreenStart = millis(); runLinePID();} // i need to distinguish 90 and T.
            else                       { runLinePID(); } // no command → follow line
        }
        break;

        // ------------------------------------------------------------------
        case EXECUTING_TURN:
        // ------------------------------------------------------------------
            handleTurnTick(); // Returns to FOLLOWING_LINE when duration elapses
            _disableGreenStart = millis();
            break;

        // ------------------------------------------------------------------
        case EVACUATION_ZONE:
        // ------------------------------------------------------------------
            handleEvacuationZone(); // Beeps; exits when XIAO stops seeing silver
            break;

        // ------------------------------------------------------------------
        case STALLED_RED:
        // ------------------------------------------------------------------
            motor(0, 0);
            if (xiaoCommand != 4) {
                cmdFilter.clear();
                xiaoCommand = 0;
                robotState = FOLLOWING_LINE;
                Serial.println("Red cleared → FOLLOWING_LINE");
            }
            break;
    }

    // Debug at ~10 Hz
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug >= 10) {
        // Serial.printf("State:%d Cmd:%u Err:%.1f Busy:%d NoGrn:%d | U:%u L:%u R:%u Red:%u Slv:%u | K230:%s dets:%u\n",
        //               (int)robotState, xiaoCommand, xiaoLineError,
        //               (int)isBusyTurning, (int)disableGreen,
        //               cmdFilter.votesUturn, cmdFilter.votesLeft,
        //               cmdFilter.votesRight, cmdFilter.votesRed, cmdFilter.votesSilver,
        //               k230Running ? "RUN" : "IDL", detectionCount);

        Serial.printf("CmdFilter votes | U:%u L:%u R:%u Red:%u Slv:%u Blk:%u | Cmd:%u Err:%.1f\n",
                      cmdFilter.votesUturn, cmdFilter.votesLeft, cmdFilter.votesRight,
                      cmdFilter.votesRed, cmdFilter.votesSilver, cmdFilter.votesBlack,
                      xiaoCommand, xiaoLineError);



        lastDebug = millis();
    }
}
