/*
 * The few places where the engine reaches back into the SAPI layer that owns
 * it: the audio queue (which carries bookmark records as well as PCM) and
 * the window notifications.  Engine.sapi is NULL in the standalone build, so
 * none of this is reached there.
 */
#include "engine.h"

#if defined(TV_HOOK_BUILD)
#include <windows.h>

/* The SAPI layer's vector push (still the original's code). */
/* @0x100385b0 */
int32_t TV_THISCALL SapiQueue_Push(void *q, const void *data, uint32_t size);

void Sapi_Lock(SapiCentral *s)
{
    EnterCriticalSection((CRITICAL_SECTION *)s->audio_lock);
}

void Sapi_Unlock(SapiCentral *s)
{
    LeaveCriticalSection((CRITICAL_SECTION *)s->audio_lock);
}

void Sapi_Post(SapiCentral *s, uint32_t msg, uint32_t wp, uint32_t lp)
{
    PostMessageA((HWND)s->hwnd, msg, (WPARAM)wp, (LPARAM)lp);
}

int32_t Sapi_QueuePush(SapiCentral *s, const void *data, uint32_t size)
{
    return SapiQueue_Push(s->audio_queue, data, size);
}

#else

void Sapi_Lock(SapiCentral *s) { (void)s; }
void Sapi_Unlock(SapiCentral *s) { (void)s; }
void Sapi_Post(SapiCentral *s, uint32_t msg, uint32_t wp, uint32_t lp)
{
    (void)s; (void)msg; (void)wp; (void)lp;
}
int32_t Sapi_QueuePush(SapiCentral *s, const void *data, uint32_t size)
{
    (void)s; (void)data; (void)size;
    return 0;
}

#endif
