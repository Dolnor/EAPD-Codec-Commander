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
	
    mWorkLoop = NULL;
    mTimer = NULL;
	
	mEAPDPoweredDown = true;
	mColdBoot = true; // assume booting from cold since hibernate is broken on most hacks
	mHDACurrentPowerState = kIOAudioDeviceSleep; // assume hda codec has no power at cold boot
	mHDAPrevPowerState = mHDACurrentPowerState;  // and previous state was the same
	
    return true;
}

/******************************************************************************
 * CodecCommander::probe - Determine if the attached device is supported
 ******************************************************************************/
IOService* CodecCommander::probe(IOService* provider, SInt32* score)
{
	DEBUG_LOG("%s::probe\n", this->getName());
	
	mAudioDevice = OSDynamicCast(IOAudioDevice, provider);
	
	if (mAudioDevice)
		return super::probe(provider, score);
	
	return NULL;
}

/******************************************************************************
 * CodecCommander::start - start kernel extension and init PM
 ******************************************************************************/
bool CodecCommander::start(IOService *provider)
{
    IOLog("%s: Version 2.2.1 starting.\n", this->getName());

    if (!provider || !super::start(provider))
	{
		DEBUG_LOG("%s: Error loading kernel extension.\n", this->getName());
		return false;
	}

	mIntelHDA = new IntelHDA(mAudioDevice, PIO);
		
	if (!mIntelHDA->initialize())
		return false;
	
	// Populate HDA properties for client matching
	setProperty(kCodecVendorID, OSNumber::withNumber(mIntelHDA->getCodecVendorId(), 32));
	setProperty(kCodecAddress, OSNumber::withNumber(mIntelHDA->getCodecAddress(), 8));
	setProperty(kCodecFuncGroupType, OSNumber::withNumber(mIntelHDA->getCodecGroupType(), 8));
	
	mConfiguration = new Configuration(this->getProperty(kCodecProfile), mIntelHDA->getCodecVendorId());
	
	if (mConfiguration->getUpdateNodes())
	{
		IOSleep(mConfiguration->getSendDelay()); // need to wait a bit until codec can actually respond to immediate verbs

		// Fetch Pin Capabilities from the range of nodes
		DEBUG_LOG("%s: Getting EAPD supported node list.\n", this->getName());
		
		mEAPDCapableNodes = OSArray::withCapacity(0);
		
		for (int nodeId = mIntelHDA->getStartingNode(); nodeId <= mIntelHDA->getTotalNodes(); nodeId++)
		{
			UInt32 response = mIntelHDA->sendCommand(nodeId, HDA_VERB_GET_PARAM, HDA_PARM_PINCAP);
			
			if (response == -1)
			{
				DEBUG_LOG("%s: Failed to retrieve pin capabilities for node 0x%02x.\n", this->getName(), nodeId);
				continue;
			}
			
			// if bit 16 is set in pincap - node supports EAPD
			if (HDA_PINCAP_IS_EAPD_CAPABLE(response))
			{
				mEAPDCapableNodes->setObject(OSNumber::withNumber(nodeId, 8));
				IOLog("%s: Node ID 0x%02x supports EAPD, will update state after sleep.\n", this->getName(), nodeId);
			}
		}
	}
	
	// Execute any custom commands registered for initialization
	customCommands(kStateInit);
	
    // notify about extra feature requests
    if (mConfiguration->getCheckInfinite())
        DEBUG_LOG("%s: Infinite workloop requested, will start now!\n", this->getName());
    
    // init power state management & set state as PowerOn
    PMinit();
    registerPowerDriver(this, powerStateArray, kPowerStateCount);
	provider->joinPMtree(this);
    
    // setup workloop and timer
    mWorkLoop = IOWorkLoop::workLoop();
    mTimer = IOTimerEventSource::timerEventSource(this,
                                                  OSMemberFunctionCast(IOTimerEventSource::Action, this,
                                                  &CodecCommander::onTimerAction));
    if (!mWorkLoop || !mTimer)
        stop(provider);;
    
    if (mWorkLoop->addEventSource(mTimer) != kIOReturnSuccess)
        stop(provider);
    
	this->registerService(0);
    return true;
}

/******************************************************************************
 * CodecCommander::stop & free - stop and free kernel extension
 ******************************************************************************/
