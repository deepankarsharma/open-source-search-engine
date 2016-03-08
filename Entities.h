#ifndef GB_ENTITIES_H
#define GB_ENTITIES_H

// Matt Wells, copyright Jul 2001

// . i only support characters from 0-255
// . i do not plan on having unicode support, but may think about it later
// . Partap seems like a chump...I'll probably make him do it

#include "Unicode.h"

// call these two
// JAB: const-ness for the optimizer
int32_t getEntity_a          ( const char *s , int32_t maxLen , uint32_t *c );

//int32_t getEntity_utf8 (char *s , int32_t maxLen , int32_t *d , int32_t *ds ) ;

#endif // GB_ENTITIES_H
