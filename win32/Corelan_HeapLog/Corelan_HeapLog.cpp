
/*
	This simple pin tool allows you to trace/log Heap related operations
	written by corelanc0d3r
	www.corelan.be
*/

#include "pin.H"
namespace WINDOWS
{
#include<Windows.h>
}
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <ctime>


/* ================================================================== */
// Global variables 
/* ================================================================== */


BOOL LogAlloc = true;
BOOL LogFree = true;
TLS_KEY alloc_key;
FILE* LogFile;
std::map<ADDRINT,WINDOWS::DWORD> chunksizes;

/* ================================================================== */
// Classes 
/* ================================================================== */

class ModuleImage
{
public:
	string ImageName;
	ADDRINT ImageBase;
	ADDRINT ImageEnd;

	void save_to_log()
	{
		//fprintf(LogFile, "** INFO ** Module %s loaded at 0x%p, module ends at 0x%p\n", ImageName, ImageBase, ImageEnd);
		fprintf(LogFile, "** Module %s loaded  (0x%p -> 0x%p) **\n", ImageName, ImageBase, ImageEnd);
	}
};

class HeapOperation
{
public:
	string operation_type;
	ADDRINT chunk_start;
	WINDOWS::DWORD chunk_size;
	ADDRINT chunk_end;
	ADDRINT saved_return_pointer;
	string srp_imagename;
	time_t operation_timestamp;

	void save_to_log()
	{
		int currentpid = PIN_GetPid();
		if (operation_type ==  "rtlallocateheap")
		{
			fprintf(LogFile, "PID: %u | alloc(0x%p) at 0x%p from 0x%p (%s)\n",currentpid,chunk_size,chunk_start,saved_return_pointer,srp_imagename);
		}
		else if (operation_type == "rtlfreeheap")
		{
			fprintf(LogFile, "PID: %u | free(0x%p) from 0x%p (size was 0x%p) (%s)\n",currentpid, chunk_start,saved_return_pointer,chunk_size,srp_imagename);
		}
	}

};

vector<HeapOperation> arrAllOperations;
vector<ModuleImage> arrLoadedModules;

std::map<string, ModuleImage> imageinfo;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */

KNOB<BOOL>   KnobLogAlloc(KNOB_MODE_WRITEONCE,  "pintool",
	"logalloc", "1", "Log heap allocations (RtlAllocateHeap and RtlReAllocateHeap)");

KNOB<BOOL>   KnobLogFree(KNOB_MODE_WRITEONCE,  "pintool",
	"logfree", "1", "Log heap free operations (RtlFreeHeap)");


/* ===================================================================== */
// Utilities
/* ===================================================================== */

INT32 Usage()
{

	return -1;
}


VOID logToFile(std::string textToLog)
{
	fprintf(LogFile, "%s\n", textToLog);
}


WINDOWS::DWORD findSize(ADDRINT address)
{
	// search in map for key address
	std::map<ADDRINT,WINDOWS::DWORD>::iterator it;

	it = chunksizes.find(address);
	if (it != chunksizes.end())
	{
		return it->second;
	}
	return 0;
}


ModuleImage addModuleToArray(IMG img)
{
	SEC sec = IMG_SecHead(img);
	ModuleImage modimage;
	modimage.ImageName = IMG_Name(img);
	modimage.ImageBase = IMG_LowAddress(img);
	modimage.ImageEnd = IMG_HighAddress(img);
	imageinfo[modimage.ImageName] = modimage;
	arrLoadedModules.push_back(modimage);
	return modimage;
}


string getModuleImageNameByAddress(ADDRINT address)
{
	string returnval = "";
	// iterate over array
	for (ModuleImage modimage : arrLoadedModules)
	{
		if (modimage.ImageBase <= address && address <= modimage.ImageEnd)
		{
			return modimage.ImageName;
		}
	}
	return returnval;
}


/* ===================================================================== */
// Analysis routines (runtime)
/* ===================================================================== */

VOID CaptureRtlAllocateHeapBefore(THREADID tid, UINT32 flags, int size)
{
	// At start of function, simply remember the requested size in TLS
	PIN_SetThreadData(alloc_key, (void *) size, tid);
}



VOID CaptureRtlAllocateHeapAfter(THREADID tid, ADDRINT addr, ADDRINT caller)
{
	// At end of function restore requested size and save data
	// avoid noise
	if (addr > 0x1000 && addr < 0x7fffffff)
	{
		//ADDRINT SavedRetPtr = (ADDRINT)PIN_GetContextReg(ctx, REG_EIP);
		int size = (int) PIN_GetThreadData(alloc_key, tid);
		
		// create new object
		HeapOperation ho_alloc;
		ho_alloc.operation_type = "rtlallocateheap";
		ho_alloc.chunk_start = addr;
		ho_alloc.chunk_size = size;
		ho_alloc.chunk_end = addr + size;
		ho_alloc.saved_return_pointer = caller;
		ho_alloc.operation_timestamp = time(0);
		string imagename = getModuleImageNameByAddress(caller);
		ho_alloc.srp_imagename = imagename;

		ho_alloc.save_to_log();

		arrAllOperations.push_back(ho_alloc);
		// add to map chunksizes
		chunksizes[addr] = size;

	}
}


