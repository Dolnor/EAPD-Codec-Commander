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

					<key>EAPD Command Verbs</key>
					<true/>
					<key>HDEF Codec Address</key>
					<integer>0</integer>
					<key>Update Speaker Node</key>
					<integer>20</integer>
					<key>Update Headphone Node</key>
					<integer>0</integer>

If you need EAPD updating by issuing codec command verbs set 'EAPD Command Verbs' to true, otherwise set it to false (but why you are using this kext anyway?).
You need to know what is your codec address (number) and what are the node numbers that have EAPD on them. This can be determined by looking in your codec dump, which you can get from any flavor of linux (basically, an ALSA Dump).
Look at the beginning of the dump, your HDEF codec address is defined there. Define 'HDEF Codec Address' according to that.

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

### Is multiple profile support present?
Yes! Thanks to methods implemented in VoodooPS2Controller by RehabMan. 
This kext also supports custom profiles (so you can use same kext on multiple machines if you define platform profiles for each machine). 
- For Clover bootloader you have to compile a debug build and DMI information for custom profile will be posted in Console log.
				kernel[0]: CodecCommander::init: make DELL
				kernel[0]: CodecCommander::init: model 0YW3P2
- For Chameleon/Chimera/bareBoot/XPC bootloaders you can also compile a debug build and check the Console log. Alternatively, open up your DSDT table and look for oemID and TableID in the header of it.
				DefinitionBlock ("DSDT.aml", "DSDT", 2, "DELL  ", "QA09   ", 0x00000000)
DELL would be your make and QA09 would be your model. 

These two methods never match, so make sure if you need a custom platform profile you get the info from right place.

### Credits
- EAPD fix (resumable-mutable-audio for IOAudioFamily): km9

- Multiple nodes updating via command verbs: EMlyDinEsHMG

- DMI info parsing from Clover: kozlek

- Platform Profile related methods: RehabMan
