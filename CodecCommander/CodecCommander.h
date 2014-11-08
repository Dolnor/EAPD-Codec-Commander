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

enum CodecCommanderState
{
	kStateSleep,
	kStateWake,
	kStateInit
};

class CodecCommander : public IOService
{
	Configuration *mConfiguration = NULL;
	IntelHDA *mIntelHDA = NULL;
	
	IOWorkLoop*			mWorkLoop = NULL;
	IOTimerEventSource* mTimer = NULL;
	
    typedef IOService super;
	OSDeclareDefaultStructors(CodecCommander)

public:
    // standard IOKit methods
	virtual bool init(OSDictionary *dictionary = 0);
    virtual bool start(IOService *provider);
	virtual void stop(IOService *provider);
#ifdef DEBUG
    virtual void free(void);
#endif
    
    // workloop parameters
    bool startWorkLoop(IOService *provider);
    void onTimerAction();
    
    //power management event
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker);
private:
	void handleStateChange(CodecCommanderState newState);
	
	// parse codec power state from ioreg
	void parseCodecPowerState();
	
	// set the state of EAPD on outputs
	void setEAPD(unsigned char logicLevel);
	
	// reset codec
	void performCodecReset();
	
	// execute configured custom commands
	void customCommands(CodecCommanderState newState);
};

#endif // __CodecCommander__