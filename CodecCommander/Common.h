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

#ifndef CodecCommander_Common_h
#define CodecCommander_Common_h

#ifdef DEBUG
#define DEBUG_LOG(args...)  IOLog(args)
#else
#define DEBUG_LOG(args...)
#endif

#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/pci/IOPCIDevice.h>

#define MAX_EAPD_NODES 5
#define MAX_CUSTOM_COMMANDS 32

#endif
