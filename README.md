## EAPD Codec Commander

### What is the purpose of this?
Used for updating EAPD (External Amplifier) state on HDA (High Definition Audio) codecs that use given amp on Speaker or Headphone nodes (both, in some cases). In OS X EAPD gets powered down across sleep so audio remains non functional after waking the machine up. 

Usually, this external amp is present on laptops and ITX board, most common on machines with ALC269 and ALC665 codecs. When machine falls asleep the amp is powered down on speaker/headphone node and after waking up even though it seems like audio is working, there is no sound coming from speaker/headphones because amp requires a codec command verb sent to it in order to powered up.

This kext is intended to take care of this.

### How is this useful over patched IOAudioFamily?
People used to rely on custom IOAudioFamily - Apple's open source files were altered, incorporating a method (originally coded by km9) to update the EAPD after sleep. What's bad about this kind of approach is that it required sources for modification to happen… and as everyone probably knows by now, Apple tends to delay the release of sources for 3 weeks to 2 month after OS updates get released. 

No more waiting for sources, no need to be searching for a kext that matches your node layout and no need to have different kexts for different OS X versions (generations, if you will). This kext has OS X Target set to 10.6, so you are good for 10.6 throughout 10.9.

### How do I enable it?
You have to edit settings inside Info.plist. There are four Default settings defined there which have default values of:

				<key>Default</key>
				<dict>
					<key>Codec Address Number</key>
					<integer>0</integer>
					<key>Engine Output Number</key>
					<integer>1</integer>
					<key>Update Speaker Node</key>
					<integer>20</integer>
					<key>Update Headphone Node</key>
					<integer>0</integer>
					<key>Stream Delay</key>
					<integer>500</integer>
					<key>Generate Stream</key>
					<true/>
					<key>Update Interval</key>
					<integer>5000</integer>
					<key>Update Multiple Times</key>
					<true/>
					<key>Update Infinitely</key>
					<false/>
				</dict>

You need to know what is your codec address and what are the node numbers that have EAPD on them. This can be determined by looking in your codec dump, which you can get from any flavor of linux (basically, an ALSA Dump).
Look at the beginning of the dump, your HDEF codec address is defined there. Set 'HDEF Codec Address' according to that.

					Codec: Realtek ACL269VB
					Address: 0

To determine the node numbers for speaker/headphones or both just search in the dump file for EAPD:

				Node 0x14 [Pin Complex] wcaps 0x40018d: Stereo Amp-Out
  				  Control: name="Speaker Playback Switch", index=0, device=0
    				    ControlAmp: chs=3, dir=Out, idx=0, ofs=0
  				Amp-Out caps: ofs=0x00, nsteps=0x00, stepsize=0x00, mute=1
  				Amp-Out vals:  [0x00 0x00]
  				Pincap 0x00010014: OUT EAPD Detect
  				EAPD 0x2: EAPD
  				Pin Default 0x99130110: [Fixed] Speaker at Int ATAPI
    				    Conn = ATAPI, Color = Unknown
    				    DefAssociation = 0x1, Sequence = 0x0
    				    Misc = NO_PRESENCE
  				Pin-ctls: 0x40: OUT
  				Unsolicited: tag=00, enabled=0
  				Connection: 2
     				    0x0c 0x0d*

As you can see from the example above, the speaker node where EAPD amp resides is 0x14, which translates into 20 in decimal. This is the number you have to put in for 'Update Speaker Node'... if this is the only EAPD occurrence for your codec leave 'Update Headphone Node' as 0.  If your codec only has EAPD on Headphone node set 'Update Speaker Node' to 0 and adjust the 'Update Headphone Node' number accordingly. If EAPD is present on both - set both.

### Upon wake I loose audio from speaker, why is that?
There are versions of ALC269 that mute speaker after 30 sec if DISABLED mixer at node 0x0f is muted and no audio stream is passed through EAPD Amp-Out. Codec incorrectly reports internal connections. Chances are, headphone and mic jack sensing won’t work either. To fix this set: 

					<key>Generate Stream</key>
					<true/>

This will produce a popping sound (by issuing mute/resume), ensuring an audio stream is active during wake. If it doesn’t happen for you then try adjusting the delay value.

					<key>Stream Delay</key>
					<integer>500</integer>

For OS X versions below 10.9.2 that should work, but not with 10.9.2 because for some reason Apple decided to heavily alter the algos in AppleHDA 2.6.0 hence just enabling audio stream after sleep no longer works. If you have ALC269 (this generally doesn't happen on ALC665) and after updating to 10.9.2 audio is not resuming properly even with popping trick, you need to enable monitoring of audio stream. 

					<key>Update Interval</key>
					<integer>5000</integer>
					<key>Update Multiple Times</key>
					<true/>

What will happen is that the kext will still send a command verb at wake, then produce a popping sound. If there’s no active stream 35 second after that, AppleHDAAudioEngine and associated EAPD will be disabled again by codec. The kext will keep monitoring the state of audio engine and if it changes to ‘on’ (ie, you started playing an audio or changed volume.. better works for audio though) it will check for EAPD state. In case it’s determined that EAPD is disabled the verb will be sent to codec to enable it. If EAPD is enabled the kext will continue monitoring to make sure EAPD gets enabled twice. After two PIO operations the check loop will be cancelled and you will see ‘EAPD re-enabled’ message in console. This is because after two iterations EAPD will stay enabled up until your next sleep-wale cycle.

Sometimes behavior is random, it could take more than two PIO operations for EAPD to reenable, for this purpose another setting can be set in config.. 

					<key>Update Infinitely</key>
					<false/>

Enabling this option will make the kext check for EngineOutput state and EAPD state infinitely, timeout won’t be called after 2 PIOs. This check will be repeated with the update interval (ms) that you have defined in config and will be initiated as soon as machine wakes up. 

### I get a strange message in my console
If upon wake you are getting a message saying ‘ .../AppleHDAEngineOutput@1B,0,1,1 is unreachable’ this means that your EngineOutput has different address. You need to determine what is the Engine Output Number for the Output that has EAPD on it and set in configuration:

					<key>Engine Output Number</key>
					<integer>1</integer>

To determine the number you can either use IORegistryExplorer or just use a simple Terminal command (laptops generally will have just one output):

					ioreg | grep EngineOutput

The output will look similar to this:
					 +-o AppleHDAEngineOutput@1B,0,1,2  <class AppleHDAEngineOutput, id 0x100000355, registered, matched, active, busy 0 (0 ms), retain 31>

Here @1B is HDEF device location, 0 - Codec Address Number (you will have to have it set in config already), 1 - Function Group Number (will usually be 1, no setting present), 2 - Engine Output Number. So the last number is the one you are going to put in config if you see the aforementioned message.

### Changelog

Feb 03, 2013 v2.1.1:

- Got rid of Platform Profiles as this function has little to no use

- Implement the ability to specify the EngineOutput number (Engine Output Number in config)

- Possibility for EAPD status checking loop to go infinitely 

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

### Credits
- EAPD fix (resumable-mutable-sound-v1 for IOAudioFamily): km9

- 'Popping' mute bezel at wake idea: EMlyDinEsHMG

- Configuration parsing methods: RehabMan
