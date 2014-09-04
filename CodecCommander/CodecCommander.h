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

#ifdef DEBUG_MSG
#define DEBUG_LOG(args...)  IOLog(args)
#else
#define DEBUG_LOG(args...)
#endif

#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceTreeSupport.h>

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// define & enumerate power states
enum
{
	kPowerStateSleep    = 0,
    kPowerStateDoze     = 1,
	kPowerStateNormal   = 2,
	kPowerStateCount
};

OSString* getManufacturerNameFromOEMName(OSString *name);

class CodecCommander : public IOService
{
    typedef IOService super;
	OSDeclareDefaultStructors(CodecCommander)

public:
    // standart IOKit methods
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
    
    // get confing and make config dictionary by parsing plist
    static OSDictionary* getConfigurationNode(OSDictionary* list, OSString* model = 0);
    static OSDictionary* makeConfigurationNode(OSDictionary* list, OSString* model = 0);

private:   
    // set plist dictionary parameters
    void setParamPropertiesGated(OSDictionary* dict);
    
protected:
    // parse codec power state from ioreg
    void parseCodecPowerState();
    
    // handle codec verb command and read response
    void setStatus(UInt32 cmd);
    void getStatus(UInt32 cmd);
    void clearIRV();
    
    // set the state of EAPD on outputs
    void setOutputs(UInt8 logicLevel);
    
    // reset codec
    void performCodecReset ();
    
    IOWorkLoop*			fWorkLoop;		// our workloop
    IOTimerEventSource* fTimer;	// used to simulate capture hardware
};

#endif // __CodecCommander__