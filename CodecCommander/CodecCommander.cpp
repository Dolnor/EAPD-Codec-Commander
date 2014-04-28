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

//REVIEW: avoids problem with Xcode 5.1.0 where -dead_strip eliminates these required symbols
#include <libkern/OSKextLib.h>
void* _org_rehabman_dontstrip_[] =
{
    (void*)&OSKextGetCurrentIdentifier,
    (void*)&OSKextGetCurrentLoadTag,
    (void*)&OSKextGetCurrentVersionString,
};

// Constats for Configuration
#define kConfiguration              "Configuration"
#define kDefault                    "Default"

// Constants for EAPD comman verb sending
#define kCodecAddressNumber         "Codec Address Number"
#define kEngineOutputNumber         "Engine Output Number"
#define kUpdateSpeakerNodeNumber    "Update Speaker Node"
#define kUpdateHeadphoneNodeNumber  "Update Headphone Node"

// Generate audio stream
#define kGenerateStream             "Generate Stream"
#define kStreamDelay                "Stream Delay"

// Workloop requred and Workloop timer aka update interval, ms
#define kCheckInfinitely           "Check Infinitely"
#define kCheckInterval             "Check Interval"
#define kSimulateHeadphoneJack      "Simulate Headphones"

// Define variables for EAPD state updating
IOMemoryDescriptor *ioregEntry;

char hdaDevicePath[0x3F];
char hdaDriverPath[0xBA];
char engineOutputPath[0xD8];

int updateCount = 0; //update counter
bool checkInfinite, generatePop, eapdPoweredDown, coldBoot;
UInt8  codecNumber, outputNumber, spNodeNumber, hpNodeNumber, shpNodeNumber, hdaCurrentPowerState, hdaPrevPowerState, hdaEngineState;
UInt16 updateInterval, streamDelay, status;
UInt32 spCommandWrite, hpCommandWrite, spCommandRead, hpCommandRead, shpCommandEnable, shpCommandDisable, response;

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
    
    // get configuration
    OSDictionary* list = OSDynamicCast(OSDictionary, dict->getObject(kConfiguration));
    OSDictionary* config = CodecCommander::makeConfigurationNode(list);
    
    // set configuration
    setParamPropertiesGated(config);
    OSSafeRelease(config);
    
    // set path for ioreg entries
    snprintf(hdaDevicePath, sizeof(hdaDevicePath),
             "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/HDEF@1B");
    snprintf(hdaDriverPath,sizeof(hdaDriverPath),
             "%s/AppleHDAController@1B/IOHDACodecDevice@1B,%d/IOHDACodecDriver/IOHDACodecFunction@1B,%d,1/AppleHDACodecGeneric/AppleHDADriver",
             hdaDevicePath,codecNumber,codecNumber);
    snprintf(engineOutputPath,sizeof(engineOutputPath),
             "%s/AppleHDAController@1B/IOHDACodecDevice@1B,%d/IOHDACodecDriver/IOHDACodecFunction@1B,%d,1/AppleHDACodecGeneric/AppleHDADriver/AppleHDAEngineOutput@1B,%d,1,%d",
             hdaDevicePath,codecNumber,codecNumber,codecNumber,outputNumber);
    
    // set codec address and node number for EAPD status set
    spCommandWrite = (codecNumber << 28) | (spNodeNumber << 20) | 0x70c02;
    hpCommandWrite = (codecNumber << 28) | (hpNodeNumber << 20) | 0x70c02;
    
    // set codec address and node number for EAPD status get
    spCommandRead  = (codecNumber << 28) | (spNodeNumber << 20) | 0xf0c00;
    hpCommandRead  = (codecNumber << 28) | (hpNodeNumber << 20) | 0xf0c00;
    
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
    // if infinite checks are enabled - essentially are for monitoring fugue state, sleep is fine with finite check
    if (checkInfinite) {
        // check if hda codec is powered
        parseCodecPowerState();
        // if no power after semi-sleep (fugue) state and power was restored - set EAPD bit
        if (eapdPoweredDown && (hdaCurrentPowerState == 0x1 || hdaCurrentPowerState == 0x2)) {
            DEBUG_LOG("CodecCommander: cc: --> hda codec power restored\n");
            setOutputs();
            // if popping requested - generate stream at fugue-wake
            if (!coldBoot && generatePop){
                createAudioStream();
            }
            // simulate headphone jack replug
            simulateHedphoneJack(); // <------ makes sure this is needed?
        }
    }
    
    // check if audio stream is up on given output
    parseAudioEngineState();
    // get EAPD bit status from command response if audio stream went up
    if (hdaEngineState == 0x1) {
        getOutputs();
         // if engine output stream has started, but EAPD isn't up
        if(response == 0x0) {
            // set EAPD bit
            setOutputs();
        }
    }

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
    
    // notify about extra feature requests
    if (generatePop && checkInfinite) {
        DEBUG_LOG("CodecCommander: cc: stream requested, will *pop* upon wake or fugue-wake\n");
    }
    if (checkInfinite) {
        DEBUG_LOG("CodecCommander: cc: infinite workloop requested, will start now!\n");
    }
    if (generatePop && !checkInfinite) {
        DEBUG_LOG("CodecCommander: cc: stream requested, will *pop* upon wake\n");
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
        DEBUG_LOG("CodecCommander: hi: keyboard device created\n");
        _keyboardDevice->registerService();
    }
    
    // determine HDEF ACPI device path in IORegistry
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

