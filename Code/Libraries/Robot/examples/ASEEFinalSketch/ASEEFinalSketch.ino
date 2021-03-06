#include <Servo.h>
#include <L3G.h>
#include <EEPROMAnything.h>
#include <eeprom.h>
#include <Wire.h>
#include <Pixy.h>
#include <SPI.h>
#include <Drivetrain.h>
#include <Sensors.h>
#include <FishManager.h>
#include <Robot.h>

/**********************
 * TESTING CONDITIONS *
 *****************************************************************************************************************************
 * Enable or disable different modules and set the test parameter to test various modules together and separately.           *
 * The description tells what the test does and the modules required tells what needs to be activated (true). All            *
 * modules not mentioned must be disabled.                                                                                   *
 *****************************************************************************************************************************
 * testParam | Description                                                                  | Modules required               *
 *___________|______________________________________________________________________________|________________________________*
 *     1     | Final test of the robot! This is the final product. Goes around the track and| All                            *
 *           | picks up all the fish, storing them inside. Dumps them all off too.          |                                *
 *___________|______________________________________________________________________________|________________________________*
 *     2     | Test PID and gyro. Goes to closest fish seen and stops in front of it.       | eyes, wheels, gyro             *
 *___________|______________________________________________________________________________|________________________________*
 *     3     | Test PID and Conveyor. Goes to closest fish seen, picks it up, and stores it | eyes, wheels, conveyor         *
 *           | based on its color                                                           |                                *
 *___________|______________________________________________________________________________|________________________________*
 *     4     | Test Conveyor. Picks up a fish and stores it. Has a pattern:                 | conveyor                       *
 *           | 1,2,3,1, 1,2,3,2, 1,2,3,3 ...                                                |                                *
 *___________|______________________________________________________________________________|________________________________*
 *     5     | Test fishCollection. Goes to closest fish seen, picks it up, and stores it   | eyes, wheels, conveyor, gyro   *
 *           | based on its color. Then rotates to next fish and repeats for all the fish.  |                                *
 *___________|______________________________________________________________________________|________________________________*
 *     6     | Test Conveyor. Picks up a fish, determines color using pixy, and stores it.  | conveyor, eyes                 *
 *___________|______________________________________________________________________________|________________________________*
 *     7     | Test PID. Goes to closest fish seen and stops in front of it, rotates to next| eyes, wheels, gyro             *
 *           | fish seen until all 12 fish have been visited.                               |                                *
 *___________|______________________________________________________________________________|________________________________*
 *     8     | Test Conveyor rotator. Rotates the conveyor up for 5 sec and down for 5 sec. | conveyor                       *
 *___________|______________________________________________________________________________|________________________________*
 *     9     | Test Bins Dumping                                                            | bins                           *
 *___________|______________________________________________________________________________|________________________________*
 *     10    | Test Bin Dumping while going around the track                                | all                            *
 *___________|______________________________________________________________________________|________________________________*
 */

const int testParam = 1;

const boolean binsEnabled = true;
const boolean eyesEnabled = true;
const boolean conveyorEnabled = true;
const boolean wheelsEnabled = true;
const boolean gyroEnabled = true;

/********
 * PINS *
 ********/
/*** EYES ***/
const byte IRPort = 0; //Port for IR sensor

/*** WHEELS ***/
const byte DLMotorForward = 2;
const byte DLMotorBackward = 3;
const byte DRMotorForward = 4;
const byte DRMotorBackward = 5;

/*** BINS ***/

const byte binServoPin = 13;

/*** CONVEYOR ***/
const byte downwardConveyorBeltPin = 7;
const byte upwardConveyorBeltPin = 6;
const byte downwardConveyorRotatorPin = 8;
const byte upwardConveyorRotatorPin = 9;
const byte rotatorLimitSwitchUpPin = 31;
const byte rotatorLimitSwitchDownPin = 28;
const byte frontClawServoPin = 10;
const byte backClawServoPin = 12;
const byte IRPin = A1;

