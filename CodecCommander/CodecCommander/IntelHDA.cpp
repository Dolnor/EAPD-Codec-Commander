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

#include "IntelHDA.h"

IntelHDA::IntelHDA(IORegistryEntry *ioRegistryEntry, HDACommandMode commandMode, char codecAddress)
{
    mCommandMode = commandMode;
    mCodecAddress = codecAddress;
    
    if (ioRegistryEntry == NULL)
        return;
    
    mService = OSDynamicCast(IOService, ioRegistryEntry);
    
    // get address field from IODeviceMemory
    if (mService != NULL && mService->getDeviceMemoryCount() != 0)
    {
        mDeviceMemory = mService->getDeviceMemoryWithIndex(0);

        // Determine codec global capabilities
        HDA_GCAP_EXT globalCapabilities;
        mDeviceMemory->readBytes(HDA_REG_GCAP, &globalCapabilities, sizeof(HDA_GCAP_EXT));
        
        IOLog("CodecCommander::IntelHDA\n");
        IOLog("....Output Streams:\t%d\n", globalCapabilities.NumOutputStreamsSupported);
        IOLog("....Input Streams:\t%d\n", globalCapabilities.NumInputStreamsSupported);
        IOLog("....Bidi Streams:\t%d\n", globalCapabilities.NumBidirectionalStreamsSupported);
        IOLog("....Serial Data:\t%d\n", globalCapabilities.NumSerialDataOutSignals);
        IOLog("....x64 Support:\t%d\n", globalCapabilities.Supports64bits);
        IOLog("....Codec Version:\t%d.%d\n", globalCapabilities.MajorVersion, globalCapabilities.MinorVersion);
        IOLog("....Vendor Id:\t0x%04x\n", this->getVendorId());
        IOLog("....Device Id:\t0x%04x\n", this->getDeviceId());
    }
}

IntelHDA::~IntelHDA()
{
    //OSSafeReleaseNULL(mDeviceMemory);
    //OSSafeReleaseNULL(mService);
}

UInt16 IntelHDA::getVendorId()
{
    if (mVendor == -1)
        mVendor = this->sendCommand(0, HDA_VERB_GET_PARAM, HDA_PARM_VENDOR);
    
    return mVendor >> 16;
}

UInt16 IntelHDA::getDeviceId()
{
    if (mVendor == -1)
        mVendor = this->sendCommand(0, HDA_VERB_GET_PARAM, HDA_PARM_VENDOR);
    
    return mVendor & 0xFFFF;
}

UInt8 IntelHDA::getTotalNodes()
{
    if (mNodes == -1)
        mNodes = this->sendCommand(1, HDA_VERB_GET_PARAM, HDA_PARM_NODECOUNT);
    
    return ((mNodes & 0x0000FF) >>  0) + 1;
}

UInt8 IntelHDA::getStartingNode()
{
    if (mNodes == -1)
        mNodes = this->sendCommand(1, HDA_VERB_GET_PARAM, HDA_PARM_NODECOUNT);
    
    return (mNodes & 0xFF0000) >> 16;
}

UInt32 IntelHDA::sendCommand(UInt32 nodeId, UInt32 verb, UInt8 payload)
{
    DEBUG_LOG("IntelHDA::SendCommand: verb 0x%06x, payload 0x%02x.\n", verb, payload);
    return this->sendCommand((nodeId & 0xFF) << 20 | (verb & 0xFFF) << 8 | payload);
}

UInt32 IntelHDA::sendCommand(UInt32 nodeId, UInt32 verb, UInt16 payload)
{
    DEBUG_LOG("IntelHDA::SendCommand: verb 0x%02x, payload 0x%04x.\n", verb, payload);
    return this->sendCommand((nodeId & 0xFF) << 20 | (verb & 0xF) << 16 | payload);
}

UInt32 IntelHDA::sendCommand(UInt32 command)
{
    UInt32 fullCommand = (mCodecAddress & 0xF) << 28 | (command & 0x0FFFFFFF);
    
    if (mDeviceMemory == NULL)
        return -1;
    
    DEBUG_LOG("IntelHDA::SendCommand: (w) --> 0x%08x\n", fullCommand);
  
    UInt32 response = -1;
    
    switch (mCommandMode)
    {
        case PIO:
            response = this->executePIO(fullCommand);
            break;
        case DMA:
            IOLog("IntelHDA: Unsupported command mode DMA requested.\n");
            response = -1;
            break;
        default:
            response = -1;
            break;
    }
    
    DEBUG_LOG("IntelHDA::SendCommand: (r) <-- 0x%08x\n", response);
    
    return response;
}

UInt32 IntelHDA::executePIO(UInt32 command)
{
    UInt16 status;
    
    status = 0x1; // Busy status
    
    for (int i = 0; i < 1000; i++)
    {
        mDeviceMemory->readBytes(HDA_REG_ICS, &status, sizeof(status));
        
        if (!HDA_ICS_IS_BUSY(status))
            break;
        
        ::IODelay(100);
    }
    
    // HDA controller was not ready to receive PIO commands
    if (HDA_ICS_IS_BUSY(status))
    {
        DEBUG_LOG("IntelHDA::ExecutePIO timed out waiting for ICS readiness.\n");
        return -1;
    }
    
    //DEBUG_LOG("IntelHDA::ExecutePIO ICB bit clear.\n");
    
    // Queue the verb for the HDA controller
    mDeviceMemory->writeBytes(HDA_REG_ICOI, &command, sizeof(command));
    
    status = 0x1; // Busy status
    mDeviceMemory->writeBytes(HDA_REG_ICS, &status, sizeof(status));
    
    //DEBUG_LOG("IntelHDA::ExecutePIO Wrote verb and set ICB bit.\n");
    
    // Wait for HDA controller to return with a response
    for (int i = 0; i < 1000; i++)
    {
        mDeviceMemory->readBytes(HDA_REG_ICS, &status, sizeof(status));
        
        if (!HDA_ICS_IS_BUSY(status))
            break;
        
        ::IODelay(100);
    }
    
    // Store the result validity while IRV is cleared
    bool validResult = HDA_ICS_IS_VALID(status);
    
    UInt32 response;
    
    if (validResult)
        mDeviceMemory->readBytes(HDA_REG_IRII, &response, sizeof(response));
    
    // Reset IRV
    status = 0x02; // Valid, Non-busy status
    mDeviceMemory->writeBytes(HDA_REG_ICS, &status, sizeof(status));
    
    if (!validResult)
    {
        DEBUG_LOG("IntelHDA::ExecutePIO Invalid result received.\n");
        return -1;
    }
    
    return response;
}