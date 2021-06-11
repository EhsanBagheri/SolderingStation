/*
 * Control.cpp
 *
 *  Created on: 5 Apr 2020
 *      Author: podonoghue
 */

#include "smc.h"
#include "SwitchPolling.h"
#include "Control.h"
#include "Channels.h"

using namespace USBDM;

/**
 * Constructor
 * Does minimal work - see initialise for main initialisation.
 */
Control::Control() {
   This = this;
}

/**
 * Initialise the control
 */
void Control::initialise() {
   using namespace USBDM;

   GpioSpare1::setOutput();
   GpioSpare2::setOutput();
   GpioSpare3::setOutput();

   ch1DutyCycleCounter.setUpperLimit(80);
   ch2DutyCycleCounter.setUpperLimit(80);

   Ch1Drive::setOutput(PinDriveStrength_High, PinDriveMode_PushPull, PinSlewRate_Slow);
   Ch2Drive::setOutput(PinDriveStrength_High, PinDriveMode_PushPull, PinSlewRate_Slow);

   ch1DutyCycleCounter.setDutyCycle(0);
   ch2DutyCycleCounter.setDutyCycle(0);

   ch1DutyCycleCounter.enable();
   ch2DutyCycleCounter.enable();

   Ch1SelectedLed::setOutput(PinDriveStrength_High, PinDriveMode_PushPull, PinSlewRate_Slow);
   Ch2SelectedLed::setOutput(PinDriveStrength_High, PinDriveMode_PushPull, PinSlewRate_Slow);

   Ch1ActiveLed::setOutput(PinDriveStrength_High, PinDriveMode_PushPull, PinSlewRate_Slow);
   Ch2ActiveLed::setOutput(PinDriveStrength_High, PinDriveMode_PushPull, PinSlewRate_Slow);

   static auto adc_cb = [](uint32_t result, int channel){
      This->adcHandler(result, channel);
   };
   Adc0::configure(
         ADC_RESOLUTION,
         AdcClockSource_Bus,
         AdcSample_20,
         AdcPower_Normal,
         AdcMuxsel_B,
         AdcClockRange_Normal,
         AdcAsyncClock_Disabled);
   unsigned retry = 10;
   while ((Adc0::calibrate() != E_NO_ERROR) && (retry-->0)) {
      console.write("ADC calibration failed, retry #").writeln(retry);
   }
   Adc0::setAveraging(AdcAveraging_32);
   Adc0::setCallback(adc_cb);
   Adc0::enableNvicInterrupts(NvicPriority_Normal);

   static auto zx_cb = [](CmpStatus){
      This->zeroCrossingHandler();
   };

   static constexpr uint8_t DAC_THRESHOLD = (3.3/2)*(Cmp0::MAXIMUM_DAC_VALUE/ADC_REF_VOLTAGE);

   Pit::configureIfNeeded();

   ControlTimerChannel::enableNvicInterrupts(NvicPriority_Normal);

   ZeroCrossingComparator::configure(CmpPower_HighSpeed, CmpHysteresis_1, CmpPolarity_Noninverted);
   ZeroCrossingComparator::setInputFiltered(CmpFilterSamples_7, CmpFilterClockSource_BusClock, 255);
   ZeroCrossingComparator::setInputs();
   ZeroCrossingComparator::configureDac(DAC_THRESHOLD, CmpDacSource_Vdda);
   ZeroCrossingComparator::selectInputs(Cmp0Input_Ptc7, Cmp0Input_Cmp0Dac);
   ZeroCrossingComparator::setCallback(zx_cb);
   ZeroCrossingComparator::enableInterrupts(CmpInterrupt_Both);
   ZeroCrossingComparator::enableNvicInterrupts(NvicPriority_Normal);

   static auto overcurrent_cb = [](uint32_t){
      This->overCurrentHandler();
   };

   Overcurrent::setInput(PinPull_None, PinAction_IrqFalling, PinFilter_Passive);
   Overcurrent::setCallback(overcurrent_cb);
   Overcurrent::enableNvicInterrupts(NvicPriority_High);
}

/**
 * Check if channel is enabled
 *
 * @param ch Channel to check
 */
bool Control::isEnabled(unsigned ch) {
   Channel &channel = channels[ch];
   return (channel.getState() != ch_off);
}

