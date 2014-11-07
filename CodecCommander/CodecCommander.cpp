/*
 *  Released under "The GNU General Public License (GPL-2.0)"
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your 
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but 
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along 
 *  with this program; if not, write to the Free Software Foundation, Inc., 
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "CodecCommander.h"

//REVIEW: avoids problem with Xcode 5.1.0 where -dead_strip eliminates these required symbols
#include <libkern/OSKextLib.h>
void* _org_rehabman_dontstrip_[] =
{
	(void*)&OSKextGetCurrentIdentifier,
	(void*)&OSKextGetCurrentLoadTag,
	(void*)&OSKextGetCurrentVersionString,
};

// Define variables for EAPD state updating
UInt8 eapdCapableNodes[5];

bool eapdPoweredDown, coldBoot;
unsigned char hdaCurrentPowerState, hdaPrevPowerState;

// Define usable power states
static IOPMPowerState powerStateArray[ kPowerStateCount ] =
{
    { 1,0,0,0,0,0,0,0,0,0,0,0 },
    { 1,kIOPMDeviceUsable, kIOPMDoze, kIOPMDoze, 0,0,0,0,0,0,0,0 },
    { 1,kIOPMDeviceUsable, IOPMPowerOn, IOPMPowerOn, 0,0,0,0,0,0,0,0 }
};

OSDefineMetaClassAndStructors(CodecCommander, IOService)

/******************************************************************************
 * CodecCommander::init - parse kernel extension Info.plist
 ******************************************************************************/
bool CodecCommander::init(OSDictionary *dictionary)
{
    DEBUG_LOG("CodecCommander: Initializing\n");
    
    if (!super::init(dictionary))
        return false;
	
	mConfiguration = new Configuration(dictionary);
	
    fWorkLoop = 0;
    fTimer = 0;
    
    eapdPoweredDown = true;
    coldBoot = true; // assume booting from cold since hibernate is broken on most hacks
    hdaCurrentPowerState = 0x0; // assume hda codec has no power at cold boot
    hdaPrevPowerState = hdaCurrentPowerState; //and previous state was the same
	
    return true;
}

/******************************************************************************
 * CodecCommander::start - start kernel extension and init PM
 ******************************************************************************/
bool CodecCommander::start(IOService *provider)
{
    IOLog("CodecCommander: Version 2.2.0 starting.\n");

    if (!provider || !super::start(provider))
	{
		DEBUG_LOG("CodecCommander: Error loading kernel extension.\n");
		return false;
	}
    
    // Retrieve HDEF device from IORegistry
	IORegistryEntry *hdaDeviceEntry = IORegistryEntry::fromPath(mConfiguration->getHDADevicePath());
	
    if (hdaDeviceEntry != NULL)
    {
		mIntelHDA = new IntelHDA(hdaDeviceEntry, mConfiguration->getCodecNumber());
		OSSafeRelease(hdaDeviceEntry);
    }
    else
    {
        DEBUG_LOG("CodecCommander: Device \"%s\" is unreachable.\n", mConfiguration->getHDADevicePath());
        return false;
    }
	
    IOSleep(300); // need to wait a bit until codec can actually respond to immediate verbs
    int k = 0; // array index
	
    // Fetch Pin Capabilities from the range of nodes
    DEBUG_LOG("CodecCommander: Getting EAPD supported node list (limited to 5)\n");
	
	for (int nodeId = mIntelHDA->getStartingNode(); nodeId <= mIntelHDA->getTotalNodes(); nodeId++)
	{
		unsigned int response = mIntelHDA->SendCommand(nodeId, HDA_VERB_GET_PARAM, HDA_PARM_PINCAP);
		
		if (response == -1)
		{
			DEBUG_LOG("Failed to retrieve pin capabilities for node 0x%02x.\n", nodeId);
			continue;
		}

		// if bit 16 is set in pincap - node supports EAPD
		if (((response & 0xFF0000) >> 16) == 1)
		{
			eapdCapableNodes[k] = nodeId;
			k++;
			IOLog("CodecCommander: NID=0x%02x supports EAPD, will update state after sleep\n", nodeId);
		}
	}
	
    // notify about extra feature requests
    if (mConfiguration->getCheckInfinite())
        DEBUG_LOG("CodecCommander: Infinite workloop requested, will start now!\n");
    
    // init power state management & set state as PowerOn
    PMinit();
    registerPowerDriver(this, powerStateArray, kPowerStateCount);
	provider->joinPMtree(this);
    
    // setup workloop and timer
    fWorkLoop = IOWorkLoop::workLoop();
    fTimer = IOTimerEventSource::timerEventSource(this,
                                                  OSMemberFunctionCast(IOTimerEventSource::Action, this,
                                                  &CodecCommander::onTimerAction));
    if (!fWorkLoop || !fTimer)
        stop(provider);;
    
    if (fWorkLoop->addEventSource(fTimer) != kIOReturnSuccess)
        stop(provider);
    
	this->registerService(0);
    return true;
}

