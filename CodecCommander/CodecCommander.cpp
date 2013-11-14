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

#include <IOKit/IOCommandGate.h>
#include "CodecCommander.h"

// Constats for Platform Profile

#define kPlatformProfile            "Platform Profile"
#define kDefault                    "Default"

// Constants for EAPD comman verb sending

#define kHDACodecAddress            "HDEF Codec Address"
#define kUpdateSpeakerNodeNumber    "Update Speaker Node"
#define kUpdateHeadphoneNodeNumber  "Update Headphone Node"

// Define variables for EAPD state updating

IOMemoryDescriptor *ioreg_;
bool eapdPoweredDown = true; //assume user pinconfig doesn't contain *speacial* codec verb

UInt8  codecNumber, spNodeNumber, hpNodeNumber;
UInt16 status;
UInt32 spCommand, hpCommand;

// Define usable power states

static IOPMPowerState powerStateArray[ kPowerStateCount ] =
{
	{ 1,0,0,0,0,0,0,0,0,0,0,0 },
	{ 1,IOPMDeviceUsable,IOPMPowerOn,IOPMPowerOn,0,0,0,0,0,0,0,0 }
};

OSDefineMetaClassAndStructors(org_tw_CodecCommander, IOService)

/******************************************************************************
 * CodecCommander::init - parse kernel extension Info.plist
 ******************************************************************************/
bool CodecCommander::init(OSDictionary *dict)
{
    DEBUG_LOG("CodecCommander::init: Initializing\n");
    
    if (!super::init(dict))
        return false;
    // get configuration for respective platform
    OSDictionary* list = OSDynamicCast(OSDictionary, dict->getObject(kPlatformProfile));
    OSDictionary* config = CodecCommander::makeConfigurationNode(list);
    
    // set platform configuration
    setParamPropertiesGated(config);
    OSSafeRelease(config);
    
    // set codec address and node number for EAPD state update
    spCommand = (codecNumber << 28) | (spNodeNumber << 20) | 0x70c02;
    hpCommand = (codecNumber << 28) | (hpNodeNumber << 20) | 0x70c02;
    
    return true;
}

/******************************************************************************
 * CodecCommander::probe - check if there is something to attach to
 ******************************************************************************/
IOService *CodecCommander::probe(IOService *provider, SInt32 *score)
{
    DEBUG_LOG("CodecCommander::probe: Probing\n");
    return this;
}

/******************************************************************************
 * CodecCommander::start - start kernel extension and init PM
 ******************************************************************************/
bool CodecCommander::start(IOService *provider)
{
    DEBUG_LOG("CodecCommander::start: Starting\n");
    
    // determine HDEF ACPI device path in IORegistry
    IORegistryEntry *hdaDeviceEntry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/HDEF");
    if(hdaDeviceEntry == NULL) {
        hdaDeviceEntry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PCI0/AppleACPIPCI/HDEF@1B");
    }
    
    if (hdaDeviceEntry != NULL) {
        IOService *service = OSDynamicCast(IOService, hdaDeviceEntry);
        
        // get address field from IODeviceMemory
        if (service != NULL && service->getDeviceMemoryCount() != 0) {
            ioreg_ = service->getDeviceMemoryWithIndex(0);
        }
        hdaDeviceEntry->release();
    }
   
    // init power state management & set state as PowerOn
    PMinit();
    registerPowerDriver(this, powerStateArray, 2);
	provider->joinPMtree(this);
    
	this->registerService(0);
    return true;
}

/******************************************************************************
 * CodecCommander::stop - stop kernel extension
 ******************************************************************************/
void CodecCommander::stop(IOService *provider)
{
	PMstop();
    DEBUG_LOG("CodecCommander::stop: Stopping\n");
    super::stop(provider);
}

/******************************************************************************
 * CodecCommander::setParamPropertiesGated - set variables based on user config
 ******************************************************************************/

void CodecCommander::setParamPropertiesGated(OSDictionary * dict)
{
    if (NULL == dict)
        return;
    
    // Get codec number (codec address)
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kHDACodecAddress)))
    {
        codecNumber = num->unsigned8BitValue();
        setProperty(kHDACodecAddress, codecNumber, 8);
    }
    
    // Get headphone node number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kUpdateHeadphoneNodeNumber)))
    {
        hpNodeNumber= num->unsigned8BitValue();
        setProperty(kUpdateHeadphoneNodeNumber, hpNodeNumber, 8);
    }
    
    // Get speaker node number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kUpdateSpeakerNodeNumber)))
    {
        spNodeNumber = num->unsigned8BitValue();
        setProperty(kUpdateSpeakerNodeNumber, spNodeNumber, 8);
    }
}

