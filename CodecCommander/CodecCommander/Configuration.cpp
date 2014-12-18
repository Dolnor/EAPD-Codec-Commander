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

Configuration::Configuration(OSObject* platformProfile)
{
    OSDictionary* list = OSDynamicCast(OSDictionary, platformProfile);
    
    if (list == NULL)
        return;
    
    // Retrieve platform profile configuration

    OSDictionary* config = Configuration::loadConfiguration(list);
   
    // Get codec address number
    if (OSNumber* num = OSDynamicCast(OSNumber, config->getObject(kCodecAddressNumber)))
        mCodecNumber = num->unsigned8BitValue();
    else
        // Default to codec number 0
        mCodecNumber = 0;
   
    // Get delay for sending the verb
    if (OSNumber* num = OSDynamicCast(OSNumber, config->getObject(kSendDelay)))
        mSendDelay = num->unsigned16BitValue();
    else
        // Default to 3000
        mSendDelay = 3000;
    
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
        
        iterator->reset();
        int k = 0;
        
        while (OSDictionary* dict = OSDynamicCast(OSDictionary, iterator->getNextObject()))
        {
            if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kCustomCommand)))
            {
                // Command should be != 0
                if (num->unsigned32BitValue() > 0)
                {
                    mCustomCommands[k].Command = num->unsigned32BitValue();
            
                    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kCommandOnInit)))
                        mCustomCommands[k].OnInit = bl->getValue();
                    else
                        mCustomCommands[k].OnInit = false;
                    
                    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kCommandOnSleep)))
                        mCustomCommands[k].OnSleep = bl->getValue();
                    else
                        mCustomCommands[k].OnSleep = false;
            
                    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kCommandOnWake)))
                        mCustomCommands[k].OnWake = bl->getValue();
                    else
                        mCustomCommands[k].OnWake = false;
                
                    k++;
                }
            }
        }
        
        OSSafeRelease(iterator);
    }
    
    OSSafeRelease(config);
    
    // Dump parsed configuration
    DEBUG_LOG("CodecCommander::Configuration\n");
    DEBUG_LOG("...Codec Number:\t%d\n", mCodecNumber);
    DEBUG_LOG("...Send Delay:\t\t%d\n", mSendDelay);
    DEBUG_LOG("...Check Infinite:\t%s\n", mCheckInfinite ? "true" : "false");
    DEBUG_LOG("...Update Interval:\t%d\n", mUpdateInterval);
}

OSDictionary* Configuration::loadConfiguration(OSDictionary* list)
{
    if (!list)
        return NULL;
    
    OSDictionary* result = NULL;
    OSDictionary* defaultNode = OSDynamicCast(OSDictionary, list->getObject(kDefault));
    
    OSString* platformManufacturer = getPlatformManufacturer();
    OSString* platformProduct = getPlatformProduct();
    
    DEBUG_LOG("CodecCommander::Platform\n");
    DEBUG_LOG("...Manufacturer:\t%s\n", platformManufacturer != NULL ? platformManufacturer->getCStringNoCopy() : "Unknown");
    DEBUG_LOG("...Product:\t\t%s\n", platformProduct != NULL ? platformProduct->getCStringNoCopy() : "Unknown");
    
    OSDictionary* platformNode = Configuration::getPlatformNode(list, platformManufacturer, platformProduct);
    
    OSSafeRelease(platformManufacturer);
    OSSafeRelease(platformProduct);
  
    if (defaultNode)
    {
        // have default node, result is merge with platform node
        result = OSDictionary::withDictionary(defaultNode);
        
        if (result && platformNode)
            result->merge(platformNode);
    }
    else if (platformNode)
        // no default node, try to use just platform node
        result = OSDictionary::withDictionary(platformNode);
   
    return result;
}

/********************************************
 * Configuration Properties
 ********************************************/
UInt8 Configuration::getCodecNumber()
{
    return mCodecNumber;
}