/**
 * Toggle the enable state of a channel.
 * If becoming enabled it also becomes selected.
 * If becoming disabled the other channel may become selected if enabled.
 *
 * @param ch Channel to modify
 */
void Control::toggleEnable(unsigned ch) {
   Channel &channel = channels[ch];
   if (channel.getState() == ch_off) {
      enable(ch);
   }
   else {
      disable(ch);
   }
}

/**
 * Enable channel.
 * It also becomes selected.
 *
 * @param ch Channel to enable
 */
void Control::enable(unsigned ch) {

   Channel &channel = channels[ch];

   channel.setState(ch_active);
   channel.restartIdleTimer();

   ch1Pid.enable(channels[1].isRunning());
   ch2Pid.enable(channels[2].isRunning());
}

/**
 * Disable channel.
 *
 * @param ch Channel to disable
 */
void Control::disable(unsigned ch) {

   Channel &channel = channels[ch];

   channel.setState(ch_off);

   ch1Pid.enable(channels[1].isRunning());
   ch2Pid.enable(channels[2].isRunning());
}

/**
 * Backoff channel (if enabled).
 *
 * @param ch Channel to modify
 */
void Control::backOff(unsigned ch) {

   Channel &channel = channels[ch];
   if (channel.isRunning()) {
      channel.setState(ch_backoff);
   }
}

/**
 * Wake-up channel (if in back-off).
 * It also becomes selected.
 *
 * @param ch Channel to modify
 */
void Control::wakeUp(unsigned ch) {

   Channel &channel = channels[ch];
   if (channel.getState() == ch_backoff) {
      enable(ch);
   }
}

/**
 * Set the selected channel
 *
 * @param ch Channel to select
 */
void Control::setSelectedChannel(unsigned ch) {

   channels.setSelectedChannel(ch);
   channels.getSelectedChannel().restartIdleTimer();
}

/**
 * Change the temperature to the next preset value for the currently selected channel
 */
void Control::nextPreset() {

   unsigned selectedChannel = channels.getSelectedChannelNumber();
   if (selectedChannel == 0) {
      return;
   }
   Channel &channel = channels.getSelectedChannel();

   channel.incrementPreset();
   channel.setUserTemperature(channel.getPresetTemperature());
}

/**
 * Change temperature of currently selected channel
 *
 * @param delta Amount to change by
 */
void Control::changeTemp(int16_t delta) {

   if (channels.getSelectedChannelNumber() == 0) {
      return;
   }

   Channel &channel = channels.getSelectedChannel();

   int targetTemperature = channel.getUserTemperature();

   targetTemperature += delta;
   if (targetTemperature>MAX_TEMP) {
      targetTemperature = MAX_TEMP;
   }
   if (targetTemperature<MIN_TEMP) {
      targetTemperature = MIN_TEMP;
   }
   channel.setUserTemperature(targetTemperature);
   channel.restartIdleTimer();

   //   {
   //      unsigned currentDutyCycle = channel.dutyCycle;
   //
   //      // Dummy code
   //      currentDutyCycle += delta;
   //      if ((int)currentDutyCycle < 0) {
   //         currentDutyCycle = 0;
   //      }
   //      if (currentDutyCycle>100) {
   //         currentDutyCycle = 100;
   //      }
   //      channel.dutyCycle = currentDutyCycle;
   //      if (selectedChannel == 1) {
   //         ch1DutyCycleCounter.setDutyCycle(currentDutyCycle);
   //      }
   //   }

}

/**
 * Update the current preset from the current temperature of the currently selected channel
 */
void Control::updatePreset() {
   if (channels.getSelectedChannelNumber() == 0) {
      return;
   }
   Channel &channel = channels.getSelectedChannel();

   channel.updatePresetTemperature();
}

/**
 * Interrupt handler for over current comparator
 */
void Control::overCurrentHandler() {

   // Disable drive
   ch1DutyCycleCounter.disable();
   ch2DutyCycleCounter.disable();

   // Mark channels as overloaded
   channels[0].setOverload(true);
   channels[1].setOverload(true);

   setNeedsRefresh();
}

/**
 * Interrupt handler for mains zero crossing Comparator
 * This uses the timer to schedule the switchHandler().
 * Occurs @100Hz or 120Hz ~ 10ms or 8.3ms
 */
