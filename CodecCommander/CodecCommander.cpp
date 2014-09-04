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

//REVIEW: avoids problem with Xcode 5.1.0 where -dead_strip eliminates these required symbols
#include <libkern/OSKextLib.h>
void* _org_rehabman_dontstrip_[] =
{
    (void*)&OSKextGetCurrentIdentifier,
    (void*)&OSKextGetCurrentLoadTag,
    (void*)&OSKextGetCurrentVersionString,
};

// Constats for Configuration
#define kPlatformProfile            "Platform Profile"
#define kDefault                    "Default"

// Constants for EAPD comman verb sending
#define kHDEFLocation               "HDEF Device Location"
#define kCodecAddressNumber         "Codec Address Number"
#define kSendDelay                  "Send Delay"

// Workloop requred and Workloop timer aka update interval, ms
#define kCheckInfinitely            "Check Infinitely"
#define kCheckInterval              "Check Interval"

// Define variables for EAPD state updating
IOMemoryDescriptor *ioregEntry;

char hdaLocation[0x02];
char hdaDevicePath[0x3F];
char hdaDriverPath[0xBA];

UInt8 eapdCapableNodes[5];

int updateCount = 0;
bool checkInfinite, eapdPoweredDown, coldBoot;
UInt8  codecNumber, hdaCurrentPowerState, hdaPrevPowerState;
UInt16 updateInterval, sendDelay, status;
UInt32 resetCommand, psCommand, response;

// Define usable power states
static IOPMPowerState powerStateArray[ kPowerStateCount ] =
{
    { 1,0,0,0,0,0,0,0,0,0,0,0 },
    { 1,kIOPMDeviceUsable, kIOPMDoze, kIOPMDoze, 0,0,0,0,0,0,0,0 },
    { 1,kIOPMDeviceUsable, IOPMPowerOn, IOPMPowerOn, 0,0,0,0,0,0,0,0 }
};

OSDefineMetaClassAndStructors(CodecCommander, IOService)

/******************************************************************************
 * CodecCommander::init - parse kernel extension Info.plist
 ******************************************************************************/
bool CodecCommander::init(OSDictionary *dict)
{
    DEBUG_LOG("CodecCommander: cc: commander initializing\n");
    
    if (!super::init(dict))
        return false;
    
    fWorkLoop = 0;
    fTimer = 0;
    
    checkInfinite = false;
    eapdPoweredDown = true;
    coldBoot = true; // assume booting from cold since hibernate is broken on most hacks
    hdaCurrentPowerState = 0x0; // assume hda codec has no power at cold boot
    hdaPrevPowerState = hdaCurrentPowerState; //and previous state was the same
    
    // get configuration
    OSDictionary* list = OSDynamicCast(OSDictionary, dict->getObject(kPlatformProfile));
    OSDictionary* config = CodecCommander::makeConfigurationNode(list);
    
    // set configuration
    setParamPropertiesGated(config);
    OSSafeRelease(config);
    
    // set path for ioreg entries
    snprintf(hdaDevicePath, sizeof(hdaDevicePath),
             "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/HDEF@%s", hdaLocation);
    snprintf(hdaDriverPath,sizeof(hdaDriverPath),
             "%s/AppleHDAController@%s/IOHDACodecDevice@%s,%d/IOHDACodecDriver/IOHDACodecFunction@%s,%d,1/AppleHDACodecGeneric/AppleHDADriver",
             hdaDevicePath,hdaLocation,hdaLocation,codecNumber,hdaLocation, codecNumber);

    // assume default Function Group number is 1
    resetCommand = (codecNumber << 28) | (0x01 << 20) | 0x7ff00;
    psCommand    = (codecNumber << 28) | (0x01 << 20) | 0x70503; // PS-Set in bits 0:3, (011) D3 (or D3hot)
    
    return true;
}

/******************************************************************************
 * CodecCommander::parseCodecPowerState - get codec power state from IOReg
 ******************************************************************************/
