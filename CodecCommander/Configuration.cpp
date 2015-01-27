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

#include "Configuration.h"

// Constants for Configuration
#define kDefault                    "Default"
#define kPerformReset               "Perform Reset"

// Constants for Intel HDA
#define kCodecAddressNumber         "Codec Address Number"

// Constants for EAPD command verb sending
#define kUpdateNodes                "Update Nodes"
#define kSendDelay                  "Send Delay"

// Workloop required and Workloop timer aka update interval, ms
#define kCheckInfinitely            "Check Infinitely"
#define kCheckInterval              "Check Interval"

// Constants for custom commands
#define kCustomCommands             "Custom Commands"
#define kCustomCommand              "Command"
#define kCommandOnInit              "On Init"
#define kCommandOnSleep             "On Sleep"
#define kCommandOnWake              "On Wake"

Configuration::Configuration(OSObject* codecProfile, UInt32 codecVendorId)
{
    mCustomCommands = OSArray::withCapacity(0);
    
    OSDictionary* list = OSDynamicCast(OSDictionary, codecProfile);
    
    if (list == NULL)
        return;
    
    // Retrieve platform profile configuration

    OSDictionary* config = Configuration::loadConfiguration(list, codecVendorId);
     
    // Get delay for sending the verb
    if (OSNumber* num = OSDynamicCast(OSNumber, config->getObject(kSendDelay)))
        mSendDelay = num->unsigned16BitValue();
    else
        // Default to 3000
        mSendDelay = 3000;

    // Determine if perform reset is requested (Defaults to true)
    if (OSBoolean* bl = OSDynamicCast(OSBoolean, config->getObject(kPerformReset)))
        mPerformReset = bl->getValue();
    else
        // Default to true
        mPerformReset = true;
    
    if (OSBoolean* bl = OSDynamicCast(OSBoolean, config->getObject(kUpdateNodes)))
        mUpdateNodes = bl->getValue();
    else
        // Default to true
        mUpdateNodes = true;
    
    // Determine if infinite check is needed (for 10.9 and up)
    if (OSBoolean* bl = OSDynamicCast(OSBoolean, config->getObject(kCheckInfinitely)))
    {
        mCheckInfinite = (int)bl->getValue();
        
        if (mCheckInfinite)
        {
            // What is the update interval
            if (OSNumber* num = OSDynamicCast(OSNumber, config->getObject(kCheckInterval)))
                mUpdateInterval = num->unsigned16BitValue();
        }
    }
    else
        // Default to false
        mCheckInfinite = false;
    
    if (OSArray* list = OSDynamicCast(OSArray, config->getObject(kCustomCommands)))
    {
        OSCollectionIterator* iterator = OSCollectionIterator::withCollection(list);
        
        while (OSDictionary* dict = OSDynamicCast(OSDictionary, iterator->getNextObject()))
        {
            if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kCustomCommand)))
            {
                // Command should be != 0
                if (num->unsigned32BitValue() > 0)
                {
                    CustomCommand customCommand;
                    
                    customCommand.Command = num->unsigned32BitValue();
            
                    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kCommandOnInit)))
                        customCommand.OnInit = bl->getValue();
                    else
                        customCommand.OnInit = false;
                    
                    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kCommandOnSleep)))
                        customCommand.OnSleep = bl->getValue();
                    else
                        customCommand.OnSleep = false;
            
                    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kCommandOnWake)))
                        customCommand.OnWake = bl->getValue();
                    else
                        customCommand.OnWake = false;
                    
                    mCustomCommands->setObject(OSData::withBytes(&customCommand, sizeof(customCommand)));
                }
            }
        }
        
        OSSafeRelease(iterator);
    }
    
    OSSafeRelease(config);
    
    // Dump parsed configuration
    DEBUG_LOG("CodecCommander::Configuration\n");
    DEBUG_LOG("...Check Infinite:\t%s\n", mCheckInfinite ? "true" : "false");
    DEBUG_LOG("...Perform Reset:\t%s\n", mPerformReset ? "true" : "false");
    DEBUG_LOG("...Send Delay:\t\t%d\n", mSendDelay);
    DEBUG_LOG("...Update Interval:\t%d\n", mUpdateInterval);
    DEBUG_LOG("...Update Nodes:\t\t%s\n", mUpdateNodes ? "true" : "false");   
}

Configuration::~Configuration()
{
    OSSafeReleaseNULL(mCustomCommands);
}

OSDictionary* Configuration::loadConfiguration(OSDictionary* list, UInt32 codecVendorId)
{
    char buffer[16];
    
    if (!list)
        return NULL;
    
    OSDictionary* result = NULL;
    OSDictionary* defaultNode = OSDynamicCast(OSDictionary, list->getObject(kDefault));
    
    snprintf(buffer, sizeof(buffer), "0x%08x", codecVendorId);
    OSString* codecString = OSString::withCString(buffer);
    
    OSDictionary* codecNode = OSDynamicCast(OSDictionary, list->getObject(codecString));
    
    if (codecNode != NULL)
        DEBUG_LOG("CodecCommander: Located configuration for codec %s.\n", buffer);
    
    OSSafeRelease(codecString);
  
    if (defaultNode)
    {
        // have default node, result is merged with platform node
        result = OSDictionary::withDictionary(defaultNode);
        
        if (result && codecNode)
            result->merge(codecNode);
    }
    else if (codecNode)
        // no default node, try to use just platform node
        result = OSDictionary::withDictionary(codecNode);
   
    return result;
}

/********************************************
 * Configuration Properties
 ********************************************/
bool Configuration::getUpdateNodes()
{
    return mUpdateNodes;
}

bool Configuration::getPerformReset()
{
    return mPerformReset;
}

UInt16 Configuration::getSendDelay()
{
    return mSendDelay;
}

bool Configuration::getCheckInfinite()
{
    return mCheckInfinite;
}

UInt16 Configuration::getInterval()
{
    return mUpdateInterval;
}

OSArray* Configuration::getCustomCommands()
{
    return mCustomCommands;
}