void Control::zeroCrossingHandler() {

   // Schedule switchOnHandler()
   static auto cb = [](){
      This->switchOnHandler();
   };
   if (!sequenceBusy) {
      // For debug!
      ControlTimerChannel::oneShotInMicroseconds(cb, POWER_ON_DELAY);
   }

   GpioSpare2::set();

   // Counter to initiate screen refresh
   static unsigned count = 0;

   if (count++>=50) {
      count = 0;
      setNeedsRefresh();
   }

   Channel &ch1 = channels[1];
   Channel &ch2 = channels[2];

   ch1.upDateCurrentTemperature();
   ch2.upDateCurrentTemperature();

   float ch1DutyCy = ch1Pid.newSample(ch1.getTargetTemperature(), ch1.getCurrentTemperature());
   float ch2DutyCy = ch2Pid.newSample(ch2.getTargetTemperature(), ch2.getCurrentTemperature());

   ch1DutyCycleCounter.setDutyCycle(ch1DutyCy);
   ch2DutyCycleCounter.setDutyCycle(ch2DutyCy);

   ch1.dutyCycle = ch1DutyCycleCounter.getDutyCycle();
   ch2.dutyCycle = ch2DutyCycleCounter.getDutyCycle();

   GpioSpare2::clear();
}

/**
 * Timer interrupt handler for turning on heaters.
 * It also uses the timer to schedule the sampleHandler().
 */
void Control::switchOnHandler() {

   // Schedule sampleHandler()
   static auto cb = [](){
      This->sampleHandler();
   };
   ControlTimerChannel::oneShotInMicroseconds(cb, SAMPLE_DELAY);

   sequenceBusy = true;

   // Enable drive to heaters as needed
   if (ch1DutyCycleCounter.isOn()) {
      Ch1Drive::write(true);
   }
   if (ch2DutyCycleCounter.isOn()) {
      Ch2Drive::write(true);
   }

   Ch1ActiveLed::write(ch1DutyCycleCounter.isOn());
   Ch2ActiveLed::write(ch2DutyCycleCounter.isOn());

}

/**
 * Timer interrupt handler for starting ADC sample sequence.
 * It also uses the timer to schedule the switchOffHandler().
 */
void Control::sampleHandler() {

   // Schedule switchOffHandler()
   static auto cb = [](){
      This->switchOffHandler();
   };
   ControlTimerChannel::oneShotInMicroseconds(cb, POWER_OFF_DELAY);

   // Measure channel 1 if idle this cycle
   if (!ch1DutyCycleCounter.isOn()) {
      adcChannelMask |= (1<<Ch1ColdJunctionNtc::CHANNEL) | (1<<Ch1TipThermocouple::CHANNEL);
   }
   // Measure channel 2 if idle this cycle
   if (!ch2DutyCycleCounter.isOn()) {
      adcChannelMask |= (1<<Ch2ColdJunctionNtc::CHANNEL) | (1<<Ch2TipThermocouple::CHANNEL);
   }

   // Always do chip temperature
   // This also starts the entire sequence of chained conversions
   ChipTemperature::startConversion(AdcInterrupt_Enabled);

   ch1DutyCycleCounter.advance();
   ch2DutyCycleCounter.advance();
}

/**
 * Timer interrupt handler for turning off the heaters.
 */
void Control::switchOffHandler() {

   sequenceBusy = false;

   // Disable drive to heaters as needed
   if (!ch1DutyCycleCounter.isOn()) {
      Ch1Drive::write(false);
   }
   if (!ch2DutyCycleCounter.isOn()) {
      Ch2Drive::write(false);
   }
}

/**
 * Interrupt handler for ADC conversions
 *
 * @param[in] result  Conversion result from ADC channel
 * @param[in] channel ADC channel providing the result
 *
 *   Initial conversion is started from a timer call-back when a channel has an idle cycle.
 *   Several consecutive conversions are then chained in sequence.
 */
