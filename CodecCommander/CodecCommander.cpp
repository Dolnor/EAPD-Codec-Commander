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
#include "CCHIDKeyboardDevice.h"

// Constats for Configuration
#define kPlatformProfile            "Platform Profile"
#define kDefault                    "Default"

// Constants for EAPD comman verb sending
#define kHDEFLocation               "HDEF Device Location"
#define kCodecAddressNumber         "Codec Address Number"
#define kEngineOutputNumber         "Engine Output Number"
#define kUpdateSpeakerNodeNumber    "Update Speaker Node"
#define kUpdateHeadphoneNodeNumber  "Update Headphone Node"
#define kUpdateExtraNodeNumber      "Update Extra Node"

// Generate audio stream
#define kGenerateStream             "Generate Stream"
#define kStreamDelay                "Stream Delay"

// Workloop requred and Workloop timer aka update interval, ms
#define kCheckInfinitely           "Check Infinitely"
#define kCheckInterval             "Check Interval"
#define kSimulateHeadphoneJack     "Simulate Headphones"

// Define variables for EAPD state updating
IOMemoryDescriptor *ioregEntry;

char hdaLocation[0x02];
char hdaDevicePath[0x3F];
char hdaDriverPath[0xBA];
char engineOutputPath[0xD8];

int updateCount = 0;
bool mcpWorkaround, shouldStop;
bool checkInfinite, generatePop, eapdPoweredDown, coldBoot;
UInt8  codecNumber, outputNumber, spNodeNumber, hpNodeNumber, shpNodeNumber, extNodeNumber, hdaCurrentPowerState, hdaPrevPowerState, hdaEngineState;
UInt16 updateInterval, streamDelay, status;
UInt32 spCommandWrite, hpCommandWrite, spCommandRead, hpCommandRead, shpCommandEnable, shpCommandDisable, extCommandWrite, extCommandRead, response;

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
    
    generatePop = false;
    checkInfinite = false;
    eapdPoweredDown = true;
    coldBoot = true; // assume booting from cold since hibernate is broken on most hacks
    hdaCurrentPowerState = 0x0; // assume hda codec has no power at cold boot
    hdaPrevPowerState = hdaCurrentPowerState; //and previous state was the same
    
    // mcp workaround
    mcpWorkaround=false;
    shouldStop=false;
    
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
    snprintf(engineOutputPath,sizeof(engineOutputPath),
             "%s/AppleHDAController@%s/IOHDACodecDevice@%s,%d/IOHDACodecDriver/IOHDACodecFunction@%s,%d,1/AppleHDACodecGeneric/AppleHDADriver/AppleHDAEngineOutput@%s,%d,1,%d",
             hdaDevicePath,hdaLocation,hdaLocation,codecNumber,hdaLocation,codecNumber,hdaLocation, codecNumber,outputNumber);
    
    // set codec address and node number for EAPD status set
    spCommandWrite = (codecNumber << 28) | (spNodeNumber << 20) | 0x70c00;
    hpCommandWrite = (codecNumber << 28) | (hpNodeNumber << 20) | 0x70c00;
    extCommandWrite =(codecNumber << 28) | (extNodeNumber << 20)| 0x70c00;
    
    // set codec address and node number for EAPD status get
    spCommandRead  = (codecNumber << 28) | (spNodeNumber << 20) | 0xf0c00;
    hpCommandRead  = (codecNumber << 28) | (hpNodeNumber << 20) | 0xf0c00;
    extCommandRead  = (codecNumber << 28) | (extNodeNumber << 20) | 0xf0c00;
    
    // commands for simulating headphone jack plugging and unplugging
    shpCommandEnable  = (codecNumber << 28) | (shpNodeNumber << 20) | 0x707c0;
    shpCommandDisable = (codecNumber << 28) | (shpNodeNumber << 20) | 0x70700;
    
    return true;
}

/******************************************************************************
 * CodecCommander::probe - check if there is something to attach to
 ******************************************************************************/
IOService *CodecCommander::probe(IOService *provider, SInt32 *score)
{
    DEBUG_LOG("CodecCommander: cc: commander probing\n");
    return this;
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
                    eapdPoweredDown = true;
                    coldBoot = false; //codec entered fugue state or sleep - no longer a cold boot
                    updateCount = 0;
                }
            }
        }
        else {
            DEBUG_LOG("CodecCommander: IOAudioPowerState unknown\n");
            return;
        }
    }
    else {
        DEBUG_LOG("CodecCommander: %s is unreachable\n", hdaDriverPath);
        return;
    }
    
    hdaDriverEntry->release();
}