/*************
 * CONSTANTS *
 *************/
/*** GYRO ***/
const float gyrokp = 4.0f;	//proportional
const float gyroki = 0.5f;	//integral
const float gyrokd = 0.5f;	//derivative
float gyroPIDconsts[3] = { gyrokp, gyroki, gyrokd };
const float gyroRotatekp = 0.1;
const float gyroRotateKi = 0;
const float gyroRotatekd = 0;
float gyroRotatePIDconsts[3] = { gyroRotatekp, gyroRotateKi, gyroRotatekd };


/*** EYES ***/
const int stopVoltage = 400; //not used
const int closeVoltage = 625; // (lower closer) voltage to position the robot
const byte errorVoltage = 3;
const int peakVoltage = 633;
const float IRconstantThreshold = 1.0;
const unsigned long pixyStallTime = 300;
byte getFishSigCount = 100;
byte errorDeadzone = 2;
//These values are the weights used to determine a blocks score`
const float centerConst = 1.0 / 13.75f; //Divide by these random numbers to convert it to inches
const float bottomLineConst = 0.3 / 2.83;
float blockScoreConsts[2] = { centerConst, bottomLineConst };
float minimumBlockScore = -300; //lower number is more lenient
float minimumBlockSize = 300.0;
float maximumBlockY = 70; //0-199

//Constants for PID controller using pixy
const float pixykp = 0.5f;	//proportional
const float pixyki = 0.05f;	//integral
const float pixykd = 0.09f;	//derivative
float pixyPIDconsts[3] = { pixykp, pixyki, pixykd};
const float pixyRotatekp = 0.2;
const float pixyRotateKi = 0.1;
const float pixyRotatekd = 0.1;
float pixyRotatePIDconsts[3] = { pixyRotatekp, pixyRotateKi, pixyRotatekd };

/*** WHEELS ***/
//Constants for motors
const int center = 130;//140; //Where the robot aims when it detects a block. Valid values are 0 - 319.
const byte drivetrainPower = 80; //How much power for wheel motors. Valid values are 0 - 255.
const double robotStopDist = 3;
const double robotTurnRadius = 7.5;
const double robotCenter = 3.0;
const double lengthToFirstFish = 16; //how far the wheels are initially from the first fish
//Constants for turning
//struct Rotation
//{
//	float angleFromVertical;
//	float length;
//	byte rotationType; //0 = stationary, 1 = sweep
//	boolean direction; //0 = left; 1 = right
//};
Rotation smallAnglePath[19] = { { 0,		16,			0,			0 },//At fish 1, go STRAIGHT to fish 2
								{ 0,		32,			0,			0 },//At fish 2, go STRAIGHT to fish 3
								{ 0,		16,			0,			0 },//At fish 3, go STRAIGHT to fish 4
								{ 117.5,	24.492,		1,			0 },//At fish 4, turn SWEEP LEFT to fish 5
								
								{ 45,		16,			0,			0 },//At fish 5, turn stationary to bin
								{ 125,		10,			0,			0 },//At fish 5, turn to dump bin

								//{ 170,		24.492,		1,			0 },//At fish 5, turn SWEEP LEFT to fish 6
								//{ 270,		16,			0,			0 },//At fish 6, turn RIGHT to fish 7
								{ 270,		32,			0,			0 },//At fish 7, turn go STRAIGHT to fish 8
								{ 270,		16,			0,			0 },//At fish 8, turn go STRAIGHT to fish 9
								{ 50,		24.492,		1,			0 },//At fish 9, turn RIGHT to fish 10
								{ 200,		45.255,		0,			0 },//At fish 10, turn RIGHT to fish 11
								{ 155,		45.255,		0,			0 },//At fish 11, turn RIGHT to fish 12
								//End of fish collection route
								{ 155,		16,			0,			0 },//At fish 12, turn LEFT to face bin 1
								{ 105,		10,			0,			0 },//At bin 1, reposition for dumping
								{ 170,		67.882251,	0,			0 },//At bin 1, face bin 2
								{ 0,		0,			0,			0 },//At bin 2, reposition for dumping
								{ 0,		0,			0,			0 },//At bin 2, face bin 3
								{ 0,		0,			0,			0 },//At bin 3, reposition for dumping
								{ 0,		0,			0,			0 },//At bin 3, face bin 4
								{ 0,		0,			0,			0 },//At bin 4, reposition for dumping
};
const byte turnDeadzone = 1;

