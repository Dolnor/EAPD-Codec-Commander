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
#define kCodecId                    "Codec Id"

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

static OSDictionary* locateConfiguration(OSDictionary* profiles, UInt32 codecVendorId)
{
    OSCollectionIterator* iterateProfiles = OSCollectionIterator::withCollection(profiles);
    
    OSSymbol* profileKey;
    
    while ((profileKey = OSDynamicCast(OSSymbol, iterateProfiles->getNextObject())))
    {
        OSDictionary* profile = OSDynamicCast(OSDictionary, profiles->getObject(profileKey));
        
        if (profile)
        {
            OSArray* codecIds = OSDynamicCast(OSArray, profile->getObject(kCodecId));
            
            if (codecIds)
            {
                OSCollectionIterator* iterateCodecs = OSCollectionIterator::withCollection(codecIds);
                
                OSNumber* codecId;
                
                while ((codecId = OSDynamicCast(OSNumber, iterateCodecs->getNextObject())))
                {
                    if (codecId->unsigned32BitValue() == codecVendorId)
                    {
                        DebugLog("Located configuration for codec: %s (0x%08x).\n",
                                  profileKey->getCStringNoCopy(), codecVendorId);
                        
                        OSSafeRelease(iterateCodecs);
                        OSSafeRelease(iterateProfiles);
                        
                        return profile;
                    }
                }
                
                OSSafeRelease(iterateCodecs);
            }
        }
    }
    
    OSSafeRelease(iterateProfiles);
}

static OSDictionary* loadConfiguration(OSDictionary* profiles, UInt32 codecVendorId)
{
    OSDictionary* defaultProfile = OSDynamicCast(OSDictionary, profiles->getObject(kDefault));
    
    OSDictionary* codecProfile = locateConfiguration(profiles, codecVendorId);

    if (defaultProfile)
    {
        // have default node, result is merged with platform node
        OSDictionary* result = OSDictionary::withDictionary(defaultProfile);
        
        if (result && codecProfile)
        {
            result->merge(codecProfile);
        }
        
        if (result)
        {
            return result;
        }
    }
    
    if (codecProfile)
    {
        // no default node, try to use just the codec profile
        return OSDictionary::withDictionary(codecProfile);
    }
    
    return OSDictionary::withCapacity(0);
}

Configuration::Configuration(OSObject* codecProfiles, UInt32 codecVendorId)
{
    mCustomCommands = OSArray::withCapacity(0);
    
    OSDictionary* list = OSDynamicCast(OSDictionary, codecProfiles);

    if (!list)
        return;

    // Retrieve platform profile configuration
    OSDictionary* config = loadConfiguration(list, codecVendorId);
    
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
    DebugLog("Configuration\n");
    DebugLog("...Check Infinite:\t%s\n", mCheckInfinite ? "true" : "false");
    DebugLog("...Perform Reset:\t%s\n", mPerformReset ? "true" : "false");
    DebugLog("...Send Delay:\t\t%d\n", mSendDelay);
    DebugLog("...Update Interval:\t%d\n", mUpdateInterval);
    DebugLog("...Update Nodes:\t%s\n", mUpdateNodes ? "true" : "false");
}

Configuration::~Configuration()
{
    OSSafeReleaseNULL(mCustomCommands);
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

