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

//REVIEW: getHDADriver and getAudioDevice are only used by "Check Infinitely"
// Note: "Check Infinitely" should be called "Check Periodically"

static IOAudioDevice* getHDADriver(IORegistryEntry* registryEntry)
{
	IOAudioDevice* audioDevice = NULL;
	while (registryEntry)
	{
		audioDevice = OSDynamicCast(IOAudioDevice, registryEntry);
		if (audioDevice)
			break;
		registryEntry = registryEntry->getChildEntry(gIOServicePlane);
	}
#ifdef DEBUG
	if (!audioDevice)
		AlwaysLog("getHDADriver unable to find IOAudioDevice\n");
#endif
	return audioDevice;
}

IOAudioDevice* CodecCommander::getAudioDevice()
{
	if (!mAudioDevice)
	{
		mAudioDevice = getHDADriver(mProvider);
		if (mAudioDevice)
			mAudioDevice->retain();
	}
	return mAudioDevice;
}

/******************************************************************************
 * CodecCommander::init - parse kernel extension Info.plist
 ******************************************************************************/
bool CodecCommander::init(OSDictionary *dictionary)
{
    DebugLog("Initializing\n");
    
    if (!super::init(dictionary))
        return false;
	
    mWorkLoop = NULL;
    mTimer = NULL;
	
	mEAPDPoweredDown = true;
	mColdBoot = true; // assume booting from cold since hibernate is broken on most hacks
	mHDAPrevPowerState = kIOAudioDeviceSleep; // assume hda codec has no power at cold boot

    return true;
}

/******************************************************************************
 * CodecCommander::probe - Determine if the attached device is supported
 ******************************************************************************/
IOService* CodecCommander::probe(IOService* provider, SInt32* score)
{
	DebugLog("Probe\n");
	
	return super::probe(provider, score);
}

static void setNumberProperty(IOService* service, const char* key, UInt32 value)
{
	OSNumber* num = OSNumber::withNumber(value, 32);
	if (num)
	{
		service->setProperty(key, num);
		num->release();
	}
}

/******************************************************************************
 * CodecCommander::start - start kernel extension and init PM
 ******************************************************************************/
bool CodecCommander::start(IOService *provider)
{
	extern kmod_info_t kmod_info;
	
    AlwaysLog("Version %s starting.\n", kmod_info.version);

    if (!provider || !super::start(provider))
	{
		DebugLog("Error loading kernel extension.\n");
		return false;
	}

	// cache the provider
	mProvider = provider;
	mProvider->retain();

	mIntelHDA = new IntelHDA(provider, PIO);
	if (!mIntelHDA || !mIntelHDA->initialize())
	{
		AlwaysLog("Error initializing IntelHDA instance\n");
		stop(provider);
		return false;
	}
	
	// Populate HDA properties for client matching
	setNumberProperty(this, kCodecVendorID, mIntelHDA->getCodecVendorId());
	setNumberProperty(this, kCodecAddress, mIntelHDA->getCodecAddress());
	setNumberProperty(this, kCodecFuncGroupType, mIntelHDA->getCodecGroupType());
	
	mConfiguration = new Configuration(this->getProperty(kCodecProfile), mIntelHDA->getCodecVendorId(), mIntelHDA->getSubsystemId(), mIntelHDA->getPCISubId());
	if (!mConfiguration || mConfiguration->getDisable())
	{
		stop(provider);
		return false;
	}
#ifdef DEBUG
	setProperty("Merged Profile", mConfiguration->mConfig);
#endif

	if (mConfiguration->getUpdateNodes())
	{
		// need to wait a bit until codec can actually respond to immediate verbs
		IOSleep(mConfiguration->getSendDelay());

		// Fetch Pin Capabilities from the range of nodes
		DebugLog("Getting EAPD supported node list.\n");
		
		mEAPDCapableNodes = OSArray::withCapacity(3);
		if (!mEAPDCapableNodes)
		{
			stop(provider);
			return false;
		}
		
		for (int nodeId = mIntelHDA->getStartingNode(); nodeId <= mIntelHDA->getTotalNodes(); nodeId++)
		{
			UInt32 response = mIntelHDA->sendCommand(nodeId, HDA_VERB_GET_PARAM, HDA_PARM_PINCAP);
			if (response == -1)
			{
				DebugLog("Failed to retrieve pin capabilities for node 0x%02x.\n", nodeId);
				continue;
			}
			
			// if bit 16 is set in pincap - node supports EAPD
			if (HDA_PINCAP_IS_EAPD_CAPABLE(response))
			{
				OSNumber* num = OSNumber::withNumber(nodeId, 8);
				if (num)
				{
					mEAPDCapableNodes->setObject(num);
					num->release();
				}
				AlwaysLog("Node ID 0x%02x supports EAPD, will update state after sleep.\n", nodeId);
			}
		}
	}
	
	// Execute any custom commands registered for initialization
	customCommands(kStateInit);
	
    // init power state management & set state as PowerOn
    PMinit();
    registerPowerDriver(this, powerStateArray, kPowerStateCount);
	provider->joinPMtree(this);

	// no need to start timer unless "Check Infinitely" is enabled
	if (mConfiguration->getCheckInfinite())
	{
		DebugLog("Infinite workloop requested, will start now!\n");

		// setup workloop and timer
		mWorkLoop = IOWorkLoop::workLoop();
		mTimer = IOTimerEventSource::timerEventSource(this,
													  OSMemberFunctionCast(IOTimerEventSource::Action, this,
													  &CodecCommander::onTimerAction));
		if (!mWorkLoop || !mTimer)
		{
			stop(provider);
			return false;
		}

		if (mWorkLoop->addEventSource(mTimer) != kIOReturnSuccess)
		{
			stop(provider);
			return false;
		}
	}

	this->registerService(0);
    return true;
}