/******************************************************************************
* Command handling method for sending codec verb commands to
* Immediate Command Input and Output Registers (offsets 0x60, 0x64 and 0x68),
* based on High Definition Audio Specification Revision 1.0a - June 17,2010
******************************************************************************/

void CodecCommander::handleCommand(UInt32 cmd){
    
    if (ioreg_ == NULL) {
        return;
    }
    
    /* Intel HD Audio Spec suggests:
     
    - Offset 60h: Immediate Command Output Interface - 32 bit register
    Bit 0-15 - Immediate Command Write (ICW): The value written into this register is sent
    out over the link during the next available frame. Software must ensure that the
    ICB bit in the Immediate Command Status register is clear before writing a value
    into this register or undefined behavior will result. Reads from this register will
    always return 0’s.
     
    - Offset 64h: Immediate Response Input Interface - 32 bit register
    Immediate Response Read (IRR): The value in this register latches the last
    response to come in over the link.
     
    - Offset 68h: Immediate Command Status - 16 bit register
    Bit 0 - Immediate Command Busy (ICB): This bit is a 0 when the controller can accept
    an immediate command. Software must wait for this bit to be 0 before writing a
    value in the ICW register.
    
    Bit 1 - Immediate Result Valid (IRV): This bit is set to a 1 by hardware when a new
    response is latched into the IRR register. Software must clear this bit before
    issuing a new command by writing a one to it so that the software may determine
    when a new response has arrived.
     
    */
    
    // write command to ICW field
    ioreg_->writeBytes(0x60, &cmd, sizeof(cmd));
    DEBUG_LOG("CodecCommander: command %04x written to ICW register\n", cmd);
    
    // set ICB as being busy
    status = 1;
    ioreg_->writeBytes(0x68, &status, sizeof(status));
    DEBUG_LOG("CodecCommander: status of ICB field changed to %d\n", status);
    
    // wait for response on Immediate Command Status
    for (int i = 0; i < 1000; i++) {
        ::IODelay(100);
        
        // check IRV for the status of previous write
        DEBUG_LOG("CodecCommander: get status of IRV field\n");
        ioreg_->readBytes(0x68, &status, sizeof(status));
        // we are good if ICW command has latched into IRR
        if (status & 0x2) {
            DEBUG_LOG("CodecCommander: response latched in IRR register, IRV returned valid status %d\n", status);
            goto Success;
        }
    }
        
    DEBUG_LOG("CodecCommander: command failed, IRV returned invalid status %d\n", status);
        
Success:
        
    // clear IRV bit for next command write
    status = 0x2;
    ioreg_->writeBytes(0x68, &status, sizeof(status));
    DEBUG_LOG("CodecCommander: IRV field cleared, ready for next command\n");
}

/******************************************************************************
 * CodecCommander::setPowerState - set active power state
 ******************************************************************************/

IOReturn CodecCommander::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
	if (kPowerStateOff == powerStateOrdinal)
	{
        DEBUG_LOG("CodecCommander::power: is off\n");
        // external amp has powered down
        if (!eapdPoweredDown) {
            eapdPoweredDown=true;
        }
        
	}
	else if (kPowerStateOn == powerStateOrdinal)
	{
        DEBUG_LOG("CodecCommander::power: is on\n");
        // update external amp by sending verb command
        if (eapdPoweredDown) {
            
            // delay sending codec verb command by 100ms, otherwise sometimes it breaks audio
            IOSleep(100);
            if(spNodeNumber)
            {
                handleCommand(spCommand); // SP node only
                if (hpNodeNumber) // both SP/HP nodes
                    handleCommand(hpCommand);
            }
            else // HP node only
                handleCommand(hpCommand);

            // mark amp as active
            eapdPoweredDown = false;
        }
	}
	
	return IOPMAckImplied;
}