VOID CaptureRtlFreeHeapBefore(ADDRINT addr, ADDRINT caller)
{
	
	// avoid noise
	if (addr > 0x1000 && addr < 0x7fffffff)
	{
		// create new object
		HeapOperation ho_free;
		ho_free.operation_type = "rtlfreeheap";
		ho_free.chunk_start = addr;
		ho_free.saved_return_pointer = caller;
		ho_free.operation_timestamp = time(0);

		string imagename = getModuleImageNameByAddress(caller);


		// see if we can get size from previous allocation
		WINDOWS::DWORD size = findSize(addr);		
		ho_free.chunk_size = size;
		ho_free.chunk_end = ho_free.chunk_start + size;
		ho_free.srp_imagename = imagename;

		ho_free.save_to_log();

		arrAllOperations.push_back(ho_free);

		// remove from chunksizes, because no longer relevant
		chunksizes.erase(addr);

	}
}



/* ===================================================================== */
// Instrumentation callbacks (instrumentation time)
/* ===================================================================== */

VOID AddInstrumentation(IMG img, VOID *v)
{
	// this instrumentation routine gets executed when an image is loaded

	// first, add image information to global array
	ModuleImage thisimage;
	thisimage = addModuleToArray(img);
	thisimage.save_to_log();

    // next, walk through the symbols in the symbol table to see if it contains the Heap related functions that we want to monitor
    //
    for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
    {
        string undFuncName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);

        //  Find the RtlAllocateHeap() function.
        if (undFuncName == "RtlAllocateHeap" && LogAlloc)
        {
            RTN allocRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
            
            if (RTN_Valid(allocRtn))
            {
                // Instrument to capture allocation address and original function arguments
				// at end of the RtlAllocateHeap function

                RTN_Open(allocRtn);

				fprintf(LogFile,"%s", "Adding instrumentation for RtlAllocateHeap\n");
                
                RTN_InsertCall(allocRtn, IPOINT_BEFORE, (AFUNPTR) &CaptureRtlAllocateHeapBefore,
                    IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                    IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_END);

                // return value is the address that has been allocated
                RTN_InsertCall(allocRtn, IPOINT_AFTER, (AFUNPTR) &CaptureRtlAllocateHeapAfter,
                    IARG_THREAD_ID, IARG_FUNCRET_EXITPOINT_VALUE, IARG_G_ARG0_CALLER, IARG_END);

         
                RTN_Close(allocRtn);
            }
        }
		//  Find the RtlFreeHeap() function.
        else if (undFuncName == "RtlFreeHeap" && LogFree)
        {
            RTN freeRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
            
            if (RTN_Valid(freeRtn))
            {
                RTN_Open(freeRtn);

				fprintf(LogFile,"%s", "Adding instrumentation for RtlFreeHeap\n");
                
                RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR) &CaptureRtlFreeHeapBefore,
                    IARG_FUNCARG_ENTRYPOINT_VALUE, 2,	// address
					IARG_G_ARG0_CALLER,					// saved return pointer
                    IARG_END);


                RTN_Close(freeRtn);
            }
        }


    }
}


BOOL FollowChild(CHILD_PROCESS childProcess, VOID * userData)
{
	fprintf(LogFile, "\n*******************************\nCreating child process %u\n*******************************\n\n", PIN_GetPid());
	return true;
}



VOID Fini(INT32 code, VOID *v)
{
	fprintf(LogFile,"\n\nNumber of heap operations logged: %d\n",arrAllOperations.size());
	fprintf(LogFile, "# EOF\n");
	fclose(LogFile);
}


/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library.
	PIN_Init(argc,argv);

	int currentpid = PIN_GetPid();
	stringstream ss;
	ss << "corelan_heaplog_";
	ss << currentpid;
	ss << ".log";
    string fileName = ss.str();
	LogAlloc = KnobLogAlloc.Value();
	LogFree = KnobLogFree.Value();

    if (!fileName.empty()) { LogFile = fopen(fileName.c_str(),"wb");}

	fprintf(LogFile, "Instrumentation started\n");

	PIN_InitSymbols();

	// we will need a way to pass data around, so we'll store stuff in TLS

	alloc_key = PIN_CreateThreadDataKey(0);


	if (LogAlloc)
	{
		fprintf(LogFile, "Logging heap alloc: YES\n");
	}
	else
	{
		fprintf(LogFile, "Logging heap alloc: NO\n");
	}

	if (LogFree)
	{
		fprintf(LogFile, "Logging heap free: YES\n");
	}
	else
	{
		fprintf(LogFile, "Logging heap free: NO\n");
	}

	fprintf(LogFile, "==========================================\n");

	// notify when following child process
	PIN_AddFollowChildProcessFunction(FollowChild, 0);

	// only add instrumentation if we have to :)

    if (LogAlloc || LogFree)
    {
        // Register function to be called to instrument traces
        IMG_AddInstrumentFunction(AddInstrumentation, 0);

        // Register function to be called when the application exits
        PIN_AddFiniFunction(Fini, 0);
    }

	fprintf(LogFile, "==========================================\n\n");

    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}