/*** CONVEYOR ***/
//Claw
const int frontUpwardAngle = 160;
const int frontDownwardAngle = 50;
const int backUpwardAngle = 180;
const int backDownwardAngle = 90;
const int backExtendedAngle = 30;
const unsigned long clawMovingTime = 400UL; //How long the system should wait before the claw is done opening and closing
const unsigned long clawRotatingTime = 100UL; //How long the system should wait before the claw is done rotating

//Conveyor motor powers
const byte conveyorBeltPower = 150;
const byte conveyorRotatorUpPower = 190;
const byte conveyorRotatorDownPower = 65;
const byte conveyorRotatorStopPower = 100;

//Conveyor positions
const BinPosition startingPosition = FISH; //Where the conveyor is when we start off the robot
const BinPosition restingPosition = FISH; //Where the conveyor rests when it is out of the way of seeing the fish

/*** BINS ***/
const unsigned long binDumpingTime = 2200UL; // how long the bin servo should turn to dump each bin
const byte binServoStop = 90; //Value for the bin servo to stop moving 
const byte binServoForward = 180; //Value for the bin servo to move

/***********
 * MODULES *
 ***********/
Drivetrain* wheels;
VisualSensor* eyes;
Gyro* gyro;
Conveyor* conveyor;
Bins* bins;
ASEE2015* myRobot;

void setup()
{
	Serial.begin(9600);

	//Construct objects that are enabled; otherwise set them to nullptrs
	eyes = (eyesEnabled) ? new VisualSensor(IRPort, stopVoltage, closeVoltage, errorVoltage, peakVoltage,
		center, errorDeadzone, pixyStallTime, IRconstantThreshold,
		blockScoreConsts, pixyPIDconsts, pixyRotatePIDconsts, minimumBlockScore, minimumBlockSize, maximumBlockY,
		getFishSigCount) : 0;
	gyro = (gyroEnabled) ? new Gyro(gyroPIDconsts, gyroRotatePIDconsts) : 0;
	wheels = (wheelsEnabled) ? new Drivetrain(DLMotorForward, DLMotorBackward, DRMotorForward, DRMotorBackward,
		center, drivetrainPower, gyro, smallAnglePath, turnDeadzone, robotStopDist, robotTurnRadius, robotCenter, lengthToFirstFish) : 0;
	conveyor = (conveyorEnabled) ? new Conveyor(frontUpwardAngle, frontDownwardAngle, backUpwardAngle, backDownwardAngle, backExtendedAngle,
		conveyorBeltPower, downwardConveyorBeltPin, upwardConveyorBeltPin,
		conveyorRotatorUpPower,conveyorRotatorDownPower,conveyorRotatorStopPower,
		downwardConveyorRotatorPin, upwardConveyorRotatorPin, rotatorLimitSwitchUpPin, rotatorLimitSwitchDownPin,
		frontClawServoPin, backClawServoPin, IRPin,
		clawMovingTime, clawRotatingTime, restingPosition, startingPosition) : 0;
	bins = (binsEnabled) ? new Bins(binServoPin, binServoStop, binServoForward, binDumpingTime) : 0;

	//Construct the robot from the modules
	myRobot = new ASEE2015(testParam, wheels, eyes, gyro, conveyor, bins);
}

void loop()
{
	//Execute the function specified by the test param.
	if (myRobot->go())
	{
		myRobot->allStop();
	}
}