void CodecCommander::parseCodecPowerState()
{
    // monitor power state of hda audio codec
    IORegistryEntry *hdaDriverEntry = IORegistryEntry::fromPath(hdaDriverPath);
    if (hdaDriverEntry != NULL) {
        OSNumber *powerState = OSDynamicCast(OSNumber, hdaDriverEntry->getProperty("IOAudioPowerState"));
        if (powerState != NULL) {
            hdaCurrentPowerState = powerState->unsigned8BitValue();
            // if hda codec changed power state
            if (hdaCurrentPowerState != hdaPrevPowerState) {
                // store current power state as previous state for next workloop cycle
                hdaPrevPowerState = hdaCurrentPowerState;
                // notify about codec power loss state
                if (hdaCurrentPowerState == 0x0) {
                    DEBUG_LOG("CodecCommander: cc: --> hda codec lost power\n");
                    setOutputs(0x0); // power down EAPDs properly
                    eapdPoweredDown = true;
                    coldBoot = false; //codec entered fugue state or sleep - no longer a cold boot
                    updateCount = 0;
                }
            }
        }
        else {
            DEBUG_LOG("CodecCommander: IOAudioPowerState unknown\n");
        }
        hdaDriverEntry->release();
    }
    else {
        DEBUG_LOG("CodecCommander: %s is unreachable\n", hdaDriverPath);
    }
}

/******************************************************************************
 * CodecCommander::onTimerAction - repeats the action each time timer fires
 ******************************************************************************/

void CodecCommander::onTimerAction()
{
    // check if hda codec is powered - we are monitoring ocurrences of fugue state
    parseCodecPowerState();
    // if no power after semi-sleep (fugue) state and power was restored - set EAPD bit
    if (eapdPoweredDown && (hdaCurrentPowerState == 0x1 || hdaCurrentPowerState == 0x2)) {
        DEBUG_LOG("CodecCommander: cc: --> hda codec power restored\n");
        setOutputs(0x2);
    }

    fTimer->setTimeoutMS(updateInterval);
}

/******************************************************************************
 * CodecCommander::start - start kernel extension and init PM
 ******************************************************************************/
bool CodecCommander::start(IOService *provider)
{
    IOLog("CodecCommander: version 2.2.0 starting\n");

    if(!provider || !super::start( provider ))
	{
		DEBUG_LOG("CodecCommander: cc: error loading kext\n");
		return false;
	}
    
    // determine if HDEF device path exists in IORegistry
    IORegistryEntry *hdaDeviceEntry = IORegistryEntry::fromPath(hdaDevicePath);
    if (hdaDeviceEntry != NULL) {
        IOService *service = OSDynamicCast(IOService, hdaDeviceEntry);
        
        // get address field from IODeviceMemory
        if (service != NULL && service->getDeviceMemoryCount() != 0) {
            ioregEntry = service->getDeviceMemoryWithIndex(0);
        }
        
        hdaDeviceEntry->release();
    }
    else {
        DEBUG_LOG("CodecCommander: %s is unreachable\n",hdaDevicePath);
        return false;
    }
    
    IOSleep(300); // need to wait a bit until codec can actually respond to immediate verbs
    int k = 0; // array index
    UInt8 startingNode;
    UInt8 totalNodes;
    
    //get start node number and node count
    getStatus((codecNumber << 28) | (0x01 << 20) | 0xf0004);
    if (response != 0) {
        startingNode = (response & 0xFF0000) >> 16;
        totalNodes =   ((response & 0x0000FF) >>  0) + 1;
        DEBUG_LOG("CodecCommander: start node 0x%x, total 0x%x\n", startingNode, totalNodes);
    }
    else
        DEBUG_LOG("CodecCommander: unable to determine node count\n");

    //fetch Pin Capabilities from the range of nodes
    DEBUG_LOG("CodecCommander: cc: --> getting EAPD supported node list (limited to 5)\n");
    for (int i = startingNode; i <= totalNodes; i++)
    {
        getStatus((codecNumber << 28) | (i << 20) | 0xf000c);
        //if bit 16 is set in pincap - node supports EAPD
        if (((response & 0xFF0000) >> 16) == 1)
        {
            eapdCapableNodes[k] = i;
            k++;
            IOLog("CodecCommander: NID=0x%02x supports EAPD, will update state after sleep\n", i);
        }
    }
    
    // notify about extra feature requests
    if (checkInfinite)
        DEBUG_LOG("CodecCommander: cc: infinite workloop requested, will start now!\n");
    
    // init power state management & set state as PowerOn
    PMinit();
    registerPowerDriver(this, powerStateArray, kPowerStateCount);
	provider->joinPMtree(this);
    
    // setup workloop and timer
    fWorkLoop = IOWorkLoop::workLoop();
    fTimer = IOTimerEventSource::timerEventSource(this,
                                                          OSMemberFunctionCast(IOTimerEventSource::Action, this,
                                                                               &CodecCommander::onTimerAction));
    if (!fWorkLoop || !fTimer)
        stop(provider);;
    
    if (fWorkLoop->addEventSource(fTimer) != kIOReturnSuccess)
        stop(provider);
    
	this->registerService(0);
    return true;
}

