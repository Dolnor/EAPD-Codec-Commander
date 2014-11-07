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

IntelHDA::IntelHDA(IORegistryEntry *ioRegistryEntry, char codecAddress)
{
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
    delete [] mPinCapabilities;
    
    if (mDeviceMemory)
        mDeviceMemory->release();
    
    if (mService)
        mService->release();
}

unsigned short IntelHDA::getVendorId()
{
    if (mVendor == -1)
        mVendor = this->SendCommand(0, HDA_VERB_GET_PARAM, HDA_PARM_VENDOR);
    
    return mVendor >> 16;
}

unsigned short IntelHDA::getDeviceId()
{
    if (mVendor == -1)
        mVendor = this->SendCommand(0, HDA_VERB_GET_PARAM, HDA_PARM_VENDOR);
    
    return mVendor & 0xFFFF;
}

unsigned char IntelHDA::getTotalNodes()
{
    if (mNodes == -1)
        mNodes = this->SendCommand(1, HDA_VERB_GET_PARAM, HDA_PARM_NODECOUNT);
    
    return ((mNodes & 0x0000FF) >>  0) + 1;
}

unsigned char IntelHDA::getStartingNode()
{
    if (mNodes == -1)
        mNodes = this->SendCommand(1, HDA_VERB_GET_PARAM, HDA_PARM_NODECOUNT);
    
    return (mNodes & 0xFF0000) >> 16;
}

unsigned int IntelHDA::SendCommand(unsigned int nodeId, unsigned int verb, unsigned char payload)
{
    DEBUG_LOG("IntelHDA::SendCommand: 12-bit verb, 4-bit payload ");
    return this->SendCommand((mCodecAddress << 28) | (nodeId << 20) | (verb << 8) | payload);
}

unsigned int IntelHDA::SendCommand(unsigned int nodeId, unsigned int verb, unsigned short payload)
{
    DEBUG_LOG("IntelHDA::SendCommand: 4-bit verb, 16-bit payload ");
    return this->SendCommand((mCodecAddress << 28) | (nodeId << 20) | (verb << 12) | payload);
}

unsigned int IntelHDA::SendCommand(unsigned int command)
{
    if (mDeviceMemory == NULL)
        return -1;
    
    DEBUG_LOG("0x%08x\n", command);
    
    return this->ExecutePIO(command);
    
    switch (mCommandMode)
    {
        PIO:
            return this->ExecutePIO(command);
        default:
            return -1;
    }
}

unsigned int IntelHDA::ExecutePIO(unsigned int command)
{
    HDA_ICS hdaICS;
    
    hdaICS.CommandBusy = 1;
    
    for (int i = 0; i < 1000; i++)
    {
        mDeviceMemory->readBytes(HDA_REG_ICS, &hdaICS, sizeof(HDA_ICS));
        
        if (hdaICS.CommandBusy == 0)
            break;
        
        ::IODelay(100);
    }
    
    // HDA controller was not ready to receive PIO commands
    if (hdaICS.CommandBusy == 1)
    {
        DEBUG_LOG("IntelHDA::ExecutePIO timed out waiting for ICS readiness.\n");
        return -1;
    }
    
    DEBUG_LOG("IntelHDA::ExecutePIO ICB bit clear.\n");
    
    // Queue the verb for the HDA controller
    mDeviceMemory->writeBytes(HDA_REG_ICOI, &command, sizeof(command));
    
    memset(&hdaICS, 0, sizeof(HDA_ICS));
    hdaICS.CommandBusy = 1;
    mDeviceMemory->writeBytes(HDA_REG_ICS, &hdaICS, sizeof(HDA_ICS));
    
    DEBUG_LOG("IntelHDA::ExecutePIO Wrote verb and set ICB bit.\n");
    
    // Wait for HDA controller to return with a response
    for (int i = 0; i < 1000; i++)
    {
        mDeviceMemory->readBytes(HDA_REG_ICS, &hdaICS, sizeof(HDA_ICS));
        
        if (hdaICS.CommandBusy == 0)
            break;
        
        ::IODelay(100);
    }
    
    // Store the result validity while IRV is cleared
    bool validResult = (hdaICS.ResultValid == 1);
    
    unsigned int response;
    
    if (validResult)
        mDeviceMemory->readBytes(HDA_REG_IRII, &response, sizeof(response));
    
    // Reset IRV
    memset(&hdaICS, 0, sizeof(HDA_ICS));
    hdaICS.ResultValid = 1;
    mDeviceMemory->writeBytes(HDA_REG_ICS, &hdaICS, sizeof(HDA_ICS));
    
    if (!validResult)
    {
        DEBUG_LOG("IntelHDA::ExecutePIO Invalid result received.\n");
        return -1;
    }
    
    DEBUG_LOG("IntelHDA::ExecutePIO Command response 0x%08x\n", response);
    
    return response;
}