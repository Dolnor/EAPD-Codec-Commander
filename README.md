## RehabMan Fork of TimeWalker's CodecCommander.kext

This repo contains a fork of TimeWalker's original here: https://github.com/Dolnor/EAPD-Codec-Commander


The main changes are:

- Modified Info.plist for the Lenovo U430.  See original README below if your codec is not configured the same.

- Added the ability to define another node in addition to speaker/headphone ("Update Alternate Node").  This is because the U430 has three nodes with the EAPD amp.

- Other minor build process changes

- Some minor code cleanup (mostly defensive measures)

Note: Do not expect it to work for your computer unless your codec is configured exactly as my Lenovo U430.


Future ideas:

- Bring back Platform Profile based configuration?

- Or as an alternate, configure via DSDT patches...


### Download location

Downloads are available on Bitbucket:

https://bitbucket.org/RehabMan/os-x-eapd-codec-commander/downloads


### Source Code:

The source code is maintained at the following sites:

https://github.com/RehabMan/OS-X-EAPD-Codec-Commander

https://bitbucket.org/RehabMan/os-x-eapd-codec-commander


### Original README.md follows...


## Codec Commander

### What is the purpose of this?
Used for updating EAPD (External Amplifier) state on HDA (High Definition Audio) codecs that use given amp on Speaker or Headphone nodes (both, or even extra ones in some cases). In OS X EAPD gets powered down across sleep so audio remains non functional after waking the machine up.

Usually, this external amp is present on laptops and ITX board, most common on machines with ALC269, ALC665 and similar codecs. When machine falls asleep the amp is powered down and after waking up, even though it seems like audio is working, there is no sound coming from speaker/headphones because amp requires a codec command verb sent to it in order to powered up.

This kext is intended to take care of this.

Additionally, starting from v2.2.0, Codec Commander can now solve a problem that plagues some desktop boards without EAPDs, but with a problem that causes to loose jack sense and sometimes audio upon wake. One of these boards is H87-HD3 with ALC892 onboard audio codec. For workaround, the codec is reset at wake, much like VoodooHDA acts, in order be treated by AppleHDA the same way as before sleep.

### How is this useful over patched IOAudioFamily?
People used to rely on custom IOAudioFamily - Apple's open source files were altered, incorporating a method (originally coded by km9) to update the EAPD after sleep. What's bad about this kind of approach is that it required sources for modification to happen… and as everyone probably knows by now, Apple tends to delay the release of sources for 3 weeks to 2 month after OS updates get released. 

No more waiting for sources, no need to be searching for a kext that matches your node layout and no need to have different kexts for different OS X versions (generations, if you will). 

### How do I enable it?
You have to edit settings inside Info.plist. There are multiple Default settings defined which have values of:

				<key>Default</key>
				<dict>
					<key>HDEF Device Location</key>
					<string>1B</string>
					<key>Codec Address Number</key>
					<integer>0</integer>
					<key>Check Interval</key>
					<integer>5000</integer>
					<key>Check Infinitely</key>
					<true/>
				</dict>

To determine configuration use a simple Terminal command (laptops generally will have just one output if Headphones and Speakers are paired in auto-detect):

					ioreg | grep EngineOutput

The output will look similar to this:
					 +-o AppleHDAEngineOutput@1B,0,1,1  <class AppleHDAEngineOutput, id 0x100000355, registered, matched, active, busy 0 (0 ms), retain 31>

Here:

1B - HDEF Device Location (which is defaulted to this in configuration, but on MCP7A chipset can be @8 for example),

0 - Codec Address Number (you will have to set this in config too),

1 - Function Group Number (will usually be 1, no setting present), 

1 - Engine Output Number.

### Upon resuming from semi-sleep I lose audio

Settings below help for fugue sleep introduced in 10.9, which you could break during 25 second delay and end up with disabled EAPD. Enable this setting if you find yourself interrupting fugue sleeps frequently :D 

					<key>Update Interval</key>
					<integer>5000</integer>
					<key>Check Infinitely</key>
					<true/>

Codec Commander will keep monitoring the codec power state transitions. If it’s determined that power to the codec was lost and then restored a verb will be sent to codec to enable EAPD. Enabling this for 10.8.5 and lower is totally useless.


