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

#ifndef CodecCommander_IntelHDA_h
#define CodecCommander_IntelHDA_h

#include "Common.h"

// Intel HDA Registers
#define HDA_REG_GCAP		0x00	// Global Capabilities
#define HDA_REG_VMIN		0x02	// Minor Version
#define HDA_REG_VMAJ		0x03	// Major Version
#define HDA_REG_OUTPAY		0x04	// Output Payload Capability
#define HDA_REG_INPAY		0x06	// Input Payload Capability
#define HDA_REG_GCTL		0x08	// Global Control
#define HDA_REG_WAKEEN		0x0d	// Wake Enable
#define HDA_REG_STATESTS	0x0e	// State Change Status
#define HDA_REG_GSTS		0x10	// Global Status
#define HDA_REG_OUTSTRMPAY	0x18	// Output Stream Payload Capability
#define HDA_REG_INSTRMPAY	0x1a	// Input Stream Payload Capability
#define HDA_REG_INTCTL		0x20	// Interrupt Control
#define HDA_REG_INTSTS		0x24	// Interrupt Status
#define HDA_REG_WALCLK		0x30	// Wall Clock Counter
#define HDA_REG_SSYNC		0x38	// Stream Synchronization
#define HDA_REG_CORBLBA		0x40	// CORB Lower Base Address
#define HDA_REG_CORBUBA		0x44	// CORB Upper Base Address
#define HDA_REG_CORBWP		0x48	// CORB Write Pointer
#define HDA_REG_CORBRP		0x4a	// CORB Read Pointer
#define HDA_REG_CORBCTL		0x4c	// CORB Control
#define HDA_REG_CORBSTS		0x4d	// CORB Status
#define HDA_REG_CORBSIZE	0x4e	// CORB Size
#define HDA_REG_RIRBLBASE	0x50	// RIRB Lower Base Address
#define HDA_REG_RIRBUBASE	0x54	// RIRB Upper Base Address
#define HDA_REG_RIRBWP		0x58	// RIRB Write Pointer
#define HDA_REG_RINTCNT		0x5a	// Response Interrupt Count
#define HDA_REG_RIRBCTL		0x5c	// RIRB Control
#define HDA_REG_RIRBSTS		0x5d	// RIRB Status
#define HDA_REG_RIRBSIZE	0x5e	// RIRB Size
#define HDA_REG_DPLBASE		0x70	// DMA Position Lower Base Address
#define HDA_REG_DPUBASE		0x74	// DMA Position Upper Base Address
#define HDA_REG_SDCTL		0x80	// Input / Output / Bidirectional Stream Descriptor Control
#define HDA_REG_SDSTS		0x83	// Input / Output / Bidirectional Stream Descriptor Status
#define HDA_REG_SDLPIB		0x84	// Input / Output / Bidirectional Stream Descriptor Link Position in Buffer
#define HDA_REG_SDCBL		0x88	// Input / Output / Bidirectional Stream Descriptor Cyclic Buffer Length
#define HDA_REG_SDLVI		0x8C	// Input / Output / Bidirectional Stream Descriptor Last Valid Index
#define HDA_REG_SDFIFOS		0x90	// Input / Output / Bidirectional Stream Descriptor FIFO size
#define HDA_REG_SDFMT		0x92	// Input / Output / Bidirectional Stream Descriptor Format
#define HDA_REG_SDBDPL		0x98	// Input / Output / Bidirectional Stream Descriptor BDL Pointer Lower Base Address
#define HDA_REG_SDBDPU		0x9C	// Input / Output / Bidirectional Stream Descriptor BDL Pointer Upper Base Address
#define HDA_REG_WALCLKA		0x2030	// Wall Clock Counter Alias
#define HDA_REG_ICOI		0x60	// Immediate Command Output Interface
#define HDA_REG_IRII		0x64	// Immediate Reponse Input Interface
#define HDA_REG_ICS			0x68	// Immediate Command Status

#define HDA_VERB_GET_PARAM		0xF00	// Get Parameter
#define HDA_VERB_SET_PSTATE		0x705	// Set Power State
#define HDA_VERB_GET_PSTATE		0xF05	// Get Power State
#define HDA_VERB_EAPDBTL_GET	0xF0C	// EAPD/BTL Enable Get
#define HDA_VERB_EAPDBTL_SET	0x70C	// EAPD/BTL Enable Set
#define HDA_VERB_RESET			0x7FF	// Function Reset Execute