/******************************************************************************
 * CodecCommander::parseAudioEngineState - repeats the action when timer fires
 ******************************************************************************/
void CodecCommander::parseAudioEngineState()
{
    IORegistryEntry *hdaEngineOutputEntry = IORegistryEntry::fromPath(engineOutputPath);
    if (hdaEngineOutputEntry != NULL) {
        OSNumber *state = OSDynamicCast(OSNumber, hdaEngineOutputEntry->getProperty("IOAudioEngineState"));
        if (state != NULL) {
            hdaEngineState = state->unsigned8BitValue();
            //DEBUG_LOG("CodecCommander:  EngineOutput power state %d\n", hdaEngineState);
            
            if (hdaEngineState == 0x1)
                DEBUG_LOG("CodecCommander: cc: --> audio stream active\n");
            //else
                //DEBUG_LOG("CodecCommander: cc: --> audio stream inactive\n"); // will produce spam in console
        }
        else {
            DEBUG_LOG("CodecCommander: IOAudioEngineState unknown\n");
            return;
        }
    }
    else {
        DEBUG_LOG("CodecCommander: %s is unreachable\n", engineOutputPath);
        return;
    }
    
    hdaEngineOutputEntry->release();
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
        // if popping requested - generate stream at fugue-wake
        if (!coldBoot && generatePop){
            createAudioStream();
        }
        // simulate headphone jack replug
        simulateHedphoneJack();
    }

    // check if audio stream is up on given output
    parseAudioEngineState();
    // get EAPD bit status from command response if audio stream went up
    if (hdaEngineState == 0x1) {
        getOutputs();
         // if engine output stream has started, but EAPD isn't up
        if(response == 0x0) {
            // set EAPD bit
            setOutputs(0x2);
        }
    }
    
    /* 
       MCP79 chipset doesn't seem to report EAPD state properly when reading IRR response EAPD bit.
       I'm unable to find a datasheet or a spec sheet that outlines how nVidia is different from Intel.
     
       To prevent false EAPD state detection and continious update, we limit loop to 4 PIOs, 
       which is enough to keep it enabled after sleep in 10.9.2+. 
       Since jacke sense is not a problem, we can sacrifice fugue wake EAPD updates for the better.
     */
    
    if (mcpWorkaround && updateCount >= 4)
        shouldStop=true;
    
    if(shouldStop){
        DEBUG_LOG("CodecCommander: cc: workloop ended after %d PIOs\n",  updateCount);
        shouldStop=false;
        fTimer->cancelTimeout();
    }
    else
        fTimer->setTimeoutMS(updateInterval);
}

/******************************************************************************
 * CodecCommander::start - start kernel extension and init PM
 ******************************************************************************/
bool CodecCommander::start(IOService *provider)
{
    DEBUG_LOG("CodecCommander: cc: commander version 2.1.1 starting\n");

    if(!provider || !super::start( provider ))
	{
		DEBUG_LOG("CodecCommander: cc: error loading kext\n");
		return false;
	}
    
    // start virtual keyboard device
    _keyboardDevice = new CCHIDKeyboardDevice;
    
    if ( !_keyboardDevice              ||
        !_keyboardDevice->init()       ||
        !_keyboardDevice->attach(this) )
    {
        _keyboardDevice->release();
        DEBUG_LOG("CodecCommander: hi: unable to create keyboard device\n");
    }
    else
    {
        // determine if HDEF device path exists in IORegistry and register CCHIDKeyboard IOService
        IORegistryEntry *hdaDeviceEntry = IORegistryEntry::fromPath(hdaDevicePath);
        if (hdaDeviceEntry != NULL) {
            IOService *service = OSDynamicCast(IOService, hdaDeviceEntry);
            
            // get address field from IODeviceMemory
            if (service != NULL && service->getDeviceMemoryCount() != 0) {
                ioregEntry = service->getDeviceMemoryWithIndex(0);
            }
            // determine HDA controller vendor from IOReg
            if (OSData *ctrlr= OSDynamicCast(OSData, hdaDeviceEntry->getProperty("vendor-id")))
            {
                UInt32 vendor = 0;
                unsigned length = ctrlr->getLength();
                if (length <= sizeof(vendor))
                    memcpy(&vendor, ctrlr->getBytesNoCopy(), length);
                
                DEBUG_LOG("CodecCommander: cc: chipset vendor - ");
                switch (vendor) {
                    case 0x8086:
                        DEBUG_LOG("Intel\n");
                        break;
                    case 0x10de:
                        DEBUG_LOG("nVidia\n");
                        mcpWorkaround = true;
                        break;
                        
                    default:
                        DEBUG_LOG("Unknown\n");
                        break;
                }
            }

            hdaDeviceEntry->release();
            // only register if HDEF device present, user may be trying voodoohda with renamed ACPI device
            _keyboardDevice->registerService();
            DEBUG_LOG("CodecCommander: hi: keyboard device created\n");
        }
        else {
            DEBUG_LOG("CodecCommander: %s is unreachable\n",hdaDevicePath);
            return false;
        }
    }
    
    // notify about extra feature requests
    if (generatePop && checkInfinite)
        DEBUG_LOG("CodecCommander: cc: stream requested, will *pop* upon wake or fugue-wake\n");
    if (checkInfinite)
        DEBUG_LOG("CodecCommander: cc: infinite workloop requested, will start now!\n");

    if (generatePop && !checkInfinite)
        DEBUG_LOG("CodecCommander: cc: stream requested, will *pop* upon wake\n");
    if (mcpWorkaround)
        DEBUG_LOG("CodecCommander: cc: running on MCP chipset, workloop limited to 4 PIOs\n");
    
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
    if (fWorkLoop) {
        if(fTimer) {
            OSSafeReleaseNULL(fTimer);// disable outstanding calls
        }
        OSSafeReleaseNULL(fWorkLoop);
    }
    // stop virtual keyboard device
    if (_keyboardDevice)
		_keyboardDevice->release();
        _keyboardDevice = NULL;
    
    PMstop();
    super::stop(provider);
}

