//
//  LGPS.cpp
//  Rover
//
//  Created by Luka Gabric on 05/08/15.
//  Copyright (c) 2015 Luka Gabric. All rights reserved.
//

#include "LGPS.h"
#include "LLCD.h"

#pragma mark - Constructor

LGPS::LGPS() {
    _gps = new TinyGPS();

    _ss = new SoftwareSerial(10, 11);
    _ss->begin(9600);
    
    _lat = TinyGPS::GPS_INVALID_F_ANGLE;
    _lon = TinyGPS::GPS_INVALID_F_ANGLE;
    _altitude = TinyGPS::GPS_INVALID_F_ALTITUDE;
}

#pragma mark - Read Data

void LGPS::readGPSData() {
    if (millis() - _time < 800) return;
    
    bool newData = false;
    if (_ss->available()) {
        newData = true;
        do {
            _gps->encode(_ss->read());
        } while (_ss->available());
    }
    
    if (newData) {
        _time = millis();
        
        _gps->f_get_position(&_lat, &_lon, &_age);
        _altitude = _gps->f_altitude();
    }
}

#pragma mark - Getters

float LGPS::latitude() {
    return _lat;
}

float LGPS::longitude() {
    return _lon;
}

unsigned long LGPS::age() {
    return _age;
}

bool LGPS::isLocationValid() {
    return (latitude() != TinyGPS::GPS_INVALID_F_ANGLE && longitude() != TinyGPS::GPS_INVALID_F_ANGLE);
}

float LGPS::altitude() {
    return _altitude;
}

bool LGPS::isAltitudeValid() {
    return altitude() != TinyGPS::GPS_INVALID_F_ALTITUDE;
}

#pragma mark - Bearing

double degToRad(double deg) {
    return (M_PI / 180) * deg;
}

double radToDeg(double rad) {
    return (180 / M_PI) * rad;
}

float LGPS::bearing(float lat1, float lon1, float lat2, float lon2) {
    double latR1 = degToRad(lat1);
//    double lonR1 = degToRad(lon1);
    
    double latR2 = degToRad(lat2);
//    double lonR2 = degToRad(lon2);
    
//    double dLatR = degToRad(lat2 - lat1);
    double dLonR = degToRad(lon2 - lon1);
    
    double y = sin(dLonR) * cos(latR2);
    double x = cos(latR1) * sin(latR2) - sin(latR1) * cos(latR2) * cos(dLonR);
    
    double theta = radToDeg(atan2(y, x));
    
    return fmod(theta + 360, 360);
}

float LGPS::bearingTo(float lat, float lon) {
    if (!isLocationValid()) return -1;
    
    return bearing(latitude(), longitude(), lat, lon);
}

#pragma mark - LDebugLog

void LGPS::printToSerial() {
    if (isLocationValid()) {
        Serial.print("\nLAT: ");Serial.print(latitude(), 15);Serial.print("    LON: ");Serial.print(longitude(), 15);Serial.print("    AGE: ");Serial.println(age());
    }
    else {
        Serial.println("\nLOCATION UNAVAILABLE");
    }
}

void LGPS::printToLCD(LLCD *lcd) {
    if (isLocationValid()) {
        lcd->print(0, 0, "LAT=");
        lcd->print(4, 0, latitude(), 15);
        lcd->print(0, 1, "LON=");
        lcd->print(4, 1, longitude(), 15);
    }
    else {
        lcd->print(0, 0, "LOCATION");
        lcd->print(0, 1, "UNAVAILABLE");
    }
}

#pragma mark -