/******************************************************************************
 * CodecCommander::stop & free - stop and free kernel extension
 ******************************************************************************/
void CodecCommander::stop(IOService *provider)
{
    DEBUG_LOG("CodecCommander: cc: commander stopping\n");
    
    // if workloop is active - release it
    fTimer->cancelTimeout();
    fWorkLoop->removeEventSource(fTimer);
    OSSafeReleaseNULL(fTimer);// disable outstanding calls
    OSSafeReleaseNULL(fWorkLoop);

    updateCount = 0;
    PMstop();
    super::stop(provider);
}

#ifdef DEBUG
void CodecCommander::free(void)
{
	super::free();
}
#endif

/******************************************************************************
 * CodecCommander::setParamPropertiesGated - set variables based on user config
 ******************************************************************************/

void CodecCommander::setParamPropertiesGated(OSDictionary * dict)
{
    if (NULL == dict)
        return;
    
    // Get HDA device location address
    if (OSString* str = OSDynamicCast(OSString, dict->getObject(kHDEFLocation))) {
        if (str->getLength() > 1) {
            for (int i=0; i<=1; i++)
                hdaLocation [i] = str->getChar(i);
        } else {
            hdaLocation [0] = str->getChar(0);
        }
    }
    
    // Get codec address number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kCodecAddressNumber))) {
        codecNumber = num->unsigned8BitValue();
    }
    
    // Get delay for sending the verb
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kSendDelay))) {
        sendDelay = num->unsigned16BitValue();
    }
    
    // Determine if infinite check is needed (for 10.9 and up)
    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kCheckInfinitely))) {
        checkInfinite = (int)bl->getValue();
        
        if (checkInfinite) {
            // What is the update interval
            if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kCheckInterval))) {
                updateInterval = num->unsigned16BitValue();
            }
        }
    }
}

/******************************************************************************
 * CodecCommander::setOutputs - set EAPD status bit on SP/HP
 ******************************************************************************/

void CodecCommander::setOutputs(UInt8 logicLevel)
{
    // delay by at least 100ms, otherwise first immediate command won't be received
    // some codecs will produce loud pop when EAPD is enabled too soon, need custom delay until codec inits
    if (sendDelay < 100)
        IOSleep(100);
    else
        IOSleep(sendDelay);
    // for nodes supporting EAPD bit 1 in logicLevel defines EAPD logic state: 1 - enable, 0 - disable
    for (int i = 0; i <= sizeof(eapdCapableNodes)/sizeof(eapdCapableNodes[0]); i++) {
        if (eapdCapableNodes[i] != 0) {
            setStatus((codecNumber << 28) | eapdCapableNodes[i] << 20 | 0x70c00 | logicLevel);
        }
    }
}

/******************************************************************************
* Verb command handling methods for getting and setting EAPD status
******************************************************************************/

void CodecCommander::getStatus(UInt32 cmd)
{
    if (ioregEntry == NULL) {
        return;
    }
    // write verb command-get F0Ch to ICW 60h:Bit0-31 field
    ioregEntry->writeBytes(0x60, &cmd, sizeof(cmd));
    DEBUG_LOG("CodecCommander:  r: ICW stored get command 0x%04x\n", cmd);
    
    // set ICB 68h:Bit0 to cause the verb to be sent over the link
    status = 0x1;
    ioregEntry->writeBytes(0x68, &status, sizeof(status));
    DEBUG_LOG("CodecCommander:  r: ICB was set, sending verb over the link\n");
    
    // wait for response to latch, get EAPD status from IRR 64h:Bit1
    for (int i = 0; i < 1000; i++) {
        ::IODelay(100);
        ioregEntry->readBytes(0x64, &response, sizeof(response));
    }
    
    DEBUG_LOG("CodecCommander:  r: codec responded: 0x%04x\n", response);
    
    clearIRV(); // prepare for next command
}