/******************************************************************************
 * Methods to obtain Platform Profile for EAPD cmd verb sending configuration
 ******************************************************************************/

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Simplify data from Clover's DMI readings and use it for profile make and model
 * Courtesy of kozlek (HWSensors project)
 * https://github.com/kozlek/HWSensors
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

OSString* getManufacturerNameFromOEMName(OSString *name)
{
    if (!name) {
        return NULL;
    }
    
    OSString *manufacturer = NULL;
    
    if (name->isEqualTo("ASUSTeK Computer INC.") ||
        name->isEqualTo("ASUSTeK COMPUTER INC.")) manufacturer = OSString::withCString("ASUS");
    if (name->isEqualTo("Dell Inc.")) manufacturer = OSString::withCString("DELL");
    if (name->isEqualTo("FUJITSU") ||
        name->isEqualTo("FUJITSU SIEMENS")) manufacturer = OSString::withCString("FUJITSU");
    if (name->isEqualTo("Hewlett-Packard")) manufacturer = OSString::withCString("HP");
    if (name->isEqualTo("IBM")) manufacturer = OSString::withCString("IBM");
    if (name->isEqualTo("Intel") ||
        name->isEqualTo("Intel Corp.") ||
        name->isEqualTo("Intel Corporation")||
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
 * Get make and model for profile from DSDT header OEM ID and Table ID fields
 * Courtesy of RehabMan (VoodooPS2Controller project)
 * https://github.com/RehabMan/OS-X-Voodoo-PS2-Controller
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#define DSDT_SIGNATURE ('D' | 'S'<<8 | 'D'<<16 | 'T'<<24)

struct DSDT_HEADER // DSDT header structure
{
    uint32_t tableSignature;
    uint32_t tableLength;
    uint8_t specCompliance;
    uint8_t checkSum;
    char oemID[6]; // platform make
    char oemTableID[8]; // platform model
    uint32_t oemRevision;
    uint32_t creatorID;
    uint32_t creatorRevision;
};

static const DSDT_HEADER* getDSDT()
{
    IORegistryEntry* reg = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert");
    if (!reg)
        return NULL;
    OSDictionary* dict = OSDynamicCast(OSDictionary, reg->getProperty("ACPI Tables"));
    reg->release();
    if (!dict)
        return NULL;
    OSData* data = OSDynamicCast(OSData, dict->getObject("DSDT"));
    if (!data || data->getLength() < sizeof(DSDT_HEADER))
        return NULL;
    const DSDT_HEADER* pDSDT = (const DSDT_HEADER*)data->getBytesNoCopy();
    if (!pDSDT || data->getLength() < sizeof(DSDT_HEADER) || pDSDT->tableSignature != DSDT_SIGNATURE)
        return NULL;
    return pDSDT;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
* Remove spaces from OEM ID and Table ID fields if any. Normally, if maker name
* is shorther than 6 bytes it will be trail-spaced, for eg. "DELL  " and "QA09   "
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static void stripTrailingSpaces(char* str)
{
    char* p = str;
    for (; *p; p++)
        ;
    for (--p; p >= str && *p == ' '; --p)
        *p = 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Obtain information for make and model to match against config in Info.plist
 * First, try to get data from Clover, it reads DMI and stores it in /efi/platform
 * DMI data won't match DSDT header TableID used for model and if DSDT patcher in 
 * Clover is used it will be "Apple ".
 * 
 * So, if you use Clover define your platform config based on DMI data
 *  or if you use Chameleon define it based on DSDT Table ID
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static OSString* getPlatformManufacturer()
{
    // try to get data from Clover first
    // considering auto patching may be used, so OEM ID will be set to "Apple "
    if (IORegistryEntry* platformNode = IORegistryEntry::fromPath("/efi/platform", gIODTPlane)) {
        
        if (OSData *data = OSDynamicCast(OSData, platformNode->getProperty("OEMVendor"))) {
            if (OSString *vendor = OSString::withCString((char*)data->getBytesNoCopy())) {
                if (OSString *manufacturer = getManufacturerNameFromOEMName(vendor)) {
                    DEBUG_LOG("CodecCommander::init: make %s\n", manufacturer->getCStringNoCopy());
                    return manufacturer;
                }
            }
        }
    }
    
    // otherwise use DSDT header
    const DSDT_HEADER* pDSDT = getDSDT();
    if (!pDSDT)
        return NULL;
    // copy to static data, NUL terminate, strip trailing spaces, and return
    static char oemID[sizeof(pDSDT->oemID)+1];
    bcopy(pDSDT->oemID, oemID, sizeof(pDSDT->oemID));
    oemID[sizeof(oemID)-1] = 0;
    stripTrailingSpaces(oemID);
    DEBUG_LOG("CodecCommander::init: make %s\n", oemID);
    return OSString::withCStringNoCopy(oemID);
}

static OSString* getPlatformProduct()
{
    // try to get data from Clover first
    if (IORegistryEntry* platformNode = IORegistryEntry::fromPath("/efi/platform", gIODTPlane)) {
        
        if (OSData *data = OSDynamicCast(OSData, platformNode->getProperty("OEMBoard"))) {
            if (OSString *product = OSString::withCString((char*)data->getBytesNoCopy())) {
                DEBUG_LOG("CodecCommander::init: model %s\n", product->getCStringNoCopy());
                return product;
            }
        }
    }
    
    const DSDT_HEADER* pDSDT = getDSDT();
    if (!pDSDT)
        return NULL;
    // copy to static data, NUL terminate, strip trailing spaces, and return
    static char oemTableID[sizeof(pDSDT->oemTableID)+1];
    bcopy(pDSDT->oemTableID, oemTableID, sizeof(pDSDT->oemTableID));
    oemTableID[sizeof(oemTableID)-1] = 0;
    stripTrailingSpaces(oemTableID);
    DEBUG_LOG("CodecCommander::init: model %s\n", oemTableID);
    return OSString::withCStringNoCopy(oemTableID);
}

static OSDictionary* _getConfigurationNode(OSDictionary *root, const char *name);

static OSDictionary* _getConfigurationNode(OSDictionary *root, OSString *name)
{
    OSDictionary *configuration = NULL;
    
    if (root && name) {
        if (!(configuration = OSDynamicCast(OSDictionary, root->getObject(name)))) {
            if (OSString *link = OSDynamicCast(OSString, root->getObject(name))) {
                const char* p1 = link->getCStringNoCopy();
                const char* p2 = p1;
                for (; *p2 && *p2 != ';'; ++p2);
                if (*p2 != ';') {
                    configuration = _getConfigurationNode(root, link);
                }
                else {
                    if (OSString* strip = OSString::withString(link)) {
                        strip->setChar(0, (unsigned)(p2 - p1));
                        configuration = _getConfigurationNode(root, strip);
                        strip->release();
                    }
                }
            }
        }
    }
    
    return configuration;
}

static OSDictionary* _getConfigurationNode(OSDictionary *root, const char *name)
{
    OSDictionary *configuration = NULL;
    
    if (root && name) {
        OSString *nameNode = OSString::withCStringNoCopy(name);
        
        configuration = _getConfigurationNode(root, nameNode);
        
        OSSafeRelease(nameNode);
    }
    
    return configuration;
}

OSDictionary* CodecCommander::getConfigurationNode(OSDictionary* list, OSString *model)
{
    OSDictionary *configuration = NULL;
    
    if (OSString *manufacturer = getPlatformManufacturer())
        if (OSDictionary *manufacturerNode = OSDynamicCast(OSDictionary, list->getObject(manufacturer)))
            if (!(configuration = _getConfigurationNode(manufacturerNode, getPlatformProduct())))
                if (!(configuration = _getConfigurationNode(manufacturerNode, model)))
                    configuration = _getConfigurationNode(manufacturerNode, kDefault);
    
    if (!configuration && !(configuration = _getConfigurationNode(list, model)))
        configuration = _getConfigurationNode(list, kDefault);
    
    return configuration;
}

EXPORT OSDictionary* CodecCommander::makeConfigurationNode(OSDictionary* list, OSString* model)
{
    if (!list)
        return NULL;
    
    OSDictionary* result = 0;
    OSDictionary* defaultNode = _getConfigurationNode(list, kDefault);
    OSDictionary* platformNode = getConfigurationNode(list, model);
    if (defaultNode)
    {
        // have default node, result is merge with platform node
        result = OSDictionary::withDictionary(defaultNode);
        if (result && platformNode)
            result->merge(platformNode);
    }
    else if (platformNode)
    {
        // no default node, try to use just platform node
        result = OSDictionary::withDictionary(platformNode);
    }
    return result;
}