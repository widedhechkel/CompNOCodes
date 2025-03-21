#ifndef __error_h
#define __error_h

#include <assert.h>


#ifdef __MSCVC_
#define errprintf(x) printf(x)
#define eprintf(x) printf(x)
#else
#define errprintf(fmt, args...){fprintf(stderr, "%s", __FUNCTION__); fprintf(stderr, fmt, ## args); }
#define eprintf(fmt, args...) fprintf(stderr, fmt,  ## args);
#endif

#ifdef DEBUG_TRACE
  #define PFUNC fprintf(stderr,"%s\n",__FUNCTION__);
#else
  #define PFUNC
#endif

#if (defined(DEBUG_TRACE) || defined(DEBUG))
  #define assertp(x) {fprintf(stderr,"%s : in \"%s\" line %d \n", __FILE__, __FUNCTION__, __LINE__);assert(x);}
#else
  #define assertp(x) assert(x)
#endif

/* To avoid all the time "#ifdef " ...*/

#if (defined(DEBUG_TRACE) || defined(DEBUG))
  #define dprintf(fmt,args...)  fprintf(stderr,"%s  : " fmt, __FUNCTION__,  ## args)
#else
#ifndef __MSCVC_
  #define dprintf(fmt,args...)
#else
  #define dprintf(x)
#endif
#endif
#endif
