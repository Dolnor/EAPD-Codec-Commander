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

#ifndef CodecCommander_Configuration_h
#define CodecCommander_Configuration_h

#include "Common.h"

struct CustomCommand
{
    UInt32 Command; // 32-bit verb to execute (Codec Address will be filled in)
    bool OnInit;          // Execute command on initialization
    bool OnSleep;         // Execute command on sleep
    bool OnWake;          // Execute command on wake
};

class Configuration
{
    char mHDALocation[0x03];
    char mHDADevicePath[0x3F];
    char mHDADriverPath[0xBA];
    CustomCommand mCustomCommands[MAX_CUSTOM_COMMANDS];
    
    bool mCheckInfinite;
    bool mUpdateNodes;
    UInt8 mCodecNumber;
    UInt16 mSendDelay, mUpdateInterval;
    
    public:
        const char * getHDADevicePath();
        const char * getHDADriverPath();
        UInt8 getCodecNumber();
        bool getUpdateNodes();
        UInt16 getSendDelay();
        bool getCheckInfinite();
        UInt16 getInterval();
    
        CustomCommand* getCustomCommands();
    
        // Constructor
        Configuration(OSDictionary* dictionary);    
    private:
        static OSDictionary* loadConfiguration(OSDictionary* list);
    
        static OSString* getManufacturerNameFromOEMName(OSString *name);
        static OSString* getPlatformManufacturer();
        static OSString* getPlatformProduct();
    
        // Get config and make config dictionary by parsing plist
        static OSDictionary* getPlatformNode(OSDictionary* list, OSString *platformManufacturer, OSString *platformProduct);
};


#endif
