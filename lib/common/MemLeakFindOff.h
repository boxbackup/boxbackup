// --------------------------------------------------------------------------
//
// File
//		Name:    MemLeakFindOff.h
//		Purpose: Switch memory leak finding off
//		Created: 13/1/04
//
// --------------------------------------------------------------------------

// no header guard

#ifdef BOX_MEMORY_LEAK_TESTING

#undef new

#ifndef MEMLEAKFINDER_FULL_MALLOC_MONITORING
	#ifdef MEMLEAKFINDER_MALLOC_MONITORING_DEFINED
		#undef malloc
		#undef realloc
		#undef free
		#undef MEMLEAKFINDER_MALLOC_MONITORING_DEFINED
	#endif
#endif

#undef MEMLEAKFINDER_ENABLED

#endif
