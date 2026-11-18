#ifndef SELFTEST_H
#define SELFTEST_H

#include <Types.h>

/* True when the harness has dropped the trigger file into the Preferences
   folder. Costs one FSMakeFSSpec at startup when it has not. */
Boolean SelfTest_Requested(void);

/* Drive the UI from inside the guest, write the log, then quit. Never
   returns. */
void SelfTest_Run(void);

#endif