void CodecCommander::setOutputs()
{
    //DEBUG_LOG("CodecCommander:  r: hda codec power restored\n");
    if(spNodeNumber) {
        setStatus(spCommandWrite); // SP node only
        if (hpNodeNumber) // both SP/HP nodes
            setStatus(hpCommandWrite);
    }
    else // HP node only
        setStatus(hpCommandWrite);
}

void CodecCommander::getOutputs()
{
    if(spNodeNumber) {
        getStatus(spCommandRead);
        if (hpNodeNumber)
            getStatus(hpCommandRead);
    }
    else
        getStatus(hpCommandRead);
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
    
    //DEBUG_LOG("CodecCommander:  r: IRR read -> %d\n", response);
    
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
    if(!coldBoot && (cmd == spCommandWrite || cmd == hpCommandWrite)) {
        updateCount++;  // count the amount of times successfully enabling EAPD
        DEBUG_LOG("CodecCommander:  w: PIO operation #%d\n",  updateCount);
    }
    
    // mark EAPD bit as set
    eapdPoweredDown = false;
    
    DEBUG_LOG("CodecCommander: rw: IRV was set by hardware\n");
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
    DEBUG_LOG("CodecCommander: cc: --> simulate mute-unmute event\n");
    for (int i = 0; i < 2; i++) {
        if (_keyboardDevice)
            _keyboardDevice->keyPressed(0x20);
    }
}


/******************************************************************************
 * CodecCommander::simulateHeadphoneJack - plug and unplug headphones virtually
 ******************************************************************************/
void CodecCommander::simulateHedphoneJack()
{
    DEBUG_LOG("CodecCommander: cc: --> simulate headphone jack event\n");
    setStatus(shpCommandEnable);  // H-Phn PinCap Enable
    IOSleep(100);
    setStatus(shpCommandDisable); // H-Phn PinCap Disable
}

/******************************************************************************
 * CodecCommander::setPowerState - set active power state
 ******************************************************************************/

IOReturn CodecCommander::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{

    if (kPowerStateSleep == powerStateOrdinal) {
        DEBUG_LOG("CodecCommander: cc: --> asleep\n");
        eapdPoweredDown = true;
        // though this probably has been determined after parsing codec power state, we set this as false again
        coldBoot = false;
	}
	else if (kPowerStateNormal == powerStateOrdinal) {
        DEBUG_LOG("CodecCommander: cc: --> awake\n");
        updateCount = 0;
// This operation has to be performed right at wake or codec will enter power mode 0 immediately!
// *****
        // set EAPD bit at wake or cold boot
        if (eapdPoweredDown) {
            DEBUG_LOG("CodecCommander: cc: --> hda codec power restored\n");
            // delay setting by 100ms, otherwise immediate command won't be received
            IOSleep(100);
            setOutputs();
        }
        // only when this is done we can stars a check workloop!
// *****
        
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
        
        // generate audio stream at wake if requested
        if (!coldBoot && generatePop){
            // apply delay or it will not trigger a system event
            IOSleep(streamDelay);
            createAudioStream();
        }
        
        // simulate headphone jack replug
        simulateHedphoneJack();
    }
    
    return IOPMAckImplied;
}

/******************************************************************************
 * Methods for getting configuration dictionary, courtesy of RehabMan
 ******************************************************************************/
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

OSDictionary* CodecCommander::makeConfigurationNode(OSDictionary* list, OSString* model)
{
    if (!list)
        return NULL;
    
    OSDictionary* result = 0;
    OSDictionary* defaultNode = _getConfigurationNode(list, kDefault);
    if (defaultNode) {
        result = OSDictionary::withDictionary(defaultNode);
    }
    return result;
}