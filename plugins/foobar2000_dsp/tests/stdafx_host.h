/* Precompiled header for the component smoke test host. Same include order
   rule as the component: the SDK first, because pfc insists on WinSock2.h
   ahead of windows.h. */

#ifndef DECRACKLE_TEST_STDAFX_HOST_H
#define DECRACKLE_TEST_STDAFX_HOST_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <SDK/foobar2000.h>
#include <SDK/component.h>

#endif // DECRACKLE_TEST_STDAFX_HOST_H