void CodecCommander::setStatus(UInt32 cmd){
    
    if (ioregEntry == NULL) {
        return;
    }
       
    // write verb command-set 70Ch with 8 bit payload to ICW 60h:Bit0-31 field
    ioregEntry->writeBytes(0x60, &cmd, sizeof(cmd));
    DEBUG_LOG("CodecCommander:  w: ICW stored set command 0x%04x\n", cmd);
    
    // set ICB 68h:Bit0 to cause the verb to be sent over the link
    status = 0x1;
    ioregEntry->writeBytes(0x68, &status, sizeof(status));
    DEBUG_LOG("CodecCommander:  w: ICB was set, sending verb over the link\n");
    
    // wait for IRV 68h:Bit1 to be set by hardware
    for (int i = 0; i < 1000; i++) {
        ::IODelay(100);
        ioregEntry->readBytes(0x68, &status, sizeof(status));
        if (status & 0x2) { // we are good if IRV was set
            goto Success;
        }
    }

    // timeout reached, time to clear ICB
    status = 0x0;
    ioregEntry->writeBytes(0x68, &status, sizeof(status));
    // wait for ICB 68h:Bit0 to clear
    for (int i = 0; i < 1000; i++) {
        ::IODelay(100);
        // check ICB for the status of previous write
        ioregEntry->readBytes(0x68, &status, sizeof(status));
        if (status & 0x0) { // ICB cleared
            DEBUG_LOG("CodecCommander: rw: IRV wasn't set by hardware, ICB cleared\n");
        }
    }
 
Success:
    
    if((cmd & 0xFF) == 0x2) { // xyy70c-02
        updateCount++;  // count the amount of times successfully enabling EAPD
        DEBUG_LOG("CodecCommander: cc: --> PIO event #%d finished\n",  updateCount);
        // mark EAPD bit as set
        eapdPoweredDown = false;
    }

    DEBUG_LOG("CodecCommander:  w: IRV was set by hardware\n");
    clearIRV(); // prepare for next command
}

void CodecCommander::clearIRV()
{
    // clear IRV bit 1 preparing for next command write
    status = 0x2;
    ioregEntry->writeBytes(0x68, &status, sizeof(status));
    DEBUG_LOG("CodecCommander: rw: IRV cleared, allowing new commands\n");
}

/******************************************************************************
 * CodecCommander::performCodecReset - reset function group and set power to D3
 *****************************************************************************/

void CodecCommander::performCodecReset ()
{
    /*
     Reset is created by sending two Function Group resets, potentially separated 
     by an undefined number of idle frames, but no other valid commands.
     This Function Group “Double” reset shall do a full initialization and reset 
     most settings to their power on defaults.
     
     This function can be used to reset codec on dekstop boards, for example H87-HD3,
     to overcome audio loss and jack sense problem after sleep with AppleHDA v2.6.0+
     */

    if (!coldBoot){
        DEBUG_LOG("CodecCommander: cc: --> resetting codec\n");
        setStatus(resetCommand);
        IOSleep(100); // define smaller delays ????
        setStatus(resetCommand);
        IOSleep(100);
        // forcefully set power state to D3
        setStatus(psCommand);
        eapdPoweredDown = true;
        DEBUG_LOG("CodecCommander: cc: --> hda codec power restored\n");
    }
}

/******************************************************************************
 * CodecCommander::setPowerState - set active power state
 ******************************************************************************/