void CodecCommander::free(void)
{
	super::free();
}

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
    
    // Get engine output number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kEngineOutputNumber))) {
        outputNumber = num->unsigned8BitValue();
    }
    
    // Get headphone node number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kUpdateHeadphoneNodeNumber))) {
        hpNodeNumber= num->unsigned8BitValue();
    }
    
    // Get speaker node number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kUpdateSpeakerNodeNumber))) {
        spNodeNumber = num->unsigned8BitValue();
    }
    
    // Get extra node number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kUpdateExtraNodeNumber))) {
        extNodeNumber = num->unsigned8BitValue();
    }
    
    // Get hp node number for simulating the unplug event
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kSimulateHeadphoneJack))) {
        shpNodeNumber = num->unsigned8BitValue();
    }
    
    // Is *pop* generation required at wake ?
    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kGenerateStream))) {
        generatePop = (int)bl->getValue();
            
        if (generatePop) {
            // Get stream delay
            if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kStreamDelay))) {
                streamDelay = num->unsigned16BitValue();
            }
        }
    }
    
    // Determine if infinite check is needed (for 10.9.2 and up)
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
 * CodecCommander::getOutputs & setOutputs - get/set EAPD status on SP/HP
 ******************************************************************************/

void CodecCommander::setOutputs(UInt8 logicLevel)
{
    IOSleep(100); // delay setting by 100ms, otherwise first immediate command won't be received
    // bit 1 in logicLevel defines EAPD logic state: 1 - enable, 0 - disable
    if(spNodeNumber)
        setStatus(spCommandWrite | logicLevel);
    if (hpNodeNumber)
        setStatus(hpCommandWrite | logicLevel);
    if (extNodeNumber)
        setStatus(extCommandWrite| logicLevel);
}

void CodecCommander::getOutputs()
{
    if(spNodeNumber)
        getStatus(spCommandRead);
    if (hpNodeNumber)
        getStatus(hpCommandRead);
    if (extNodeNumber)
        getStatus(extCommandRead);
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
    DEBUG_LOG("CodecCommander:  r: ICW stored get command %04x\n", cmd);
    
    // set ICB 68h:Bit0 to cause the verb to be sent over the link
    status = 0x1;
    ioregEntry->writeBytes(0x68, &status, sizeof(status));
    DEBUG_LOG("CodecCommander:  r: ICB was set, sending verb over the link\n");
    
    // wait for response to latch, get EAPD status from IRR 64h:Bit1
    for (int i = 0; i < 1000; i++) {
        ::IODelay(100);
        ioregEntry->readBytes(0x64, &response, sizeof(response));
    }
    
    clearIRV(); // prepare for next command
    if (response == 0x2) { // bit 1 will be cleared after 35 second!
        DEBUG_LOG("CodecCommander:  r: IRR is set, EAPD active\n");
        eapdPoweredDown = false;
    }
    if (response == 0x0) {
        DEBUG_LOG("CodecCommander:  r: IRR isn't set, EAPD inactive\n");
        eapdPoweredDown = true;
    }
}

