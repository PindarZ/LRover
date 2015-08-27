#include "Arduino.h"
#include "LRoverNavigator.h"
#include <Wire.h>
#include "LUltrasonic.h"
#include "LDebugLog.h"

#pragma mark - Setup

void LRoverNavigator::setup() {
    _state = LRoverStateInitialization;
    _timeInit = 0; _time1Hz = 0; _time20Hz = 0; _time5s = 0;
    
#if DEBUG_LOG
    Serial.begin(9600);
    Serial.println("Starting...");
#endif

    delay(100);
    
    _compass = new LCompass(NULL);
    _compass->setDeclinationDeg(4.183333);
    
    _sonics = new LRoverSonics();
    
    _gps = new LGPS();
    
#if DRIVE
    _motorController = new LMotorController(ENA, IN1, IN2, ENB, IN3, IN4, 1, 1);
#endif 
    
    _cruisePID = new LPID(Kp, Ki, Kd, DIRECT);
    _cruisePID->SetMode(AUTOMATIC);
    _cruisePID->SetOutputLimits(-255, 255);
    _cruisePID->SetSampleTime(50);
    
    _cruiseController = new LCruiseController(_cruisePID);
    _cruiseController->minimumForwardWheelSpeed = MINIMUM_FORWARD_WHEEL_SPEED;
    
    _wallFollowPID = new LPID(Kp, Ki, Kd, DIRECT);
    _wallFollowPID->SetMode(AUTOMATIC);
    _wallFollowPID->SetSampleTime(50);
    _wallFollowPID->setSetpoint(OBSTACLE_DISTANCE_THRESHOLD);
    
    _wallFollowController = new LWallFollowController(_wallFollowPID);
    
#if MANUAL_PID_TUNING
//    _cruisePIDTuner = new LPIDTuner(_cruisePID, POT_Kp, POT_Ki, POT_Kd);
    _wallFollowPIDTuner = new LPIDTuner(_wallFollowPID, POT_Kp, POT_Ki, POT_Kd);
#endif
    
#if LCD_DEBUG_LOG
    _lcd = new LLCD();
#endif

#if DEBUG_LOG || LCD_DEBUG_LOG
    std::vector<LDebugLog*> logItems;
    
    if (_sonics) {
        _sonicsLogger = new LRoverSonicsLogger(_sonics);
        logItems.push_back(_sonicsLogger);
    }
    if (_gps) {
        _gpsLogger = new LGPSLogger(_gps);
        logItems.push_back(_gpsLogger);
    }
    if (_compass) {
        _compassLogger = new LCompassLogger(_compass);
        logItems.push_back(_compassLogger);
    }
    if (_cruisePID) {
        _cruisePIDLogger = new LPIDLogger(_cruisePID);
        logItems.push_back(_cruisePIDLogger);
    }
    if (_wallFollowPID) {
        _wallFollowPIDLogger = new LPIDLogger(_wallFollowPID);
        logItems.push_back(_wallFollowPIDLogger);
    }
    if (_motorController) {
        _motorControllerLogger = new LMotorControllerLogger(_motorController);
        logItems.push_back(_motorControllerLogger);
    }
    
    _logger = new LLogger(_lcd, logItems);
#endif
}

#pragma mark - Loop

void LRoverNavigator::loop() {
    if (_state == LRoverStateGoalReached) return;
    
    _gps->readGPSData();
    
    if (_state == LRoverStateInitialization) {
        initializationLoop();
        return;
    }
    
    unsigned long currentTime = millis();
    
    if (currentTime - _time20Hz >= 50) {
        _time20Hz = currentTime;
        loopAt20Hz();
    }
    if (currentTime - _time1Hz >= 1000) {
        _time1Hz = currentTime;
        loopAt1Hz();
    }
    if (currentTime - _time5s >= 5000) {
        _time5s = currentTime;
        loopAt5s();
    }
}

void LRoverNavigator::initializationLoop() {
    if (!_gps->location().isValid()) return;
    
    if (_timeInit == 0) {
        _timeInit = millis();
        return;
    }
    
    if (millis() - _timeInit > 10000 && _gps->age() < 1000) {
        _goalLocation = _gps->location();
        _gps->setGoalLocation(_goalLocation);
        delay(10000);
        _state = LRoverStateCruise;
    }
}

void LRoverNavigator::loopAt5s() {
#if LCD_DEBUG_LOG
    _logger->skipNextDebugLogToLCD();
#endif    
}

void LRoverNavigator::loopAt1Hz() {
#if MANUAL_PID_TUNING
    _cruisePIDTuner->configurePIDConstants();
    _wallFollowPIDTuner->configurePIDConstants();
#endif
#if DEBUG_LOG
    _logger->debugLogToSerial();
#endif
#if LCD_DEBUG_LOG
    _logger->debugLogToLCD();
#endif
}

void LRoverNavigator::loopAt20Hz() {    
    //perception
    updateSensorReadings();

    //localization
    LGeoLocation currentLocation = _gps->location();
    
    if (!currentLocation.isValid()) return;
    
    double goalHeadingDeg = currentLocation.headingDegTo(_goalLocation);
    _compass->setGoalHeadingDeg(goalHeadingDeg);
    
    //path planning
    if (isCurrentEqualToGoalLocation()) {
        arrivedAtGoal();
        return;
    }
    
    LObstacleDistances obstacleDistances = _sonics->obstacleDistances();
    
    if (_state != LRoverStateWallFollow) {
        if (obstacleDistances.isObstacleDetected()) {
            _state = LRoverStateWallFollow;
        }
    }
    else if (obstacleDistances.isObstacleCleared() || shouldForceCruiseAndIgnoreObstacle(obstacleDistances)) {
        _state = LRoverStateCruise;
    }
    
    LWheelSpeeds wheelSpeeds;
    
    switch (_state) {
        case LRoverStateWallFollow:
            wheelSpeeds = _wallFollowController->wallFollowOutput(obstacleDistances);
            break;
        case LRoverStateCruise:
            wheelSpeeds = _cruiseController->cruiseOutput(currentLocation, _goalLocation, _compass->headingDeg());
            break;
    }
    
    //path execution
#if DRIVE
    _motorController->move(wheelSpeeds.leftWheelSpeed, wheelSpeeds.rightWheelSpeed);
#endif
}

#pragma mark - Perception

void LRoverNavigator::updateSensorReadings() {
    _sonics->performNextMeasurement();
    _compass->updateHeading();
}

#pragma mark - Path Planning

bool LRoverNavigator::isCurrentEqualToGoalLocation() {
    float distanceToLocation = _gps->distanceToGoalLocation();
    return distanceToLocation < GOAL_RADIUS_METERS;
}

void LRoverNavigator::arrivedAtGoal() {
    _state = LRoverStateGoalReached;
    
    _motorController->turnLeft(255, false);
    delay(4000);
    _motorController->stopMoving();
}

bool LRoverNavigator::shouldForceCruiseAndIgnoreObstacle(LObstacleDistances obstacleDistances) {
    bool leftFollow = obstacleDistances.leftMinDistance() < obstacleDistances.rightMinDistance();
    double headingOffset = _compass->offsetFromGoalHeadingDeg();
    
    if ((leftFollow == true && headingOffset > 0) || (leftFollow == false && headingOffset < 0)) {
        return true;
    }
    
    return false;
}

#pragma mark -