/******************************************************************************
 * CodecCommander::stop & free - stop and free kernel extension
 ******************************************************************************/
void CodecCommander::stop(IOService *provider)
{
    DEBUG_LOG("CodecCommander: Stopping...\n");
    
    // if workloop is active - release it
    fTimer->cancelTimeout();
    fWorkLoop->removeEventSource(fTimer);
    OSSafeReleaseNULL(fTimer);// disable outstanding calls
    OSSafeReleaseNULL(fWorkLoop);

	// Free IntelHDA engine
	mIntelHDA->~IntelHDA();
	
    PMstop();
    super::stop(provider);
}

#ifdef DEBUG
void CodecCommander::free(void)
{
	super::free();
}
#endif

/******************************************************************************
 * CodecCommander::parseCodecPowerState - get codec power state from IOReg
 ******************************************************************************/
void CodecCommander::parseCodecPowerState()
{
	// monitor power state of hda audio codec
	IORegistryEntry *hdaDriverEntry = IORegistryEntry::fromPath(mConfiguration->getHDADriverPath());
	
	if (hdaDriverEntry != NULL)
	{
		OSNumber *powerState = OSDynamicCast(OSNumber, hdaDriverEntry->getProperty("IOAudioPowerState"));

		if (powerState != NULL)
		{
			hdaCurrentPowerState = powerState->unsigned8BitValue();

			// if hda codec changed power state
			if (hdaCurrentPowerState != hdaPrevPowerState)
			{
				// store current power state as previous state for next workloop cycle
				hdaPrevPowerState = hdaCurrentPowerState;
				// notify about codec power loss state
				if (hdaCurrentPowerState == 0x0)
				{
					DEBUG_LOG("CodecCommander: HDA codec lost power\n");
					setOutputs(0x0); // power down EAPDs properly
					eapdPoweredDown = true;
					coldBoot = false; //codec entered fugue state or sleep - no longer a cold boot
				}
			}
		}
		else
			DEBUG_LOG("CodecCommander: IOAudioPowerState unknown\n");
		
		hdaDriverEntry->release();
	}
	else
		DEBUG_LOG("CodecCommander: %s is unreachable\n", mConfiguration->getHDADriverPath());
}

/******************************************************************************
 * CodecCommander::onTimerAction - repeats the action each time timer fires
 ******************************************************************************/
void CodecCommander::onTimerAction()
{
    // check if hda codec is powered - we are monitoring ocurrences of fugue state
    parseCodecPowerState();
	
    // if no power after semi-sleep (fugue) state and power was restored - set EAPD bit
    if (eapdPoweredDown && (hdaCurrentPowerState == 0x1 || hdaCurrentPowerState == 0x2))
    {
        DEBUG_LOG("CodecCommander: cc: --> hda codec power restored\n");
        setOutputs(0x2);
    }
    
    fTimer->setTimeoutMS(mConfiguration->getInterval());
}

