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
#define kConfiguration              "Configuration"
#define kDefault                    "Default"

// Constants for EAPD comman verb sending
#define kHDACodecAddress            "HDEF Codec Address"
#define kUpdateSpeakerNodeNumber    "Update Speaker Node"
#define kUpdateHeadphoneNodeNumber  "Update Headphone Node"

// Generate audio stream
#define kGenerateStream             "Generate Stream"
#define kStreamDelay                "Stream Delay"

// Workloop requred? and Workloop timer aka update interval, ms
#define kUpdateMultipleTimes        "Update Multiple Times"
#define kUpdateInterval             "Update Interval"

// Define variables for EAPD state updating
IOMemoryDescriptor *ioregEntry;

int updateCount = 0; //update counter
bool multiUpdate, generatePop, eapdPoweredDown, coldBoot, latched;
UInt8  codecNumber, spNodeNumber, hpNodeNumber, hdaEngineState;
UInt16 updateInterval, streamDelay, status;
UInt32 spCommandWrite, hpCommandWrite, spCommandRead, hpCommandRead, response;

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
    
    multiUpdate = false;
    generatePop = false;
    eapdPoweredDown = true;
    coldBoot = true; // assume booting from cold since hibernate is broken on most hacks
    latched  = false; // has command latched in IRR?
    
    // get configuration
    OSDictionary* list = OSDynamicCast(OSDictionary, dict->getObject(kConfiguration));
    OSDictionary* config = CodecCommander::makeConfigurationNode(list);
    
    // set configuration
    setParamPropertiesGated(config);
    OSSafeRelease(config);
    
    // set codec address and node number for EAPD status set
    spCommandWrite = (codecNumber << 28) | (spNodeNumber << 20) | 0x70c02;
    hpCommandWrite = (codecNumber << 28) | (hpNodeNumber << 20) | 0x70c02;
    
    // set codec address and node number for EAPD status get
    spCommandRead  = (codecNumber << 28) | (spNodeNumber << 20) | 0xf0c00;
    hpCommandRead  = (codecNumber << 28) | (hpNodeNumber << 20) | 0xf0c00;
    
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
 * CodecCommander::parseAudioEngineState - repeats the action when timer fires
 ******************************************************************************/

void CodecCommander::parseAudioEngineState()
{
    IORegistryEntry *hdaEngineOutputEntry = IORegistryEntry::fromPath(
                                                                      "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/HDEF@1B/AppleHDAController@1B/IOHDACodecDevice@1B,0/IOHDACodecDriver/IOHDACodecFunction@1B,0,1/AppleHDACodecGeneric/AppleHDADriver/AppleHDAEngineOutput@1B,0,1,1");
    // IOHDACodecDevice@1B,0 -> 0 - Codec Address Number
    // IOHDACodecFunction@1B,0,1 -> 0 - Codec Address Number, 1 - Function Group Number
    // AppleHDAEngineOutput@1B,0,1,1 -> 0 - Codec Address Number, 1 - Function Group Number, 1 - Engine Output Number
    
    if (hdaEngineOutputEntry != NULL) {
        OSNumber *state = OSDynamicCast(OSNumber, hdaEngineOutputEntry->getProperty("IOAudioEngineState"));
        if (state != NULL) {
            hdaEngineState = state->unsigned8BitValue();
            //DEBUG_LOG("CodecCommander:  EngineOutput power state %d\n", hdaEngineState);
            
            if (hdaEngineState == 0x1)
                DEBUG_LOG("CodecCommander:  r: audio stream active\n");
            //else
                //DEBUG_LOG("CodecCommander:  r: audio stream inactive\n"); // will produce spam in console
        }
        else {
            DEBUG_LOG("CodecCommander: IOAudioEngineState unknown\n");
            return;
        }
    }
    else {
        DEBUG_LOG("CodecCommander: AppleHDAEngineOutput@1B,0,1,1 is unreachable\n");
        return;
    }
    
    hdaEngineOutputEntry->release();
}

/******************************************************************************
 * CodecCommander::onTimerAction - repeats the action each time timer fires
 ******************************************************************************/

void CodecCommander::onTimerAction()
{
    /*
     if your kext is properly patched upon wake EAPD takes 1 write, sometimes 2 writes to re-enable.
     if delays are present in between streams behavior is very random, it could take 2 times
     and coult take 3, sometimes 1 so the workloop goes forever checking the audio engine state 
     and EAPD state when audio stream on engine output is present
     */
    
    // check if audio stream is up on given output
    parseAudioEngineState();
    // get EAPD status from command response if audio stream went up
    if (hdaEngineState == 0x1) {
        getOutputs();
         // if engine output stream has started, but EAPD isn't up
        if(response == 0x0) {
            setOutputs();
        }
    }
    
    fTimer->setTimeoutMS(updateInterval);

    /*
     if your kext is improperly patched your EAPD will be disabled 35 sec after *pop* stream stops
     however it will take exactly 2 PIOs for it to stay enabled until your next sleep cycle, but
     your jack sense will be always broken upon wake. re-patch your AppleHDA in case you see this behavior
     based on the log (PIO operation count will always remain at 2) or use the below workloop escape 
     method if you are unable to repach yourself and dont care about jacks.
    
    // if EAPD was re-enabled using bezel popping timeout should be cancelled, EAPD wont be disabled again
    if (generatePop && updateCount == 2) { // to be absolutely sure check if response == 0x2 too
        DEBUG_LOG("CodecCommander: cc: workloop ended after %d PIOs\n",  updateCount);
        IOLog("CodecCommander: EAPD re-enabled\n");
        fTimer->cancelTimeout();
    }
     
     */
    
}