IOReturn CodecCommander::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{

    if (kPowerStateSleep == powerStateOrdinal) {
        DEBUG_LOG("CodecCommander: cc: --> asleep\n");
        setOutputs(0x0); // set EAPD logic level 0 to cause EAPD to power off properly
        eapdPoweredDown = true;  // now it's powered down for sure
        coldBoot = false;
        updateCount = 0; // reset PIO counter
	}
	else if (kPowerStateNormal == powerStateOrdinal) {
        DEBUG_LOG("CodecCommander: cc: --> awake\n");

        // issue codec reset at wake
        performCodecReset();
        
        if (eapdPoweredDown){
            // set EAPD bit at wake or cold boot
            setOutputs(0x2);
        }

        // if infinite checking requested
        if (checkInfinite){
            // if checking infinitely then make sure to delay workloop
            if (coldBoot) {
                fTimer->setTimeoutMS(20000); // create a nasty 20sec delay for AudioEngineOutput to initialize
            }
            // if we are waking it will be already initialized
            else {
                fTimer->setTimeoutMS(100); // so fire timer for workLoop almost immediately
            }
            DEBUG_LOG("CodecCommander: cc: --> workloop started\n");
        }
    }
    
    return IOPMAckImplied;
}

/******************************************************************************
 * Methods for getting configuration dictionary, courtesy of RehabMan
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
    if (name->isEqualTo("Gigabyte Technology Co., Ltd.")) manufacturer = OSString::withCString("Gigabyte");
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
 * Obtain information for make and model to match against config in Info.plist
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static OSString* getPlatformManufacturer()
{
    // try to get data from Clover/bareBoot first
    if (IORegistryEntry* platformNode = IORegistryEntry::fromPath("/efi/platform", gIODTPlane)) {
        
        if (OSData *data = OSDynamicCast(OSData, platformNode->getProperty("OEMVendor"))) {
            if (OSString *vendor = OSString::withCString((char*)data->getBytesNoCopy())) {
                if (OSString *manufacturer = getManufacturerNameFromOEMName(vendor)) {
                    DEBUG_LOG("CodecCommander: cc: board make - %s\n", manufacturer->getCStringNoCopy());
                    return manufacturer;
                }
            }
        }
    }
    
    // if not, then try from RehabMan OEM string on PS2K (useful chameleon/chimera)
    if (IORegistryEntry* ps2KeyboardDevice = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2K")) {
        
        if (OSString *vendor = OSDynamicCast(OSString, ps2KeyboardDevice->getProperty("RM,oem-id"))) {
            if (getManufacturerNameFromOEMName(vendor)) {
                DEBUG_LOG("CodecCommander: cc: board make - %s\n", vendor->getCStringNoCopy());
                return vendor;
            }
        }
    }

    return NULL;
}

static OSString* getPlatformProduct()
{
    // try to get data from Clover first
    if (IORegistryEntry* platformNode = IORegistryEntry::fromPath("/efi/platform", gIODTPlane)) {
        
        if (OSData *data = OSDynamicCast(OSData, platformNode->getProperty("OEMBoard"))) {
            if (OSString *product = OSString::withCString((char*)data->getBytesNoCopy())) {
                DEBUG_LOG("CodecCommander: cc: board model - %s\n", product->getCStringNoCopy());
                return product;
            }
        }
    }
    
    // then from PS2K
    if (IORegistryEntry* ps2KeyboardDevice = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2K")) {
        
        if (OSString *product = OSDynamicCast(OSString,ps2KeyboardDevice->getProperty("RM,oem-table-id"))) {
            if (product) {
                DEBUG_LOG("CodecCommander: cc: board model - %s\n", product->getCStringNoCopy());
                return product;
            }
        }
    }

    return NULL;
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

OSDictionary* CodecCommander::makeConfigurationNode(OSDictionary* list, OSString* model)
{
    if (!list)
        return NULL;
    
    OSDictionary* result = 0;
    OSDictionary* defaultNode = _getConfigurationNode(list, kDefault);
    OSDictionary* platformNode = getConfigurationNode(list, model);
    if (defaultNode) {
        // have default node, result is merge with platform node
        result = OSDictionary::withDictionary(defaultNode);
        if (result && platformNode)
            result->merge(platformNode);
    }
    else if (platformNode) {
        // no default node, try to use just platform node
        result = OSDictionary::withDictionary(platformNode);
    }

    return result;
}