/******************************************************************************
 * CodecCommander::setOutputs - set EAPD status bit on SP/HP
 ******************************************************************************/
void CodecCommander::setOutputs(unsigned char logicLevel)
{
    // delay by at least 100ms, otherwise first immediate command won't be received
    // some codecs will produce loud pop when EAPD is enabled too soon, need custom delay until codec inits
    if (mConfiguration->getSendDelay() < 100)
        IOSleep(100);
    else
        IOSleep(mConfiguration->getSendDelay());
	
    // for nodes supporting EAPD bit 1 in logicLevel defines EAPD logic state: 1 - enable, 0 - disable
    for (int i = 0; i <= sizeof(eapdCapableNodes) / sizeof(eapdCapableNodes[0]); i++)
	{
        if (eapdCapableNodes[i] != 0)
			mIntelHDA->SendCommand(eapdCapableNodes[i], HDA_VERB_EAPDBTL_SET, logicLevel);
    }
	
	unsigned int* customVerbs = mConfiguration->getCustomVerbs();
	
	for (int i = 0; i < 32; i++)
	{
		if (customVerbs[i] == 0)
			break;
		
		mIntelHDA->SendCommand(customVerbs[i]);
	}
	
	eapdPoweredDown = false;
}

/******************************************************************************
 * CodecCommander::performCodecReset - reset function group and set power to D3
 *****************************************************************************/

void CodecCommander::performCodecReset ()
{
    /*
     Reset is created by sending two Function Group resets, potentially separated 
     by an undefined number of idle frames, but no other valid commands.
     This Function Group “Double” reset shall do a full initialization and reset 
     most settings to their power on defaults.
     
     This function can be used to reset codec on dekstop boards, for example H87-HD3,
     to overcome audio loss and jack sense problem after sleep with AppleHDA v2.6.0+
     */

    if (!coldBoot)
	{
        DEBUG_LOG("CodecCommander: cc: --> resetting codec\n");
		mIntelHDA->SendCommand(1, HDA_VERB_RESET, HDA_PARM_NULL);
        IOSleep(100); // define smaller delays ????
		mIntelHDA->SendCommand(1, HDA_VERB_RESET, HDA_PARM_NULL);
        IOSleep(100);
        // forcefully set power state to D3
		mIntelHDA->SendCommand(1, HDA_VERB_SET_PSTATE, HDA_PARM_PS_D3_HOT);
		
        eapdPoweredDown = true;
        DEBUG_LOG("CodecCommander: cc: --> hda codec power restored\n");
    }
}

/******************************************************************************
 * CodecCommander::setPowerState - set active power state
 ******************************************************************************/
IOReturn CodecCommander::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{

    if (kPowerStateSleep == powerStateOrdinal)
	{
        DEBUG_LOG("CodecCommander: cc: --> asleep\n");
        setOutputs(0x0); // set EAPD logic level 0 to cause EAPD to power off properly
        eapdPoweredDown = true;  // now it's powered down for sure
        coldBoot = false;
	}
	else if (kPowerStateNormal == powerStateOrdinal)
	{
        DEBUG_LOG("CodecCommander: cc: --> awake\n");

        // issue codec reset at wake
        performCodecReset();
        
        if (eapdPoweredDown)
            // set EAPD bit at wake or cold boot
            setOutputs(0x2);

        // if infinite checking requested
        if (mConfiguration->getCheckInfinite())
		{
            // if checking infinitely then make sure to delay workloop
            if (coldBoot)
                fTimer->setTimeoutMS(20000); // create a nasty 20sec delay for AudioEngineOutput to initialize
            // if we are waking it will be already initialized
            else
                fTimer->setTimeoutMS(100); // so fire timer for workLoop almost immediately

            DEBUG_LOG("CodecCommander: cc: --> workloop started\n");
        }
    }
    
    return IOPMAckImplied;
}
