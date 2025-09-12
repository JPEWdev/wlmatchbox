#ifndef _WLMATCHAPP_SYMBOLS_H
#define _WLMATCHAPP_SYMBOLS_H

#if LIBWLMATCHAPP_BUILD
#define WLM_API __attribute__((__visibility__("default")))
#else
#define WLM_API
#endif

#endif
