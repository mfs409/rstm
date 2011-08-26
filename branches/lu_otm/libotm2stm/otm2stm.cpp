#include <stdio.h>
#include <stdlib.h>
#include "alt-license/OracleSkyStuff.hpp"
#include "common/locks.hpp"

__thread void* myDescriptor = NULL;

//#define DEBUG(...) printf(__VA_ARGS__)
#define DEBUG(...)

volatile uint32_t LOCK = 0;

namespace stm
{
  // [mfs] stopped here.  const char*stm::get_algname() not defined yet
}


extern "C"
{
    void* STM_GetMyTransId()
    {
        void* ret = (void*)&myDescriptor;
        DEBUG("Call to STM_GetMyTransID returning 0x%p\n", ret);
        if (myDescriptor == NULL) {
            getStackInfo();
            myDescriptor = (void*)1;
        }
        return ret;
    }

    BOOL  STM_BeginTransaction(void* theTransId)
    {
        DEBUG("Call to STM_BeginTransaction by 0x%p\n", theTransId);
        while (!bcas32(&LOCK, 0, 1)) { }
        return 1;
    }

    BOOL  STM_ValidateTransaction(void* theTransId)
    {
        DEBUG("Call to STM_ValidateTransaction by 0x%p\n", theTransId);
        return 1;
    }

    CommitStatus  STM_CommitTransaction(void* theTransId)
    {
        DEBUG("Call to STM_CommitTransaction by 0x%p\n", theTransId);
        LOCK = 0;
        return CommittedNoRetry;
    }

    RdHandle* STM_AcquireReadPermission (void* theTransId, void* theAddr, BOOL theValid)
    {
        DEBUG("Call to STM_AcquireReadPermission by 0x%p\n", theTransId);
        return NULL;
    }


    WrHandle* STM_AcquireWritePermission (void* theTransId, void* theAddr, BOOL theValid)
    {
        DEBUG("Call to STM_AcquireWritePermission by 0x%p\n", theTransId);
        return NULL;
    }


    UINT32 STM_TranRead32 (void* theTransId, RdHandle* theRdHandle, UINT32* theAddr, BOOL theValid)
    {
        DEBUG("Call to STM_TranRead32 by 0x%p\n", theTransId);
        return *theAddr;
    }

    BOOL  STM_TranWrite32 (void* theTransId, WrHandle* theWrHandle, UINT32* theAddr, UINT32 theVal, BOOL theValid)
    {
        DEBUG("Call to STM_TranWrite32 by 0x%p\n", theTransId);
        *theAddr = theVal;
        return true;
    }

    void* STM_TranMalloc(void* theTransId, size_t theSize)
    {
        return malloc(theSize);
    }

    void  STM_TranMFree(void* theTransId, void *theMemBlock)
    {
        free(theMemBlock);
    }

} // extern "C"