void CodecCommander::setStatus(UInt32 cmd){
    
    if (ioregEntry == NULL) {
        return;
    }
       
    // write verb command-set 70Ch with 8 bit payload to ICW 60h:Bit0-31 field
    ioregEntry->writeBytes(0x60, &cmd, sizeof(cmd));
    DEBUG_LOG("CodecCommander:  w: ICW stored set command %04x\n", cmd);
    
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
    
    if((cmd & 0x000000FF) == 0x2) { // xyy70c-02
        updateCount++;  // count the amount of times successfully enabling EAPD
        DEBUG_LOG("CodecCommander: cc: --> PIO event #%d\n",  updateCount);
    }

    // mark EAPD bit as set
    eapdPoweredDown = false;
    DEBUG_LOG("CodecCommander: w: IRV was set by hardware\n");
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
 * CodecCommander::createAudioStream - generate *pop* sound to start a stream
 ******************************************************************************/

void CodecCommander::createAudioStream ()
{
    if (!coldBoot && generatePop){
        DEBUG_LOG("CodecCommander: cc: --> simulate mute-unmute event\n");
        IOSleep(streamDelay); // apply delay or it will not trigger a system event
        for (int i = 0; i < 2; i++) {
            if (_keyboardDevice)
                ::IODelay(100);
                _keyboardDevice->keyPressed(0x20);
        }
    }
}

/******************************************************************************
 * CodecCommander::simulateHeadphoneJack - plug and unplug headphones virtually
 ******************************************************************************/
void CodecCommander::simulateHedphoneJack()
{
    if (!coldBoot && shpNodeNumber) {
        DEBUG_LOG("CodecCommander: cc: --> simulate headphone jack event\n");
        setStatus(shpCommandEnable);  // H-Phn PinCap Enable
        IOSleep(200);
        setStatus(shpCommandDisable); // H-Phn PinCap Disable
    }
}

/******************************************************************************
 * CodecCommander::setPowerState - set active power state
 ******************************************************************************/

IOReturn CodecCommander::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{

    if (kPowerStateSleep == powerStateOrdinal) {
        DEBUG_LOG("CodecCommander: cc: --> asleep\n");
        DEBUG_LOG("CodecCommander: cc: --> amp assumed %s by now\n", eapdPoweredDown ? "disabled" : "enabled");
        setOutputs(0x0); // set EAPD logic level 0 to cause EAPD to power off properly
        eapdPoweredDown = true;  // now it's powered down for sure
        coldBoot = false;
        updateCount = 0; // reset PIO counter
	}
	else if (kPowerStateNormal == powerStateOrdinal) {
        DEBUG_LOG("CodecCommander: cc: --> awake\n");
        // set EAPD bit at wake or cold boot
        if (eapdPoweredDown) {
            DEBUG_LOG("CodecCommander: cc: --> hda codec power restored\n");
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
        
        // generate audio stream at wake
        createAudioStream();
        // simulate headphone jack replug
        simulateHedphoneJack();
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
    // try to get data from Clover/bareBoot first
    // considering auto patching may be used, so OEM ID will be set to "Apple "
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
    
    // otherwise use DSDT header
    const DSDT_HEADER* pDSDT = getDSDT();
    if (!pDSDT)
        return NULL;
    // copy to static data, NUL terminate, strip trailing spaces, and return
    static char oemID[sizeof(pDSDT->oemID)+1];
    bcopy(pDSDT->oemID, oemID, sizeof(pDSDT->oemID));
    oemID[sizeof(oemID)-1] = 0;
    stripTrailingSpaces(oemID);
    DEBUG_LOG("CodecCommander: cc: board make - %s\n", oemID);
    return OSString::withCStringNoCopy(oemID);
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
    
    const DSDT_HEADER* pDSDT = getDSDT();
    if (!pDSDT)
        return NULL;
    // copy to static data, NUL terminate, strip trailing spaces, and return
    static char oemTableID[sizeof(pDSDT->oemTableID)+1];
    bcopy(pDSDT->oemTableID, oemTableID, sizeof(pDSDT->oemTableID));
    oemTableID[sizeof(oemTableID)-1] = 0;
    stripTrailingSpaces(oemTableID);
    DEBUG_LOG("CodecCommander: cc: board model - %s\n", oemTableID);
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