### Changelog

September 4, 2014 v2.2.0

- Removed virtual keyboard class that was used for popping audio at wake

- Got rid of audio stream simulation which was using mute/resume NX system events at wake

- Eliminated headphone jack plug/unplug simulation due to being useless

- New algo to prevent jack sense loss and unpredicted codec power state transitions by performing the following at system wake (see extract from HDA spec in performCodecReset() for details):

  1. resetting codec function group (NID=0x01)

  2. forcefully setting codec power state to D3 Hot

- Added Gigabyte OEM name support

- CodecCommander can now be used on desktop boards with problematic codecs (some ALC892 implementations), where jack sense  and audio loss is a problem after sleep, even though there are no EAPDs to enable. Codec will be reset and set to D3 power state, thus simulating a scenario as if the codec was started cold

- Not monitoring EAPD state when audio stream is active anymore, since resetting the codec causes it to be enabled properly after first PIO.

- MCP workaround removed due to being useless with new reset functionality. MCP chipset can also use the setting to prevent audio loss after fugue sleep, which was previously limited to 4 PIOs 

- Node configuration no longer needed as EAPD enabled nodes are determined automatically when Codec Commander starts

- Fixed a bug with memory allocation which would cause system to lock up and restart when unloading the kext manually


July 13, 2014 v2.1.2:

- Add a procedure to set PinCaps on headphone node to Hpone enable, then Hphone disable to trigger a jack insertion event. Helps with audio loss on Intel controllers

- Add extra node to update in case codec utilizes more than 2x EAPDs on output nodes

- Add an ability to specify HDEF device address (location), Default Intel location is @1B

- Platform Profiles are brought back to support multiple machines with a single kext. Default config serves as a base, OEM config overrides default config. See examples…

- CodecCommander will first try to read OEM data reported to IODeviceTree by Clover/bareBoot, if none are reported it will try to get OEM info from IOService on PS2K device (RM,oem-id\RM,oem-table-id) and only then from DSDT header.

- Detect HDEF Controller chipset based on vendor-id data reported from IORegistry

- Properly power down EAPDs across sleep on Intel chipset

- Add support for nVidia MCP79/MCP7A HDA controllers that do not report EAPD state when reading response from IRR. A workaround is applied if such a chipset is detected to limit PIO count to 4, which is enough to keep speaker and headphone EAPDs enabled in 10.9.2+

- Remove unnecessary probing in order to prevent unresolved symbols when compiling with earlier SDKs for newer system version.


Feb 06, 2014 v2.1.1:

- Now monitoring codec power state from HDADriver, EAPD will re-enable after fugue-sleep state now too!

- Add a workaround to jack sense loss after 2 PIO operations (use Apple - Sleep and tap any key after 5 seconds, jack sense should work thereafter).

- Got rid of Platform Profiles as this function has little to no use

- Implement the ability to specify the EngineOutput number (Engine Output Number in config)

- EAPD status checking loop can go infinitely

- Get EAPD status bit if multiple updating is not requested, but bezel popping is 


Feb 01, 2014 v2.1.0:

- Implement virtual keyboard, add an ability to invoke mute-unmute event at wake to produce an audio stream at wake (Generate Stream toggle in config)

- Parse IORegistry to determine if audio stream is up on EngineOutput, if stream is up we are going to check whether EAPD bit is set

- Ability to get response from IRR register in order read EAPD status bit

- Added option to update EAPD status bit multiple times upon wake (Update Multiple Times in config), timeout will be called after 2 PIO operations

- Almost a complete rewrite, antipop no longer required (unless user decides otherwise)


Oct 15, 2013 v1.0.1:

- Performs PIO operation at cold boot as well as wake that will set EAPD status bit

- Required antipop 1.0.2 to generate audio stream upon wake or there will be no jack sense at wake

- Initial release 


### To-Do

- Automatically detect HDEF device location and codec number (regex? ioreg iterator?) thus simplifying the configuration even more


### Credits

- EAPD fix (resumable-mutable-sound-v1 for IOAudioFamily): km9

- Codec Function Group reset at wake idea: EMlyDinEsHMG

- Configuration parsing methods: RehabMan