void CodecCommander::stop(IOService *provider)
{
    DEBUG_LOG("%s: Stopping...\n", this->getName());

    // if workloop is active - release it
    mTimer->cancelTimeout();
    mWorkLoop->removeEventSource(mTimer);
    OSSafeReleaseNULL(mTimer);// disable outstanding calls
    OSSafeReleaseNULL(mWorkLoop);
	
    PMstop();
	
	// Free IntelHDA engine
	if (mIntelHDA)
		delete mIntelHDA;
	
	// Free Configuration
	if (mConfiguration)
		delete mConfiguration;
	
	OSSafeReleaseNULL(mEAPDCapableNodes);
	
    super::stop(provider);
}

/******************************************************************************
 * CodecCommander::onTimerAction - repeats the action each time timer fires
 ******************************************************************************/
void CodecCommander::onTimerAction()
{
    // check if hda codec is powered - we are monitoring ocurrences of fugue state
	mHDACurrentPowerState = mAudioDevice->getPowerState();
	
	// if hda codec changed power state
	if (mHDACurrentPowerState != mHDAPrevPowerState)
	{
		DEBUG_LOG("%s: Power state transition from %s to %s recorded.\n",
				  this->getName(), getPowerState(mHDAPrevPowerState), getPowerState(mHDACurrentPowerState));
		
		// store current power state as previous state for next workloop cycle
		mHDAPrevPowerState = mHDACurrentPowerState;
		
		// notify about codec power loss state
		if (mHDACurrentPowerState == kIOAudioDeviceSleep)
		{
			DEBUG_LOG("%s: HDA codec lost power\n", this->getName());
			handleStateChange(kIOAudioDeviceSleep); // power down EAPDs properly
			mEAPDPoweredDown = true;
			mColdBoot = false; //codec entered fugue state or sleep - no longer a cold boot
		}
	}
	
    // if no power after semi-sleep (fugue) state and power was restored - set EAPD bit
	if (mEAPDPoweredDown && mHDACurrentPowerState != kIOAudioDeviceSleep)
    {
        DEBUG_LOG("%s: --> hda codec power restored\n", this->getName());
		handleStateChange(kIOAudioDeviceActive);
    }
    
    mTimer->setTimeoutMS(mConfiguration->getInterval());
}

/******************************************************************************
 * CodecCommander::handleStateChange - handles transitioning from one state to another, i.e. sleep --> wake
 ******************************************************************************/
void CodecCommander::handleStateChange(IOAudioDevicePowerState newState)
{
	switch (newState)
	{
		case kIOAudioDeviceSleep:
			if (mConfiguration->getUpdateNodes())
				setEAPD(0x0);
			
			customCommands(kStateSleep);
			break;
		case kIOAudioDeviceIdle:
		case kIOAudioDeviceActive:
			if (mConfiguration->getUpdateNodes())
				setEAPD(0x02);
			
			customCommands(kStateWake);
			break;
	}
}

/******************************************************************************
 * CodecCommander::customCommands - fires all configured custom commands
 ******************************************************************************/
void CodecCommander::customCommands(CodecCommanderState newState)
{
	OSCollectionIterator* iterator = OSCollectionIterator::withCollection(mConfiguration->getCustomCommands());
	
	OSData* data;
	
	while ((data = OSDynamicCast(OSData, iterator->getNextObject())))
	{
		CustomCommand* customCommand = (CustomCommand*)data->getBytesNoCopy();

		if ((customCommand->OnInit == (newState == kStateInit)) ||
			(customCommand->OnWake == (newState == kStateWake)) ||
			(customCommand->OnSleep == (newState == kStateSleep)))
		{
			DEBUG_LOG("%s: --> custom command 0x%08x\n", this->getName(), customCommand->Command);
			mIntelHDA->sendCommand(customCommand->Command);
		}
	}
	
	OSSafeRelease(iterator);
}

/******************************************************************************
 * CodecCommander::setOutputs - set EAPD status bit on SP/HP
 ******************************************************************************/
