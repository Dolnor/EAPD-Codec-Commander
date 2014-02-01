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

#include "CCHIDKeyboard.h"

#define super IOHIKeyboard

OSDefineMetaClassAndStructors(CCHIDKeyboard, IOHIKeyboard)

/******************************************************************************
 * CCHIDKeyboard:init/start/stop/free - standard IOKit methods
 ******************************************************************************/

bool CCHIDKeyboard::init(OSDictionary *dict)
{
	DEBUG_LOG("CodecCommander: hi: keyboard initializing\n");
    return super::init(dict);
}

bool CCHIDKeyboard::start(IOService *provider)
{
	DEBUG_LOG("CodecCommander: hi: keyboard starting\n");
    if(!provider || !super::start( provider ))
	{
		DEBUG_LOG("CodecCommander: hi: keyboard failed to start\n");
		return false;
	}
	return true;
}

void CCHIDKeyboard::stop(IOService *provider)
{
	DEBUG_LOG("CodecCommander: hi: stopping\n");
    super::stop(provider);
}

void CCHIDKeyboard::free(void)
{
	super::free();
}

/******************************************************************************
* CCHIDKeyboard:message - receive scancode and send keyboard down and up event
******************************************************************************/
IOReturn CCHIDKeyboard::message( UInt32 type, IOService * provider, void * argument)
{
	if (type == kIOACPIMessageDeviceNotification)
	{
        UInt8 code = *((UInt8 *) argument);
        AbsoluteTime now;
        clock_get_uptime((uint64_t*)&now);
        dispatchKeyboardEvent(code,true, *((AbsoluteTime*)&now)); // key down
        
        clock_get_uptime((uint64_t *)&now);
        dispatchKeyboardEvent(code,false,*((AbsoluteTime*)&now)); // key up
            
        DEBUG_LOG("CodecCommander: hi: adb keycode %d out\n", code);
    }
	return kIOReturnSuccess;
}

/******************************************************************************
* CCHIDKeyboard:defaultKeymapOfLength - default keymap override for IOHIKeyboard
******************************************************************************/
const unsigned char * CCHIDKeyboard::defaultKeymapOfLength( UInt32 * length )
{
    static const unsigned char ConsumerKeyMap[] =
    {
        0x00,0x00,		// data is in bytes
        0x00,
        0x00,
        0x00,
        0x01,           // number of special keys
        
        // Special Key	  	KEYCODE
        //-----------------------------------------
        0x07,       0x4a  // NX_KEYTYPE_MUTE
    
    };
    
    if( length ) *length = sizeof( ConsumerKeyMap );
    return( ConsumerKeyMap );
}