/******************************************************************************
 * CodecCommander::stop & free - stop and free kernel extension
 ******************************************************************************/
void CodecCommander::stop(IOService *provider)
{
    DebugLog("Stopping...\n");

    // if workloop is active - release it
	if (mTimer)
		mTimer->cancelTimeout();
	if (mWorkLoop && mTimer)
		mWorkLoop->removeEventSource(mTimer);
    OSSafeReleaseNULL(mTimer);// disable outstanding calls
    OSSafeReleaseNULL(mWorkLoop);
	
    PMstop();
	
	// Free IntelHDA engine
	delete mIntelHDA;
	mIntelHDA = NULL;
	
	// Free Configuration
	delete mConfiguration;
	mConfiguration = NULL;
	
	OSSafeReleaseNULL(mEAPDCapableNodes);
	OSSafeReleaseNULL(mAudioDevice);
	OSSafeReleaseNULL(mProvider);
	
    super::stop(provider);
}

/******************************************************************************
 * CodecCommander::onTimerAction - repeats the action each time timer fires
 ******************************************************************************/
void CodecCommander::onTimerAction()
{
	mTimer->setTimeoutMS(mConfiguration->getInterval());

	IOAudioDevice* audioDevice = getAudioDevice();
	if (!audioDevice)
		return;

    // check if hda codec is powered - we are monitoring ocurrences of fugue state
	IOAudioDevicePowerState powerState = audioDevice->getPowerState();
	
	// if hda codec changed power state
	if (powerState != mHDAPrevPowerState)
	{
		DebugLog("Power state transition from %s to %s recorded.\n",
				  getPowerState(mHDAPrevPowerState), getPowerState(powerState));
		
		// store current power state as previous state for next workloop cycle
		mHDAPrevPowerState = powerState;
		
		// notify about codec power loss state
		if (powerState == kIOAudioDeviceSleep)
		{
			DebugLog("HDA codec lost power\n");
			handleStateChange(kIOAudioDeviceSleep); // power down EAPDs properly
			mEAPDPoweredDown = true;
			mColdBoot = false; //codec entered fugue state or sleep - no longer a cold boot
		}

		// if no power after semi-sleep (fugue) state and power was restored - set EAPD bit
		if (powerState != kIOAudioDeviceSleep)
		{
			DebugLog("--> hda codec power restored\n");
			handleStateChange(kIOAudioDeviceActive);
			mEAPDPoweredDown = false;
		}
	}
}

/******************************************************************************
 * CodecCommander::handleStateChange - handles transitioning from one state to another, i.e. sleep --> wake
 ******************************************************************************/
