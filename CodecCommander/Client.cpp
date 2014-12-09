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

#include "CodecCommander.h"

const IOExternalMethodDispatch CodecCommanderClient::sMethods[kClientNumMethods] =
{
    { // kClientExecuteVerb
      (IOExternalMethodAction)&CodecCommanderClient::executeVerb,
      1, // One input
      0,
      1, // One output
      0
    }
};

/*
 * Define the metaclass information that is used for runtime
 * typechecking of IOKit objects. We're a subclass of IOUserClient.
 */

#define super IOUserClient
OSDefineMetaClassAndStructors(CodecCommanderClient, IOUserClient);

bool CodecCommanderClient::initWithTask(task_t owningTask, void* securityID, UInt32 type, OSDictionary* properties)
{
    IOLog("CodecCommanderClient::initWithTask(type %u)\n", (unsigned int)type);
    
    mTask = owningTask;
    
    return super::initWithTask(owningTask, securityID, type, properties);
}

bool CodecCommanderClient::start(IOService * provider)
{
    IOLog("CodecCommanderClient::start\n");
    
    if(!super::start(provider))
        return( false );
    
    /*
     * Our provider is always CodecCommander
     */
    assert(OSDynamicCast(CodecCommander, provider));
    mDriver = (CodecCommander*) provider;
    
    mOpenCount = 1;
    
    return true;
}

IOReturn CodecCommanderClient::clientClose(void)
{
    if (!isInactive())
        terminate();
    
    return kIOReturnSuccess;
}

void CodecCommanderClient::stop(IOService * provider)
{
    IOLog("CodecCommanderClient::stop\n");
    
    super::stop(provider);
}

IOReturn CodecCommanderClient::externalMethod(uint32_t selector, IOExternalMethodArguments* arguments,
                                              IOExternalMethodDispatch* dispatch, OSObject* target, void* reference)

{
    IOLog("%s[%p]::%s(%d, %p, %p, %p, %p)\n", getName(), this, __FUNCTION__, selector, arguments, dispatch, target, reference);
    
    if (selector < (uint32_t)kClientNumMethods)
    {
        dispatch = (IOExternalMethodDispatch *)&sMethods[selector];
        
        if (!target)
        {
            if (selector == kClientExecuteVerb)
                target = mDriver;
            else
                target = this;
        }
    }
    
    return super::externalMethod(selector, arguments, dispatch, target, reference);
}

IOReturn CodecCommanderClient::executeVerb(CodecCommander* target, void* reference, IOExternalMethodArguments* arguments)
{
    arguments->scalarOutput[0] = target->executeCommand((UInt32)arguments->scalarInput[0]);
    return kIOReturnSuccess;
}

