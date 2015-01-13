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

#ifndef __CodecCommander__
#define __CodecCommander__

#define CodecCommander CodecCommander

#include "Common.h"
#include "Configuration.h"
#include "IntelHDA.h"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// define & enumerate power states
enum
{
	kPowerStateSleep    = 0,
    kPowerStateDoze     = 1,
	kPowerStateNormal   = 2,
	kPowerStateCount
};

// Track audio codec state transitions
enum CodecCommanderState
{
	kStateSleep,
	kStateWake,
	kStateInit
};

// External client methods
enum
{
	kClientExecuteVerb = 0,
	kClientNumMethods
};

class CodecCommander : public IOService
{
    typedef IOService super;
	OSDeclareDefaultStructors(CodecCommander)

public:
    // standard IOKit methods
	virtual bool init(OSDictionary *dictionary = 0);
	virtual IOService* probe (IOService* provider, SInt32* score);
    virtual bool start(IOService *provider);
	virtual void stop(IOService *provider);
	
    // workloop parameters
    bool startWorkLoop(IOService *provider);
    void onTimerAction();
    
    // power management event
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker);
	
	UInt32 executeCommand(UInt32 command);
private:
	IOAudioDevice* mAudioDevice = NULL;
	IOAudioDevicePowerState mHDACurrentPowerState, mHDAPrevPowerState;
	
	Configuration *mConfiguration = NULL;
	IntelHDA *mIntelHDA = NULL;
	
	IOWorkLoop* mWorkLoop = NULL;
	IOTimerEventSource* mTimer = NULL;
	
	// Define variables for EAPD state updating
	OSArray* mEAPDCapableNodes = NULL;
	
	bool mEAPDPoweredDown, mColdBoot;
		
	void handleStateChange(IOAudioDevicePowerState newState);
	
	// parse codec power state from ioreg
	void parseCodecPowerState();
	
	// set the state of EAPD on outputs
	void setEAPD(UInt8 logicLevel);
	
	// reset codec
	void performCodecReset();
	
	// execute configured custom commands
	void customCommands(CodecCommanderState newState);
	
	static const char* getPowerState(IOAudioDevicePowerState powerState);
};

class CodecCommanderClient : public IOUserClient
{
	/*
	 * Declare the metaclass information that is used for runtime
	 * typechecking of IOKit objects.
	 */
	OSDeclareDefaultStructors(CodecCommanderClient);
	
	private:
		CodecCommander* mDriver;
		task_t mTask;
		SInt32 mOpenCount;
	
		static const IOExternalMethodDispatch sMethods[kClientNumMethods];
	public:
		/* IOService overrides */
		virtual bool start(IOService* provider);
		virtual void stop(IOService* provider);
	
		/* IOUserClient overrides */
		virtual bool initWithTask(task_t owningTask, void * securityID, UInt32 type, OSDictionary* properties);
		virtual IOReturn clientClose(void);
	
		virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments, IOExternalMethodDispatch* dispatch = 0,
										OSObject* target = 0, void* reference = 0);
	
	
		/* External methods */
		static IOReturn executeVerb(CodecCommander* target, void* reference, IOExternalMethodArguments* arguments);
};
#endif // __CodecCommander__