/**
 * @file    PidController.h
 * @brief   PID Controller class
 *
 *  Created on: 10 Jul 2021
 *      Author: podonoghue
 */
#include "PidController.h"
#include "hardware.h"
#include "Channel.h"

using namespace USBDM;

/**
 * Set control parameters
 *
 * @param settings Parameter to use
 */
void PidController::setControlParameters(const TipSettings *settings) {
   fKp       = settings->getKp();
   fKi       = settings->getKi() * fInterval;
   fKd       = settings->getKd() / fInterval;
   fILimit   = settings->getILimit();
}

/**
 * Enable controller
 *
 * @note: Controller may be re-initialised when enabled.
 * @note: Output is left unchanged when disabled.
 *
 * @param[in] enable True to enable
 */
void PidController::enable(bool enable) {
   if (enable) {
      if (!fEnabled) {
         // Just enabled
         fIntegral    = fCurrentOutput;
         fTickCount   = 0;
      }
   }
   else if (fEnabled) {
      // Just disabled
      fCurrentOutput = 0;
      setDutyCycle(0);
   }
   fEnabled = enable;
}

/**
 * Main calculation
 *
 * Should be executed at interval period
 *
 * Process new sample to produce new control output
 *
 * @note If the controller is disabled it will simply return the last output value
 *
 * @param actualTemperature   Tip temperature in Celsius
 * @param targetTemperature   Target tip temperature in Celsius
 *
 * @return Control output
 */
float PidController::newSample(float targetTemperature, float actualTemperature) {

   const float lastInput = fCurrentInput;

   // Save for next iteration
   fCurrentInput = actualTemperature;

   if(!fEnabled) {
      // Assume manually set value
      return fCurrentOutput;
   }

   fTickCount++;

   fCurrentTarget = targetTemperature;

   // Update input samples & error
   fCurrentError = fCurrentTarget - fCurrentInput;

   if ((fCurrentOutput<(fOutMin+1))){
      // Hit bottom drive limit - de-integrate slower
      fIntegral += (fKi/2 * fCurrentError);
   }
   else if ((fCurrentError<-4) && (fIntegral>0)) {
      // Overshoot
      fIntegral += (5 * fKi * fCurrentError);
   }
   else {
      fIntegral += (fKi * fCurrentError);
   }

//   if (fabs(fCurrentError)>40) {
      if (fIntegral>fILimit) {
         fIntegral = fILimit;
      }
      else if (fIntegral<-fILimit) {
         fIntegral = -fILimit;
      }
//   }
//   if ((fCurrentOutput>=fOutMax) && (fCurrentError>0) && (fIntegral > fILimit)) {
//      // Limit positive integral term when at 100% power and rising
//      fIntegral = fILimit;
//   }
//   if (fIntegral < -fILimit) {
//      // Limit negative integral term in general
//      fIntegral = -fILimit;
//   }
   fDifferential = ((fKd * (fCurrentInput - lastInput)) + fDifferential)/2;

   fProportional = fKp * fCurrentError;

   fCurrentOutput = fProportional + fIntegral - fDifferential;

   if(fCurrentOutput > fOutMax) {
      fCurrentOutput = fOutMax;
   }
   else if(fCurrentOutput < fOutMin) {
      fCurrentOutput = fOutMin;
   }

   // Update output
   return fCurrentOutput;
}

/**
 * Print heading for report()
 */
void PidController::reportHeading(Channel & ch) const {

      console.setFloatFormat(1, Padding_None).write("SetTemp, Drive,", ch.getTipName(), ",Error,P=", getKp());
      console.setFloatFormat(3, Padding_None).write(",I=", getKi());
      console.setFloatFormat(1, Padding_None).write("<", fILimit, "@", (int)round(fOutMax), "%,D,Instant. T");
      console.writeln();
}

/**
 * Report current situation
 */
void PidController::report() const {

   console.setFloatFormat(1, Padding_LeadingSpaces, 3);
   console.write(",", fCurrentTarget); // Set temperature
   console.write(",", fCurrentOutput); // Drive %
   console.write(",", fCurrentInput);  // Average temperature
   console.write(",", fCurrentError);  // Error
   console.write(",", fProportional);  // P
   console.write(",", fIntegral);      // I
   console.write(",", fDifferential);  // D
}
