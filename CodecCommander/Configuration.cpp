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
#define kPerformResetOnEAPDFail     "Perform Reset on EAPD Fail"
#define kCodecId                    "Codec Id"
#define kDisable                    "Disable"

// Constants for EAPD command verb sending
#define kUpdateNodes                "Update Nodes"
#define kSleepNodes                 "Sleep Nodes"
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

// Parsing for configuration

UInt32 Configuration::parseInteger(const char* str)
{
    UInt32 result = 0;
    while (*str == ' ') ++str;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
        str += 2;
        while (*str)
        {
            result <<= 4;
            if (*str >= '0' && *str <= '9')
                result |= *str - '0';
            else if (*str >= 'A' && *str <= 'F')
                result |= *str - 'A' + 10;
            else if (*str >= 'a' && *str <= 'f')
                result |= *str - 'a' + 10;
            else
                return 0;
            ++str;
        }
    }
    else
    {
        while (*str)
        {
            result *= 10;
            if (*str >= '0' && *str <= '9')
                result += *str - '0';
            else
                return 0;
            ++str;
        }
    }
    return result;
}

bool Configuration::getBoolValue(OSDictionary *dict, const char *key, bool defValue)
{
    bool result = defValue;
    if (dict)
    {
        if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(key)))
            result = bl->getValue();
    }
    return result;
}

UInt32 Configuration::getIntegerValue(OSDictionary *dict, const char *key, UInt32 defValue)
{
    UInt32 result = defValue;
    if (dict)
        result = getIntegerValue(dict->getObject(key), defValue);
    return result;
}

UInt32 Configuration::getIntegerValue(OSObject *obj, UInt32 defValue)
{
    UInt32 result = defValue;
    if (OSNumber* num = OSDynamicCast(OSNumber, obj))
        result = num->unsigned32BitValue();
    else if (OSString* str = OSDynamicCast(OSString, obj))
        result = parseInteger(str->getCStringNoCopy());
    return result;
}

OSDictionary* Configuration::locateConfiguration(OSDictionary* profiles, UInt32 codecVendorId, UInt32 subsystemId, UInt32 pciSubId)
{
    UInt16 vendor = codecVendorId >> 16;
    UInt16 codec = codecVendorId & 0xFFFF;

    // check vendor_codec_hda_subsystem first
    char codecLookup[sizeof("vvvv_cccc_pci_xxxxdddd")]; // can also be vvvv_cccc_hda_xxxxdddd
    snprintf(codecLookup, sizeof(codecLookup), "%04x_%04x_hda_%08x", vendor, codec, subsystemId);
    OSObject* obj = profiles->getObject(codecLookup);
    if (!obj)
    {
        // check vendor_codec_pci_subid next
        snprintf(codecLookup, sizeof(codecLookup), "%04x_%04x_pci_%08x", vendor, codec, pciSubId);
        obj = profiles->getObject(codecLookup);
        if (!obj)
        {
            // check vendor_codec next
            snprintf(codecLookup, sizeof(codecLookup), "%04x_%04x", vendor, codec);
            obj = profiles->getObject(codecLookup);
            if (!obj)
            {
                // not found, check for vendor override (used for Intel HDMI)
                snprintf(codecLookup, sizeof(codecLookup), "%04x", vendor);
                obj = profiles->getObject(codecLookup);
            }
        }
    }

    // look up actual dictionary (can be string redirect)
    OSDictionary* dict;
    if (OSString* str = OSDynamicCast(OSString, obj))
        dict = OSDynamicCast(OSDictionary, profiles->getObject(str));
    else
        dict = OSDynamicCast(OSDictionary, obj);
    
    return dict;
}

OSDictionary* Configuration::loadConfiguration(OSDictionary* profiles, UInt32 codecVendorId, UInt32 subsystemId, UInt32 pciSubId)
{
    OSDictionary* defaultProfile = NULL;
    OSDictionary* codecProfile = NULL;
    if (profiles)
    {
        defaultProfile = OSDynamicCast(OSDictionary, profiles->getObject(kDefault));
        codecProfile = locateConfiguration(profiles, codecVendorId, subsystemId, pciSubId);
    }
    OSDictionary* result = NULL;

    if (defaultProfile)
    {
        // have default node, result is merged with platform node
        result = OSDictionary::withDictionary(defaultProfile);
        if (result && codecProfile)
            result->merge(codecProfile);
    }

    if (!result)
    {
        if (codecProfile)
            // no default node, try to use just the codec profile
            result = OSDictionary::withDictionary(codecProfile);
        if (!result)
            // empty dictionary in case of errors/memory problems
            result = OSDictionary::withCapacity(0);
    }

    return result;
}