void Control::adcHandler(uint32_t result, int adcChannel) {

   GpioSpare2::toggle();

   switch (adcChannel) {
      case Ch1ColdJunctionNtc::CHANNEL :
         channels[1].coldJunctionTemperature.accumulate(result);
         break;
      case Ch1TipThermocouple::CHANNEL :
         channels[1].tipTemperature.accumulate(result);
         break;
      case Ch2ColdJunctionNtc::CHANNEL :
         channels[2].coldJunctionTemperature.accumulate(result);
         break;
      case Ch2TipThermocouple::CHANNEL :
         channels[2].tipTemperature.accumulate(result);
         break;
      case ChipTemperature::CHANNEL :
         chipTemperature.accumulate(result);
         break;
      default:
         // Should be impossible
         __BKPT(0);
   }

   // Set up next conversion
   int nextAdcChannel = __builtin_ffs(adcChannelMask)-1;
   if (nextAdcChannel >= 0) {
      TemperatureAdc::startConversion(AdcInterrupt_Enabled|nextAdcChannel);
   }

   // Mark done current conversion
   // Note - this means each conversion is done twice except for chip temperature
   adcChannelMask &= ~(1<<adcChannel);

   GpioSpare2::toggle();
}

/**
 * Refresh the display of channel information
 */
void Control::refresh() {
   needRefresh = false;

   // Update LEDs
   Ch1SelectedLed::write(channels[1].isRunning());
   Ch2SelectedLed::write(channels[2].isRunning());

   // Update display
   display.displayTools();
}

/**
 * Debugging code
 */
void Control::reportChannel(Channel &ch) {
   static int count=0;

   if (count++ == 2000) {
      count = 0;
      console.setFloatFormat(2, Padding_LeadingSpaces, 2);
      float tipV   = 1000*ch.tipTemperature.getVoltage();
      float tipT   = ch.tipTemperature.getTemperature();
      float coldT  = ch.coldJunctionTemperature.getTemperature();

      console.
      write("Tip = ").write(tipT+coldT).
      write(" (").write(tipT).write("+").write(+coldT).
      write("),(").write(tipV).write(" mV").
      write(",").write(ch.coldJunctionTemperature.getResistance()).write(" ohms)").
      write(" ").write(tipV).write(" ").write(coldT).write(" ").
      //         write(" C, Ch1 Cold = ").write(ch1ColdJunctionTemperature.getConvertedValue()).
      //         write(" C, Ch2 Tip  = ").write(ch2TipTemperature.getConvertedValue()+ch2ColdJunctionTemperature.getConvertedValue()).
      //         write(" C, Ch2 Cold = ").write(ch2ColdJunctionTemperature.getConvertedValue()).
      //         write(" C, Chip = ").write(chipTemperature.getConvertedValue()).
      writeln();
      console.resetFormat();
   }
}

/**
 * Event loop for front panel events
 */
void Control::eventLoop()  {
   refresh();

   for(;;) {
//      reportChannel(channels[1]);

      Event event = switchPolling.getEvent();

      if (event.type != ev_None) {
         // Assume visible change
         setNeedsRefresh();

//         console.write("Position = ").write(event.change).write(", Event = ").writeln(getEventName(event));
         console.write("Event = ").writeln(getEventName(event));
         switch(event.type) {
            case ev_Ch1Hold      : toggleEnable(1);             break;
            case ev_Ch2Hold      : toggleEnable(2);             break;
            case ev_Ch1Ch2Hold   :
               if (!isEnabled(1) && !isEnabled(2)) {
                  // Both currently off - turn on both channels
                  enable(2);
                  enable(1); // Active channel
               }
               else {
                  // >= 1 channel on - turn off both
                  disable(1);
                  disable(2);
               }
               break;
            case ev_Tool1Active   : wakeUp(1);                 break;
            case ev_Tool2Active   : wakeUp(2);                 break;
            case ev_Tool1Idle     : backOff(1);                break;
            case ev_Tool2Idle     : backOff(2);                break;
            case ev_Tool1LongIdle : disable(1);                break;
            case ev_Tool2LongIdle : disable(2);                break;
            case ev_Ch1Release      :
               if (channels.getSelectedChannelNumber() != 1) {
                  setSelectedChannel(1);
               }
               else {
                  nextPreset();
               }
               break;
            case ev_Ch2Release      :
               if (channels.getSelectedChannelNumber() != 2) {
                  setSelectedChannel(2);
               }
               else {
                  nextPreset();
               }
               break;
            case ev_SelRelease    : nextPreset();              break;
            case ev_QuadRelease   : updatePreset();            break;
            case ev_QuadRotate    : changeTemp(event.change);  break;
            default: break;
         }
      }
      if (needRefresh) {
         refresh();
      }
      Smc::enterWaitMode();
   }
}

/// this pointer for static members (call-backs)
Control *Control::This = nullptr;

class SettingsData {

public:
   enum Type {Temperature, Time};

