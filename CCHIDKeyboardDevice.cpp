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

#include "CCHIDKeyboardDevice.h"
#include "CodecCommander.h"

#define super IOService


OSDefineMetaClassAndStructors(CCHIDKeyboardDevice, IOService);

// define custom reference keyboard map
const CCKeyMap CCHIDKeyboardDevice::keyMap[] = {
	{0x20, 0x4a, "NX_KEYTYPE_MUTE"},
	{0,0xFF,NULL}
};

bool CCHIDKeyboardDevice::attach( IOService * provider)
{
	DEBUG_LOG("CodecCommander: hi: keyboard device attach\n");
    
    if( !super::attach(provider) )
        return false;
	
	keyboardNest = OSDynamicCast(CodecCommander, provider);
	if (NULL == keyboardNest)
		return false;
	keyboardNest->retain();
	
	return true;
}

void CCHIDKeyboardDevice::detach( IOService * provider )
{
    DEBUG_LOG("CodecCommander: hi: keyboard device detach\n");
	keyboardNest->release();
	keyboardNest = 0;
    
	super::detach(provider);
}

void CCHIDKeyboardDevice::keyPressed(int code)
{
	int i = 0, out;
	do
	{
		if (keyMap[i].description == NULL && keyMap[i].in == 0 && keyMap[i].out == 0xFF)
		{
			DEBUG_LOG("CodecCommander: hi: unknown scancode %02X\n", code);
			break;
		}
		if (keyMap[i].in == code)
		{
            DEBUG_LOG("CodecCommander: hi: scancode %02X in\n", code);
			out = keyMap[i].out;
			messageClients(kIOACPIMessageDeviceNotification, &out);
			break;
		}
		i++;
	}
	while (true);
}
