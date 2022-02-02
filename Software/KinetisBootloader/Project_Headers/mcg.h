/**
 * @file      mcg.h (180.ARM_Peripherals/Project_Headers/mgc.h)
 *
 * @brief    Abstraction layer for MCG interface
 *
 * @version  V4.12.1.80
 * @date     13 April 2016
 */

#ifndef INCLUDE_USBDM_MCG_H_
#define INCLUDE_USBDM_MCG_H_
 /*
 * *****************************
 * *** DO NOT EDIT THIS FILE ***
 * *****************************
 *
 * This file is generated automatically.
 * Any manual changes will be lost.
 */
#include "derivative.h"
#include "system.h"
#include "pin_mapping.h"

namespace USBDM {

/**
 * @addtogroup MCG_Group MCG, Multipurpose Clock Generator
 * @brief Abstraction for Multipurpose Clock Generator
 * @{
 */

/** MCGFFCLK - Fixed frequency clock (input to FLL) */
extern volatile uint32_t SystemMcgffClock;
/** MCGOUTCLK - Primary output from MCG, various sources */
extern volatile uint32_t SystemMcgOutClock;
/** MCGFLLCLK - Output of FLL */
extern volatile uint32_t SystemMcgFllClock;
/** MCGPLLCLK - Output of PLL */
extern volatile uint32_t SystemMcgPllClock;

extern void setSysDividersStub(uint32_t simClkDiv1);

/**
 * Clock configurations
 */
enum ClockConfig : uint8_t {
   ClockConfig_PEE_60MHz,
   ClockConfig_BLPE_4MHz,
   ClockConfig_PEE_48MHz,

   ClockConfig_default = 0,
};

/**
 * Type definition for MCG interrupt call back
 */
typedef void (*MCGCallbackFunction)(void);

/**
 * @brief Class representing the MCG
 *
 * <b>Example</b>
 * @code
 *    Mcg::initialise();
 * @endcode
 */
class Mcg {

private:
   /** Callback function for ISR */
   static MCGCallbackFunction callback;

   /** Hardware instance */
   static constexpr HardwarePtr<MCG_Type> mcg = McgInfo::baseAddress;

public:
   /**
    * Table of clock settings
    */
   static const McgInfo::ClockInfo clockInfo[];

   /**
    * Transition from current clock mode to mode given
    *
    * @param[in]  clockInfo Clock mode to transition to
    *
    * @return E_NO_ERROR on success
    */
   static ErrorCode clockTransition(const McgInfo::ClockInfo &clockInfo);

   /**
    * Update SystemCoreClock variable
    *
    * Updates the SystemCoreClock variable with current core Clock retrieved from CPU registers.
    */
   static void SystemCoreClockUpdate(void);

   /**
    *  Change SIM->CLKDIV1 value
    *
    * @param[in]  simClkDiv1 - Value to write to SIM->CLKDIV1 register
    */
   static void setSysDividers(uint32_t simClkDiv1) {
      SIM->CLKDIV1 = simClkDiv1;
   }

   /**
    * Enable interrupts in NVIC
    */
   static void enableNvicInterrupts() {
      NVIC_EnableIRQ(McgInfo::irqNums[0]);
   }

   /**
    * Enable and set priority of interrupts in NVIC
    * Any pending NVIC interrupts are first cleared.
    *
    * @param[in]  nvicPriority  Interrupt priority
    */
   static void enableNvicInterrupts(NvicPriority nvicPriority) {
      enableNvicInterrupt(McgInfo::irqNums[0], nvicPriority);
   }

   /**
    * Disable interrupts in NVIC
    */
   static void disableNvicInterrupts() {
      NVIC_DisableIRQ(McgInfo::irqNums[0]);
   }
   /**
    * MCG interrupt handler -  Calls MCG callback
    */
   static void irqHandler() {
      if (callback != 0) {
         callback();
      }
   }

   /**
    * Set callback for ISR
    *
    * @param[in]  callback The function to call from stub ISR
    */
   static void setCallback(MCGCallbackFunction callback) {
      Mcg::callback = callback;
   }

   /** Current clock mode (FEI out of reset) */
   static McgInfo::ClockMode currentClockMode;

   /**
    * Get current clock mode
    *
    * @return
    */
   static McgInfo::ClockMode getClockMode() {
      return currentClockMode;
   }

   /**
    * Get name for clock mode
    *
    * @return Pointer to static string
    */
   static const char *getClockModeName(McgInfo::ClockMode);

   /**
    * Get name for current clock mode
    *
    * @return Pointer to static string
    */
   static const char *getClockModeName() {
      return getClockModeName(getClockMode());
   }

   /**
    *  Configure the MCG for given mode
    *
    *  @param[in]  settingNumber CLock setting number
    */
   static void configure(ClockConfig settingNumber=ClockConfig_default) {
      clockTransition(clockInfo[settingNumber]);
   }

   /**
    *   Finalise the MCG
    */
   static void finalise() {
      clockTransition(clockInfo[ClockConfig_default]);
   }

   /**
    * Initialise MCG to default settings.
    */
   static void defaultConfigure();

   /**
    * Set up the OSC out of reset.
    */
   static void initialise() {
      defaultConfigure();
   }

};

/**
 * @}
 */

} // End namespace USBDM

#endif /* INCLUDE_USBDM_MCG_H_ */
