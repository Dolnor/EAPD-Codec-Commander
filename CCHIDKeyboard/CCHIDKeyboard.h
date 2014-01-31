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

#ifndef __CodecCommander__CCHIDKeyboard__
#define __CodecCommander__CCHIDKeyboard__

#ifdef DEBUG_MSG
#define DEBUG_LOG(args...)  IOLog(args)
#else
#define DEBUG_LOG(args...)
#endif

#include <IOKit/hidsystem/IOHIKeyboard.h>

#define kIOACPIMessageDeviceNotification iokit_family_msg(sub_iokit_acpi, 0x10)

class CCHIDKeyboardDevice;

class CCHIDKeyboard : public IOHIKeyboard
{
	OSDeclareDefaultStructors(CCHIDKeyboard)
	
public:
	// standard IOKit methods
	virtual bool       init(OSDictionary *dictionary = 0);
	virtual bool       start(IOService *provider);
	virtual void       stop(IOService *provider);
	virtual void       free(void);
	
	IOReturn message( UInt32 type, IOService * provider, void * argument);
	
	// IOHIKeyboard specific methods
	virtual const unsigned char * defaultKeymapOfLength(UInt32 * length);
};

#endif /* defined(__CodecCommander__CCHIDKeyboard__) */
