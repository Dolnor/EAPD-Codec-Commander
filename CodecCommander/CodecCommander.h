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

#define CodecCommander org_tw_CodecCommander

#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceTreeSupport.h>

#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif

#ifdef DEBUG_MSG
#define DEBUG_LOG(args...)  IOLog(args)
#else
#define DEBUG_LOG(args...)
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// define & enumerate power states
enum
{
	kPowerStateOff = 0,
	kPowerStateOn,
	kPowerStateCount
};

OSString* getManufacturerNameFromOEMName(OSString *name);

class EXPORT CodecCommander : public IOService
{
    typedef IOService super;
	OSDeclareDefaultStructors(org_tw_CodecCommander)

public:
    // standart IOKit methods
	virtual bool init(OSDictionary *dictionary = 0);
    virtual IOService *probe(IOService *provider, SInt32 *score);
    virtual bool start(IOService *provider);
	virtual void stop(IOService *provider);
    
    //power management event
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker);
    
    static OSDictionary* getConfigurationNode(OSDictionary* list, OSString* model = 0);
    static OSDictionary* makeConfigurationNode(OSDictionary* list, OSString* model = 0);

private:
    // set info.plist defined parameters
    void setParamPropertiesGated(OSDictionary* dict);
    
protected:
    // handle codec verb command
    void handleCommand(UInt32 cmd);
};

#endif // __CodecCommander__