Configuration::Configuration(OSObject* codecProfiles, UInt32 codecVendorId, UInt32 hdaSubsystemId, UInt32 pciSubId)
{
    OSDictionary* list = OSDynamicCast(OSDictionary, codecProfiles);

    // Retrieve platform profile configuration
    OSDictionary* config = loadConfiguration(list, codecVendorId, hdaSubsystemId, pciSubId);
#ifdef DEBUG
    mConfig = config;
    if (mConfig)
        mConfig->retain();
#endif

    mCustomCommands = OSArray::withCapacity(0);
    if (!mCustomCommands)
    {
        OSSafeRelease(config);
        return;
    }

    // if Disable is set in the profile, no more config is gathered, start will fail
    mDisable = getBoolValue(config, kDisable, false);
    if (mDisable)
    {
        OSSafeRelease(config);
        return;
    }

    // Get delay for sending the verb
    mSendDelay = getIntegerValue(config, kSendDelay, 3000);

    // Determine if perform reset is requested (Defaults to true)
    mPerformReset = getBoolValue(config, kPerformReset, true);

    // Determine if perform reset is requested (Defaults to true)
    mPerformResetOnEAPDFail = getBoolValue(config, kPerformResetOnEAPDFail, true);

    // Determine if update to EAPD nodes is requested (Defaults to true)
    mUpdateNodes = getBoolValue(config, kUpdateNodes, true);
    mSleepNodes = getBoolValue(config, kSleepNodes, true);

    // Determine if infinite check is needed (for 10.9 and up)
    mCheckInfinite = getBoolValue(config, kCheckInfinitely, false);
    mUpdateInterval = getIntegerValue(config, kCheckInterval, 1000);

    // Parse custom commands
    if (OSArray* list = OSDynamicCast(OSArray, config->getObject(kCustomCommands)))
    {
        OSCollectionIterator* iterator = OSCollectionIterator::withCollection(list);
        if (!iterator) return;
        while (OSDictionary* dict = OSDynamicCast(OSDictionary, iterator->getNextObject()))
        {
            OSObject* obj = dict->getObject(kCustomCommand);
            OSData* commandData = NULL;
            CustomCommand* customCommand;

            if (UInt32 commandBits = getIntegerValue(obj, 0))
            {
                commandData = OSData::withCapacity(sizeof(CustomCommand)+sizeof(UInt32));
                commandData->appendByte(0, commandData->getCapacity());
                customCommand = (CustomCommand*)commandData->getBytesNoCopy();
                customCommand->CommandCount = 1;
                customCommand->Commands[0] = commandBits;
            }
            else if (OSData* data = OSDynamicCast(OSData, obj))
            {
                unsigned length = data->getLength();
                commandData = OSData::withCapacity(sizeof(CustomCommand)+length);
                commandData->appendByte(0, commandData->getCapacity());
                customCommand = (CustomCommand*)commandData->getBytesNoCopy();
                customCommand->CommandCount = length / sizeof(customCommand->Commands[0]);
                // byte reverse here, so the author of Info.pist doesn't have to...
                UInt8* bytes = (UInt8*)data->getBytesNoCopy();
                for (int i = 0; i < customCommand->CommandCount; i++)
                {
                    customCommand->Commands[i] = bytes[0]<<24 | bytes[1]<<16 | bytes[2]<<8 | bytes[3];
                    bytes += sizeof(UInt32);
                }
            }
            if (commandData)
            {
                customCommand->OnInit = getBoolValue(dict, kCommandOnInit, false);
                customCommand->OnSleep = getBoolValue(dict, kCommandOnSleep, false);
                customCommand->OnWake = getBoolValue(dict, kCommandOnWake, false);
                mCustomCommands->setObject(commandData);
                commandData->release();
            }
        }
        iterator->release();
    }

    OSSafeRelease(config);

    // Dump parsed configuration
    DebugLog("Configuration\n");
    DebugLog("...Check Infinite: %s\n", mCheckInfinite ? "true" : "false");
    DebugLog("...Perform Reset: %s\n", mPerformReset ? "true" : "false");
    DebugLog("...Perform Reset on EAPD Fail: %s\n", mPerformResetOnEAPDFail ? "true" : "false");
    DebugLog("...Send Delay: %d\n", mSendDelay);
    DebugLog("...Update Interval: %d\n", mUpdateInterval);
    DebugLog("...Update Nodes: %s\n", mUpdateNodes ? "true" : "false");
    DebugLog("...Sleep Nodes: %s\n", mSleepNodes ? "true" : "false");

#ifdef DEBUG
    if (OSCollectionIterator* iterator = OSCollectionIterator::withCollection(mCustomCommands))
    {
        while (OSData* data = OSDynamicCast(OSData, iterator->getNextObject()))
        {
            CustomCommand* customCommand = (CustomCommand*)data->getBytesNoCopy();
            DebugLog("Custom Command\n");
            if (customCommand->CommandCount == 1)
                DebugLog("...Command: 0x%08x\n", customCommand->Commands[0]);
            if (customCommand->CommandCount == 2)
                DebugLog("...Commands(%d): 0x%08x 0x%08x\n", customCommand->CommandCount, customCommand->Commands[0], customCommand->Commands[1]);
            if (customCommand->CommandCount == 3)
                DebugLog("...Commands(%d): 0x%08x 0x%08x 0x%08x\n", customCommand->CommandCount, customCommand->Commands[0], customCommand->Commands[1], customCommand->Commands[2]);
            if (customCommand->CommandCount == 3)
                DebugLog("...Commands(%d): 0x%08x 0x%08x 0x%08x ...\n", customCommand->CommandCount, customCommand->Commands[0], customCommand->Commands[1], customCommand->Commands[2]);
            DebugLog("...OnInit: %s\n", customCommand->OnInit ? "true" : "false");
            DebugLog("...OnWake: %s\n", customCommand->OnWake ? "true" : "false");
            DebugLog("...OnSleep: %s\n", customCommand->OnSleep ? "true" : "false");
        }
        iterator->release();
    }
#endif
}

Configuration::~Configuration()
{
#ifdef DEBUG
    OSSafeRelease(mConfig);
#endif
    OSSafeRelease(mCustomCommands);
}

