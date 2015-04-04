
# really just some handy scripts...

KEXT=CodecCommander.kext
DIST=RehabMan-CodecCommander
BUILDDIR=./build/Products
INSTDIR=/System/Library/Extensions
OPTIONS:=$(OPTIONS) -scheme CodecCommander

ifeq ($(findstring 32,$(BITS)),32)
OPTIONS:=$(OPTIONS) -arch i386
endif

ifeq ($(findstring 64,$(BITS)),64)
OPTIONS:=$(OPTIONS) -arch x86_64
endif

.PHONY: all
all:
	xcodebuild build $(OPTIONS) -configuration Debug
	xcodebuild build $(OPTIONS) -configuration Release

.PHONY: clean
clean:
	xcodebuild clean $(OPTIONS) -configuration Debug
	xcodebuild clean $(OPTIONS) -configuration Release

.PHONY: update_kernelcache
update_kernelcache:
	sudo touch /System/Library/Extensions
	sudo kextcache -update-volume /

.PHONY: install_debug
install_debug:
	sudo cp $(BUILDDIR)/Debug/CodecCommanderClient /usr/bin/hda-verb
	if [ "`which tag`" != "" ]; then sudo tag -a Purple /usr/bin/hda-verb; fi
	sudo rm -Rf $(INSTDIR)/$(KEXT)
	sudo cp -R $(BUILDDIR)/Debug/$(KEXT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Purple $(INSTDIR)/$(KEXT); fi
	make update_kernelcache

.PHONY: install
install:
	sudo cp $(BUILDDIR)/Release/CodecCommanderClient /usr/bin/hda-verb
	if [ "`which tag`" != "" ]; then sudo tag -a Blue /usr/bin/hda-verb; fi
	sudo rm -Rf $(INSTDIR)/$(KEXT)
	sudo cp -R $(BUILDDIR)/Release/$(KEXT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Blue $(INSTDIR)/$(KEXT); fi
	make update_kernelcache

.PHONY: distribute
distribute:
	if [ -e ./Distribute ]; then rm -r ./Distribute; fi
	mkdir ./Distribute
	cp -R $(BUILDDIR)/Debug ./Distribute
	cp -R $(BUILDDIR)/Release ./Distribute
	mv ./Distribute/Debug/CodecCommanderClient ./Distribute/Debug/hda-verb
	mv ./Distribute/Release/CodecCommanderClient ./Distribute/Release/hda-verb
	find ./Distribute -path *.DS_Store -delete
	find ./Distribute -path *.dSYM -exec echo rm -r {} \; >/tmp/org.voodoo.rm.dsym.sh
	chmod +x /tmp/org.voodoo.rm.dsym.sh
	/tmp/org.voodoo.rm.dsym.sh
	rm /tmp/org.voodoo.rm.dsym.sh
	ditto -c -k --sequesterRsrc --zlibCompressionLevel 9 ./Distribute ./Archive.zip
	mv ./Archive.zip ./Distribute/`date +$(DIST)-%Y-%m%d.zip`
