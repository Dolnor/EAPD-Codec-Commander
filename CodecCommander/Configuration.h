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

typedef struct __attribute__((packed))
{
    UInt32 Command; // 32-bit verb to execute (Codec Address will be filled in)
    bool OnInit;    // Execute command on initialization
    bool OnSleep;   // Execute command on sleep
    bool OnWake;    // Execute command on wake
} CustomCommand;

class Configuration
{
    OSArray* mCustomCommands;
    
    bool mCheckInfinite;
    bool mPerformReset;
    bool mUpdateNodes;
    UInt16 mSendDelay, mUpdateInterval;
    
    public:
        bool getUpdateNodes();
        bool getPerformReset();
        UInt16 getSendDelay();
        bool getCheckInfinite();
        UInt16 getInterval();
    
        OSArray* getCustomCommands();
    
        // Constructor
        Configuration(OSObject* codecProfiles, UInt32 codecVendorId);
        ~Configuration();
};


#endif