#define HDA_VERB_SET_AMP_GAIN	0x3		// Set Amp Gain / Mute
#define HDA_VERB_GET_AMP_GAIN	0xB		// Get Amp Gain / Mute

#define HDA_PARM_NULL		(UInt8)0x00	// Empty or NULL payload

#define HDA_PARM_VENDOR		(UInt8)0x00 // Vendor ID
#define HDA_PARM_REVISION	(UInt8)0x02	// Revision ID
#define HDA_PARM_NODECOUNT	(UInt8)0x04	// Subordinate Node Count
#define HDA_PARM_FUNCGRP	(UInt8)0x05	// Function Group Type
#define HDA_PARM_PINCAP		(UInt8)0x0C	// Pin Capabilities
#define HDA_PARM_PWRSTS		(UInt8)0x0F	// Supported Power States

#define HDA_PARM_PS_D0		(UInt8)0x00 // Powerstate D0: Fully on
#define HDA_PARM_PS_D1		(UInt8)0x01 // Powerstate D1
#define HDA_PARM_PS_D2		(UInt8)0x02 // Powerstate D2
#define HDA_PARM_PS_D3_HOT	(UInt8)0x03 // Powerstate D3Hot
#define HDA_PARM_PS_D3_COLD (UInt8)0x04	// Powerstate D3Cold

// Dynamic payload parameters
#define HDA_PARM_AMP_GAIN_GET(Index, Left, Output) \
	(UInt16)((Output & 0x1) << 15 | (Left & 0x01) << 13 | Index & 0xF) // Get Amp gain / mute

#define HDA_PARM_AMP_GAIN_SET(Gain, Mute, Index, SetRight, SetLeft, SetInput, SetOutput) \
	(UInt16)((SetOutput & 0x01) << 15 | (SetInput & 0x01) << 14 | (SetLeft & 0x01) << 13 | (SetRight & 0x01) << 12 | \
    (Index & 0xF) << 8 | (Mute & 0x1) << 7 | Gain & 0x7F) // Set Amp gain / mute

#define HDA_ICS_IS_BUSY(status) ((status & 0x01) == 1) // Determine Immediate Command Busy (ICB) of Immediate Command Status (ICS)
#define HDA_ICS_IS_VALID(status) (((status & 0x02) >> 1) == 1) // Determine Immediate Result Valid (IRV) of Immediate Command Status (ICS)

#define HDA_PINCAP_IS_EAPD_CAPABLE(capabilities) (((capabilities & 0xFF0000) >> 16) == 1) // Determine if this Pin widget capabilities is marked EAPD capable

// Global Capabilities response
struct HDA_GCAP
{
	UInt16 NumOutputStreamsSupported : 4;
	UInt16 NumInputStreamsSupported : 4;
	UInt16 NumBidirectionalStreamsSupported : 5;
	UInt16 NumSerialDataOutSignals : 2;
	UInt16 Supports64bits : 1;
};

// Global Capabilities & HDA Version response
struct HDA_GCAP_EXT : HDA_GCAP
{
	UInt8 MinorVersion;
	UInt8 MajorVersion;	
};

enum HDACommandMode
{
	PIO,
	DMA	
};

class IntelHDA
{
	IOService *mService = NULL;
	IOMemoryDescriptor *mDeviceMemory = NULL;
	HDACommandMode mCommandMode = PIO;
	char mCodecAddress;

	// Read-once parameters
	UInt32 mVendor = -1;
	UInt32 mNodes = -1;
	
	public:
		// Constructor
		IntelHDA(IORegistryEntry *ioRegistryEntry, HDACommandMode commandMode, char codecAddress);
		// Destructor
		~IntelHDA();
	
		// 12-bit verb and 8-bit payload
		UInt32 sendCommand(UInt32 nodeId, UInt32 verb, UInt8 payload);
		// 4-bit verb and 16-bit payload
		UInt32 sendCommand(UInt32 nodeId, UInt32 verb, UInt16 payload);
	
		// Send a raw command (verb and payload combined)
		UInt32 sendCommand(UInt32 command);
	
		UInt16 getVendorId();
		UInt16 getDeviceId();
	
		UInt8 getTotalNodes();
		UInt8 getStartingNode();
	private:
		UInt32 executePIO(UInt32 command);
};


#endif
