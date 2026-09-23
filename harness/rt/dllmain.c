/* Entry point for tvtts.dll.  The library has no process-wide state to set
 * up -- msvcrt is a shared CRT and is ready when we are loaded -- so this
 * only has to exist and succeed. */
int __attribute__((stdcall))
DllMainCRTStartup(void *instance, unsigned reason, void *reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return 1;
}