bool Configuration::getUpdateNodes()
{
    return mUpdateNodes;
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

CustomCommand* Configuration::getCustomCommands()
{
    return mCustomCommands;
}

/******************************************************************************
 * Methods for getting configuration dictionary, courtesy of RehabMan
 ******************************************************************************/
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Simplify data from Clover's DMI readings and use it for profile make and model
 * Courtesy of kozlek (HWSensors project)
 * https://github.com/kozlek/HWSensors
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

OSString* Configuration::getManufacturerNameFromOEMName(OSString *name)
{
    if (!name)
        return NULL;
    
    OSString *manufacturer = NULL;
    
    if (name->isEqualTo("ASUSTeK Computer INC.") ||
        name->isEqualTo("ASUSTeK COMPUTER INC.")) manufacturer = OSString::withCString("ASUS");

    if (name->isEqualTo("Dell Inc.")) manufacturer = OSString::withCString("DELL");

    if (name->isEqualTo("Gigabyte Technology Co., Ltd.")) manufacturer = OSString::withCString("Gigabyte");

    if (name->isEqualTo("FUJITSU") ||
        name->isEqualTo("FUJITSU SIEMENS")) manufacturer = OSString::withCString("FUJITSU");

    if (name->isEqualTo("Hewlett-Packard")) manufacturer = OSString::withCString("HP");

    if (name->isEqualTo("IBM")) manufacturer = OSString::withCString("IBM");

    if (name->isEqualTo("Intel") ||
        name->isEqualTo("Intel Corp.") ||
        name->isEqualTo("Intel Corporation") ||
        name->isEqualTo("INTEL Corporation")) manufacturer = OSString::withCString("Intel");

    if (name->isEqualTo("Lenovo") || name->isEqualTo("LENOVO")) manufacturer = OSString::withCString("Lenovo");

    if (name->isEqualTo("Micro-Star International") ||
        name->isEqualTo("MICRO-STAR INTERNATIONAL CO., LTD") ||
        name->isEqualTo("MICRO-STAR INTERNATIONAL CO.,LTD") ||
        name->isEqualTo("MSI")) manufacturer = OSString::withCString("MSI");
    
    if (!manufacturer && !name->isEqualTo("To be filled by O.E.M."))
        manufacturer = OSString::withString(name);
    
    return manufacturer;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Obtain information for make and model to match against config in Info.plist
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
OSString* Configuration::getPlatformManufacturer()
{
    // Try to get data from Clover first
    if (IORegistryEntry* platformNode = IORegistryEntry::fromPath("/efi/platform", gIODTPlane))
    {
        OSString *vendor = NULL;
        OSString *manufacturer = NULL;
        
        if (OSData *data = OSDynamicCast(OSData, platformNode->getProperty("OEMVendor")))
        {
            vendor = OSString::withCString((char*)data->getBytesNoCopy());
            manufacturer = getManufacturerNameFromOEMName(vendor);
        }
        
        OSSafeRelease(vendor);
        OSSafeRelease(platformNode);
        
        if (manufacturer)
            return manufacturer;
    }
    
    // If not, then try from RehabMan OEM string on PS2K (useful chameleon/chimera)
    if (IORegistryEntry* ps2KeyboardDevice = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2K"))
    {
        OSString *vendor = NULL;
        OSString *manufacturer = NULL;
        
        vendor = OSDynamicCast(OSString, ps2KeyboardDevice->getProperty("RM,oem-id"));
        manufacturer = getManufacturerNameFromOEMName(vendor);
        
        OSSafeRelease(vendor);
        OSSafeRelease(ps2KeyboardDevice);
        
        if (manufacturer)
            return manufacturer;
    }
    
    return NULL;
}

OSString* Configuration::getPlatformProduct()
{
    // try to get data from Clover first
    if (IORegistryEntry* platformNode = IORegistryEntry::fromPath("/efi/platform", gIODTPlane))
    {
        OSString *product = NULL;
        
        if (OSData *data = OSDynamicCast(OSData, platformNode->getProperty("OEMBoard")))
            product = OSString::withCString((char*)data->getBytesNoCopy());
        
        OSSafeRelease(platformNode);
        
        if (product)
            return product;
    }
    
    // then from PS2K
    if (IORegistryEntry* ps2KeyboardDevice = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2K"))
    {
        OSString *product = OSDynamicCast(OSString, ps2KeyboardDevice->getProperty("RM,oem-table-id"));
        
        OSSafeRelease(ps2KeyboardDevice);
        
        if (product)
            return product;
    }
    
    return NULL;
}

OSDictionary* Configuration::getPlatformNode(OSDictionary* list, OSString *platformManufacturer, OSString *platformProduct)
{
    OSDictionary *configuration = NULL;

    // Try and retrieve
    // 1) Manufacturer / Product
    // 2) Manufacturer / Default
    // 3) Product
    
    if (platformManufacturer && platformProduct)
    {
        if (OSDictionary *manufacturerNode = OSDynamicCast(OSDictionary, list->getObject(platformManufacturer)))
            if (!(configuration = OSDynamicCast(OSDictionary, manufacturerNode->getObject(platformProduct))))
                configuration = OSDynamicCast(OSDictionary, manufacturerNode->getObject(kDefault));
    }

    if (platformProduct)
    {
        if (!configuration && !(configuration = OSDynamicCast(OSDictionary, list->getObject(platformProduct))))
            configuration = OSDynamicCast(OSDictionary, list->getObject(kDefault));
    }
    
    return configuration;
}