   const char      * const name;
   const Type              type;
   union {
      Nonvolatile<int>       *settingInt;
      Nonvolatile<float>     *settingFloat;
   };

   constexpr SettingsData(const char *name, Nonvolatile<int> &setting, Type type)
      : name(name), type(type), settingInt(&setting) {
   }
   constexpr SettingsData(const char *name, Nonvolatile<float> &setting, Type type)
      : name(name), type(type), settingFloat(&setting) {
   }

   void increment(int delta, int &scratch) const {
      constexpr int MAX_TIME = (99*60+50); // in seconds

      switch (type) {
         case Temperature:
            scratch += delta;
            if (scratch>Control::MAX_TEMP) {
               scratch = Control::MAX_TEMP;
            }
            if (scratch<Control::MIN_TEMP) {
               scratch = Control::MIN_TEMP;
            }
            break;
         case Time:
            // Increment by multiple of 10s with forced rounding
            scratch += delta*10;
            scratch  = scratch - scratch%10;

            if (scratch>MAX_TIME) {
               scratch = MAX_TIME;
            }
            if (scratch<0) {
               scratch = 0;
            }
            break;
      }

   }
};

static const SettingsData settingsData[] = {
      SettingsData("CH 1\nIdle temp.",    nvinit.ch1Settings.backOffTemperature, SettingsData::Temperature),
      SettingsData("CH 2\nIdle temp.",    nvinit.ch2Settings.backOffTemperature, SettingsData::Temperature),
      SettingsData("CH 1\nIdle time",     nvinit.ch1Settings.backOffTime,        SettingsData::Time       ),
      SettingsData("CH 2\nIdle time",     nvinit.ch2Settings.backOffTime,        SettingsData::Time       ),
      SettingsData("CH 1\nSafety time",   nvinit.ch1Settings.safetyOffTime,      SettingsData::Time       ),
      SettingsData("CH 2\nSafety time",   nvinit.ch2Settings.safetyOffTime,      SettingsData::Time       ),
//      SettingsData("Dummy",               SettingsData::dummyFloat,       SettingsData::Time       ),
};

EventType editItem(const SettingsData &data) {

   bool menuContinue = true;
   bool doRefresh    = true;

   Event event;;

   int scaleFactor = 1;

   int scratch = *data.settingInt;
   if (data.type == SettingsData::Time) {
      // Convert time to seconds
      scaleFactor = 1000;
      scratch /= scaleFactor;
   }

   do {
      event = switchPolling.getEvent();
      switch (event.type) {
         case ev_QuadPress:
            *data.settingInt = scratch * scaleFactor;
            break;
         case ev_QuadRotate:
            data.increment(event.change, scratch);
            doRefresh = true;
            break;
         case ev_SelPress:
         case ev_Ch1Press:
         case ev_Ch2Press:
            menuContinue = false;
            break;
         default:
            break;
      }
      if (doRefresh) {
         doRefresh = false;
         switch (data.type) {
            case SettingsData::Temperature:
               display.displayTemperatureMenuItem(data.name, scratch);
               break;
            case SettingsData::Time:
               display.displayTimeMenuItem(data.name, scratch);
               break;
         }
      }
   } while (menuContinue);

   return event.type;
}

//void displayMenuList(unsigned channel, const char *name, unsigned temperature) {
//   oled.clearDisplay();
//
//   oled.setFont(fontLarge);
//   oled.moveXY(0, 0).writeln(name).write("CH ").write(channel);
//
//   oled.setFont(fontVeryLarge);
//   oled.setPadding(Padding_LeadingSpaces).setWidth(4);
//   oled.moveXY(15, 30).write(temperature);
//   oled.setFont(fontMedium).write("C");
//
//   oled.refreshImage();
//
//   oled.resetFormat();
//}

void Control::testMenu() {

   console.write("Size    = ").writeln(sizeof(settingsData));
   console.write("Address = ").writeln(&settingsData);

   unsigned index = 0;
   for(;;) {
      EventType event = editItem(settingsData[index]);
      switch (event) {

         case ev_Ch1Press:
            if (index>0) {
               index--;
            }
            break;
         case ev_SelPress:
         case ev_Ch2Press:
            if (index < (sizeof(settingsData)/sizeof(settingsData[0]))-1) {
               index++;
            }
            break;
         default:
            break;
      }

   }
}


