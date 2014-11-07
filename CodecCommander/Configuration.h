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

class Configuration
{
    char mHDALocation[0x03];
    char mHDADevicePath[0x3F];
    char mHDADriverPath[0xBA];
    
    bool mCheckInfinite;
    unsigned char mCodecNumber;
    unsigned short mSendDelay, mUpdateInterval;
    
    public:
        const char * getHDADevicePath();
        const char * getHDADriverPath();
        unsigned char getCodecNumber();
        unsigned short getSendDelay();
        bool getCheckInfinite();
        unsigned short getInterval();
    
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