/******************************************************************************
 * CodecCommander::start - start kernel extension and init PM
 ******************************************************************************/
bool CodecCommander::start(IOService *provider)
{
    DEBUG_LOG("CodecCommander: cc: commander version 2.1.0 starting\n");

    if(!provider || !super::start( provider ))
	{
		DEBUG_LOG("CodecCommander: cc: error loading kext\n");
		return false;
	}
    
    // notify about extra feature requests
    if(generatePop)
        DEBUG_LOG("CodecCommander: cc: stream requested, will *pop* upon wake\n");
    if(multiUpdate)
        DEBUG_LOG("CodecCommander: cc: workloop requested, will start upon wake\n");
    
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
    IORegistryEntry *hdaDeviceEntry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/HDEF@1B");
    if(hdaDeviceEntry == NULL) {
        hdaDeviceEntry = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PCI0/AppleACPIPCI/HDEF@1B");
    }
    
    if (hdaDeviceEntry != NULL) {
        IOService *service = OSDynamicCast(IOService, hdaDeviceEntry);
        
        // get address field from IODeviceMemory
        if (service != NULL && service->getDeviceMemoryCount() != 0) {
            ioregEntry = service->getDeviceMemoryWithIndex(0);
            
        }
        hdaDeviceEntry->release();
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
    
    // Get codec number (codec address)
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kHDACodecAddress))) {
        codecNumber = num->unsigned8BitValue();
    }
    
    // Get headphone node number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kUpdateHeadphoneNodeNumber))) {
        hpNodeNumber= num->unsigned8BitValue();
    }
    
    // Get speaker node number
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kUpdateSpeakerNodeNumber))) {
        spNodeNumber = num->unsigned8BitValue();
    }
    
    // Is *pop* generation required at wake ?
    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kGenerateStream))) {
        generatePop = (int)bl->getValue();
            
        // Get stream delay
        if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kStreamDelay))) {
            streamDelay = num->unsigned16BitValue();
        }
    }
    
    // Determine if multiple update is needed (for 10.9.2 and up)
    if (OSBoolean* bl = OSDynamicCast(OSBoolean, dict->getObject(kUpdateMultipleTimes))) {
        multiUpdate = (int)bl->getValue();
        
        // What is the update interval
        if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kUpdateInterval))) {
            updateInterval = num->unsigned16BitValue();
        }
    }
}

/******************************************************************************
 * CodecCommander::getOutputs & setOutputs - get/set EAPD status on SP/HP
 ******************************************************************************/

void CodecCommander::setOutputs()
{
    // delay sending codec verb command by 100ms, otherwise sometimes it breaks audio
    if (eapdPoweredDown) {
        IOSleep(100);
        if(spNodeNumber) {
            setStatus(spCommandWrite); // SP node only
            if (hpNodeNumber) // both SP/HP nodes
                setStatus(hpCommandWrite);
        }
        else // HP node only
            setStatus(hpCommandWrite);
    }
}

void CodecCommander::getOutputs()
{
    IOSleep(100);
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
    
    if (response == 0x2) { // bit 1 will be cleared after 35 second if AppleHDA is improperly patched!
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
    if(!coldBoot) {
        updateCount++;  // count the amount of times successfully enabling EAPD
        DEBUG_LOG("CodecCommander:  w: PIO operation #%d\n",  updateCount);
    }

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
    IOSleep(streamDelay);
    for (int i = 0; i < 2; i++) {
        if (_keyboardDevice)
            _keyboardDevice->keyPressed(0x20);
    }
}

/******************************************************************************
 * CodecCommander::setPowerState - set active power state
 ******************************************************************************/

IOReturn CodecCommander::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{

    if (kPowerStateSleep == powerStateOrdinal) {
        DEBUG_LOG("CodecCommander: cc: asleep\n");
        eapdPoweredDown = true;
        // coming from sleep, so no longer a cold boot
        coldBoot = false;
	}
	else if (kPowerStateNormal == powerStateOrdinal) {
        DEBUG_LOG("CodecCommander: cc: awake\n");
        // update external amp by sending verb command
        if (eapdPoweredDown) {
            updateCount = 0;
            setOutputs();

        }
        // if coming from sleep and pop requested
        if (!coldBoot && generatePop){
            /*
             behavior may change depending on the way your kext is patched, also on AppleHDA version
             with 2.6.0 (10.9.2) a workloop is always required, with 2.5.3 and below (10.9.1) it's not ...
            */
            if (multiUpdate) {
                fTimer->setTimeoutMS(300); // fire timer for workLoop
                DEBUG_LOG("CodecCommander: cc: workloop started\n");
            }
            /*
              *pop* after workloop starts but before we seek response, so that it would be
              possible to read EAPD state in both cases - at wake controller expects set and get actions
            */
            createAudioStream();
            if (!multiUpdate)
                getOutputs();
        }
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