void CodecCommander::handleStateChange(IOAudioDevicePowerState newState)
{
	switch (newState)
	{
		case kIOAudioDeviceSleep:
			if (mConfiguration->getSleepNodes())
			{
				if (!setEAPD(0x00) && mConfiguration->getPerformResetOnEAPDFail())
				{
					AlwaysLog("BLURP! setEAPD(0x00) failed... attempt fix with codec reset\n");
					performCodecReset();
					setEAPD(0x00);
				}
			}

			customCommands(kStateSleep);
			break;

		case kIOAudioDeviceIdle:	// note kIOAudioDeviceIdle is not used
		case kIOAudioDeviceActive:
			mIntelHDA->applyIntelTCSEL();
			
			if (mConfiguration->getUpdateNodes())
			{
				if (!setEAPD(0x02) && mConfiguration->getPerformResetOnEAPDFail())
				{
					AlwaysLog("BLURP! setEAPD(0x02) failed... attempt fix with codec reset\n");
					performCodecReset();
					setEAPD(0x02);
				}
			}

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
	if (!iterator) return;
	
	while (OSData* data = OSDynamicCast(OSData, iterator->getNextObject()))
	{
		CustomCommand* customCommand = (CustomCommand*)data->getBytesNoCopy();

		if ((customCommand->OnInit && (newState == kStateInit)) ||
			(customCommand->OnWake && (newState == kStateWake)) ||
			(customCommand->OnSleep && (newState == kStateSleep)))
		{
			for (int i = 0; i < customCommand->CommandCount; i++)
			{
				DebugLog("--> custom command 0x%08x\n", customCommand->Commands[i]);
				executeCommand(customCommand->Commands[i]);
			}
		}
	}
	
	iterator->release();
}

/******************************************************************************
 * CodecCommander::setOutputs - set EAPD status bit on SP/HP
 ******************************************************************************/
bool CodecCommander::setEAPD(UInt8 logicLevel)
{
    // some codecs will produce loud pop when EAPD is enabled too soon, need custom delay until codec inits
    IOSleep(mConfiguration->getSendDelay());
	
    // for nodes supporting EAPD bit 1 in logicLevel defines EAPD logic state: 1 - enable, 0 - disable
	OSCollectionIterator* iterator = OSCollectionIterator::withCollection(mEAPDCapableNodes);
	if (!iterator) return true;
	
	bool result = true;
	while (OSNumber* nodeId = OSDynamicCast(OSNumber, iterator->getNextObject()))
	{
		if (-1 == mIntelHDA->sendCommand(nodeId->unsigned8BitValue(), HDA_VERB_EAPDBTL_SET, logicLevel))
			result = false;
	}
	iterator->release();

	return result;
}

/******************************************************************************
 * CodecCommander::performCodecReset - reset function group and set power to D3
 *****************************************************************************/
void CodecCommander::performCodecReset()
{
	/*
     This function can be used to reset codec on dekstop boards, for example H87-HD3,
     to overcome audio loss and jack sense problem after sleep with AppleHDA v2.6.0+
     */

    if (!mColdBoot)
	{
		mIntelHDA->resetCodec();
        mEAPDPoweredDown = true;
    }
}

/******************************************************************************
 * CodecCommander::setPowerState - set active power state
 ******************************************************************************/
IOReturn CodecCommander::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
	DebugLog("setPowerState %ld\n", powerStateOrdinal);

	if (mPrevPowerStateOrdinal == powerStateOrdinal)
	{
		DebugLog("setPowerState same power state\n");
		return IOPMAckImplied;
	}
	mPrevPowerStateOrdinal = powerStateOrdinal;

	switch (powerStateOrdinal)
	{
		case kPowerStateSleep:
			AlwaysLog("--> asleep(%d)\n", (int)powerStateOrdinal);
			mColdBoot = false;
			handleStateChange(kIOAudioDeviceSleep); // set EAPD logic level 0 to cause EAPD to power off properly
			mEAPDPoweredDown = true;  // now it's powered down for sure
			break;

		case kPowerStateDoze:	// note kPowerStateDoze never happens
		case kPowerStateNormal:
			AlwaysLog("--> awake(%d)\n", (int)powerStateOrdinal);
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
				
				DebugLog("--> workloop started\n");
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


/******************************************************************************
 * CodecCommander_PowerHook - for tracking power states of IOAudioDevice nodes
 ******************************************************************************/

OSDefineMetaClassAndStructors(CodecCommanderPowerHook, IOService)

bool CodecCommanderPowerHook::init(OSDictionary *dictionary)
{
	DebugLog("CodecCommanderPowerHook::init\n");

	if (!super::init(dictionary))
		return false;

	return true;
}

IOService* CodecCommanderPowerHook::probe(IOService* provider, SInt32* score)
{
	DebugLog("CodecCommanderPowerHook::probe\n");

	return super::probe(provider, score);
}

bool CodecCommanderPowerHook::start(IOService *provider)
{
	DebugLog("CodecCommanderPowerHook::start\n");

	if (!provider || !super::start(provider))
	{
		DebugLog("Error loading kernel extension.\n");
		return false;
	}

	// walk up tree to find associated IOHDACodecFunction
	IORegistryEntry* entry = provider;
	while (entry)
	{
		if (OSDynamicCast(OSNumber, entry->getProperty(kCodecSubsystemID)))
			break;
		entry = entry->getParentEntry(gIOServicePlane);
	}
	if (!entry)
	{
		DebugLog("parent entry IOHDACodecFunction not found\n");
		return false;
	}
	// look at children for CodecCommander instance
	OSIterator* iter = entry->getChildIterator(gIOServicePlane);
	if (!iter)
	{
		DebugLog("can't get child iterator\n");
		return false;
	}
	while (OSObject* entry = iter->getNextObject())
	{
		CodecCommander* commander = OSDynamicCast(CodecCommander, entry);
		if (commander)
		{
			mCodecCommander = commander;
			mCodecCommander->retain();
			break;
		}
	}
	iter->release();

	// if no CodecCommander instance found, don't attach
	if (!mCodecCommander)
	{
		DebugLog("no CodecCommander found with child iterator\n");
		return false;
	}

	// init power state management & set state as PowerOn
	PMinit();
	registerPowerDriver(this, powerStateArray, kPowerStateCount);
	provider->joinPMtree(this);

	this->registerService(0);
	return true;
}

void CodecCommanderPowerHook::stop(IOService *provider)
{
	OSSafeReleaseNULL(mCodecCommander);

	PMstop();

	super::stop(provider);
}

IOReturn CodecCommanderPowerHook::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
	DebugLog("PowerHook: setPowerState %ld\n", powerStateOrdinal);

	if (mCodecCommander)
		return mCodecCommander->setPowerState(powerStateOrdinal, policyMaker);

	return IOPMAckImplied;
}