void CodecCommander::setEAPD(UInt8 logicLevel)
{
    // delay by at least 100ms, otherwise first immediate command won't be received
    // some codecs will produce loud pop when EAPD is enabled too soon, need custom delay until codec inits
    if (mConfiguration->getSendDelay() < 100)
        IOSleep(100);
    else
        IOSleep(mConfiguration->getSendDelay());
	
    // for nodes supporting EAPD bit 1 in logicLevel defines EAPD logic state: 1 - enable, 0 - disable
	OSCollectionIterator* iterator = OSCollectionIterator::withCollection(mEAPDCapableNodes);
	
	OSNumber* nodeId;
	
	while ((nodeId = OSDynamicCast(OSNumber, iterator->getNextObject())))
		mIntelHDA->sendCommand(nodeId->unsigned8BitValue(), HDA_VERB_EAPDBTL_SET, logicLevel);
	
	OSSafeRelease(iterator);
	
	mEAPDPoweredDown = false;
}

/******************************************************************************
 * CodecCommander::performCodecReset - reset function group and set power to D3
 *****************************************************************************/
void CodecCommander::performCodecReset()
{
    /*
     Reset is created by sending two Function Group resets, potentially separated 
     by an undefined number of idle frames, but no other valid commands.
     This Function Group “Double” reset shall do a full initialization and reset 
     most settings to their power on defaults.
     
     This function can be used to reset codec on dekstop boards, for example H87-HD3,
     to overcome audio loss and jack sense problem after sleep with AppleHDA v2.6.0+
     */

    if (!mColdBoot)
	{
        DEBUG_LOG("%s: --> resetting codec\n", this->getName());
		mIntelHDA->sendCommand(1, HDA_VERB_RESET, HDA_PARM_NULL);
        IOSleep(100); // define smaller delays ????
		mIntelHDA->sendCommand(1, HDA_VERB_RESET, HDA_PARM_NULL);
        IOSleep(100);
        // forcefully set power state to D3
		mIntelHDA->sendCommand(1, HDA_VERB_SET_PSTATE, HDA_PARM_PS_D3_HOT);
		
        mEAPDPoweredDown = true;
		DEBUG_LOG("%s: --> hda codec power restored\n", this->getName());
    }
}

/******************************************************************************
 * CodecCommander::setPowerState - set active power state
 ******************************************************************************/
IOReturn CodecCommander::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
	switch (powerStateOrdinal)
	{
		case kPowerStateSleep:
			DEBUG_LOG("%s: --> asleep\n", this->getName());
			handleStateChange(kIOAudioDeviceSleep); // set EAPD logic level 0 to cause EAPD to power off properly
			mEAPDPoweredDown = true;  // now it's powered down for sure
			mColdBoot = false;
			break;
		case kPowerStateNormal:
			DEBUG_LOG("%s: --> awake\n", this->getName());
			
			if (mConfiguration->getPerformReset())
				// issue codec reset at wake and cold boot
				performCodecReset();
			
			if (mEAPDPoweredDown)
				// set EAPD bit at wake or cold boot
				handleStateChange(kIOAudioDeviceActive);
			
			// if infinite checking requested
			if (mConfiguration->getCheckInfinite())
			{
				// if checking infinitely then make sure to delay workloop
				if (mColdBoot)
					mTimer->setTimeoutMS(20000); // create a nasty 20sec delay for AudioEngineOutput to initialize
				// if we are waking it will be already initialized
				else
					mTimer->setTimeoutMS(100); // so fire timer for workLoop almost immediately
				
				DEBUG_LOG("%s: --> workloop started\n", this->getName());
			}
			break;
	}
	
    return IOPMAckImplied;
}

/******************************************************************************
 * CodecCommander::executeCommand - Execute an external command
 ******************************************************************************/
UInt32 CodecCommander::executeCommand(UInt32 command)
{
	if (mIntelHDA)
		return mIntelHDA->sendCommand(command);
	
	return -1;
}

/******************************************************************************
 * CodecCommander::getPowerState - Get a textual description for a IOAudioDevicePowerState
 ******************************************************************************/
const char* CodecCommander::getPowerState(IOAudioDevicePowerState powerState)
{
	static const IONamedValue state_values[] = {
		{kIOAudioDeviceSleep,  "Sleep"  },
		{kIOAudioDeviceIdle,   "Idle"   },
		{kIOAudioDeviceActive, "Active" },
		{0,                    NULL     }
	};
	
	return IOFindNameForValue(powerState, state_values);
}
